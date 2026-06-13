#ifndef EMULATIONCONTROLLER_H
#define EMULATIONCONTROLLER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "CpuClock.h"
#include "POM1Build.h"
#if !POM1_IS_WASM
#include <thread>
#endif

#include "EmulationSnapshot.h"
#include "KeyboardController.h"
#include "M6502.h"
#include "Memory.h"
#include "RewindBuffer.h"
#include "Screen_ImGui.h"
#include "SnapshotPublisher.h"
#include "TMS9918.h"   // TMS9918::DropDiagnostics for getTms9918DropDiagnostics()

// Mutex ordering: stateMutex > keyboard's internal keyMutex > publisher's
// internal snapshotMutex. publisher.publish() is invoked while holding
// stateMutex; it then takes snapshotMutex internally. keyboard.drainTo() is
// also invoked under stateMutex; KeyboardController takes its own keyMutex
// via swap-out before calling setKeyPressed() with keyMutex released.
// Never acquire stateMutex while holding a nested mutex, or the emulation
// thread can deadlock.

/// std::mutex wrapper that exposes a waiter count. In MAX speed the
/// emulation thread reacquires stateMutex within nanoseconds of releasing
/// it; most schedulers then re-grant it to the same thread even when the
/// UI thread has been waiting. `waiters > 0` signals "someone else is
/// trying to get in" — the emulation loop checks this after each slice
/// and yields when true, so the UI can make progress on stateMutex-heavy
/// frames. Acts like std::mutex otherwise: BasicLockable, usable with
/// std::lock_guard<PriorityMutex>.
class PriorityMutex {
public:
    void lock() {
        waiters_.fetch_add(1, std::memory_order_relaxed);
        mtx_.lock();
        waiters_.fetch_sub(1, std::memory_order_relaxed);
    }
    void unlock() { mtx_.unlock(); }
    bool hasWaiters() const {
        return waiters_.load(std::memory_order_relaxed) > 0;
    }
private:
    std::mutex mtx_;
    std::atomic<int> waiters_{0};
};

class EmulationController
{
public:
    explicit EmulationController(Screen_ImGui* screen);
    ~EmulationController();

    void copySnapshot(EmulationSnapshot& out) const;

    void setExecutionSpeedCyclesPerFrame(int cyclesPerFrame);
    int getExecutionSpeedCyclesPerFrame() const;

    void startCpu();
    void stopCpu();
    void softReset();
    void hardReset();
    void stepCpu();

    // Debug: toggle the M6502 BRK trace (CPU state + stack + recent
    // control-flow transfers, logged at WARN on every BRK). Off by default.
    void setCpuBrkTraceEnabled(bool enabled);
    bool isCpuBrkTraceEnabled() const;
    // Debug: dump the CPU's PC ring buffer to the log on demand.
    void dumpCpuPcTrace(const char* tag);

    // PC-matched halt for headless/scripted debugging — see M6502::setBreakpoint.
    // Single-PC by design; the CPU stops itself when it next reaches `address`,
    // and `isCpuBreakpointTripped()` flips true. Continuing requires either a
    // manual `stepCpu()` past the address followed by `startCpu()`, or
    // `clearCpuBreakpoint()` + `startCpu()`.
    void     setCpuBreakpoint(uint16_t address);
    void     clearCpuBreakpoint();
    bool     hasCpuBreakpoint() const;
    uint16_t getCpuBreakpoint() const;
    bool     isCpuBreakpointTripped() const;

    void queueKey(char key);
    void writeMemory(uint16_t address, uint8_t value);

    bool loadHexDump(const std::string& path, uint16_t& startAddress, std::string& error,
                     int* bytesLoaded = nullptr,
                     std::vector<std::pair<uint16_t,uint16_t>>* zones = nullptr);
    bool loadBinary(const std::string& path, uint16_t startAddress, std::string& error, int* bytesLoaded = nullptr);
    bool loadBinaryToRam(const std::string& path, uint16_t address, std::string& error);
    bool saveMemoryRange(const std::string& path, uint16_t startAddress, uint16_t endAddress, bool binaryFormat, std::string& error);

    /// Snapshot save/load — see Memory::saveSnapshot for the format and the
    /// list of state currently captured. Both calls take stateMutex; the CPU
    /// is paused for the duration so the snapshot is internally consistent.
    /// loadSnapshot does NOT restart the CPU on its own — caller decides.
    bool saveSnapshot(const std::string& path, std::string& error) const;
    bool loadSnapshot(const std::string& path, std::string& error);

    // ── State rewind (microM8-style timeline) ─────────────────────────────
    // A bounded, delta-encoded ring of in-memory snapshots captured a few
    // times per second while the CPU runs. The UI exposes a scrub slider;
    // seeking pauses the CPU and restores the chosen frame. Resuming "here"
    // discards the rewound-past future and continues recording. All access
    // is funnelled through stateMutex — RewindBuffer has no locking of its
    // own (see RewindBuffer.h).
    struct RewindStatus {
        bool        enabled    = false;
        bool        previewing = false;   // paused on a rewound frame
        std::size_t frameCount = 0;
        std::size_t currentPos = 0;       // 0 = oldest, frameCount-1 = live
        std::size_t storedBytes = 0;
    };
    void setRewindEnabled(bool enabled);
    bool isRewindEnabled() const { return rewindEnabled_.load(); }
    void setRewindMemoryBudgetMB(int megabytes);
    RewindStatus getRewindStatus() const;
    void rewindSeekTo(std::size_t pos);       // preview: pause + restore frame
    void rewindResumeHere(std::size_t pos);   // restore + drop future + run
    void rewindResumeLive();                  // jump to newest frame + run

    void setWriteInRom(bool enabled);
    bool getWriteInRom() const;
    void setTerminalSpeed(int charsPerSecond);
    void setPresetRamKB(int kb);
    void setSiliconStrictMode(bool enabled);
    bool isSiliconStrictMode() const;
    // Silicon fidelity profile knobs. Each takes effect on the next
    // hardReset (or Memory::resetMemory). Defaults are OFF — historic
    // POM1 behaviour (MSX1 bistable VRAM, zero-init RAM) is preserved.
    void setVramNoiseOnReset(bool enabled);
    bool isVramNoiseOnReset() const;
    void setSystemRamNoiseOnReset(bool enabled);
    bool isSystemRamNoiseOnReset() const;
    // Juke-Box EEPROM 28c256 write-cycle timing knobs (no-op when chip is
    // in Flash mode or card is unplugged). All take a lock on stateMutex.
    void setJukeBoxEepromWriteCycleCpu(int cycles);
    int  getJukeBoxEepromWriteCycleCpu() const;
    uint64_t getJukeBoxEepromWritesTotal() const;
    uint64_t getJukeBoxEepromWritesDropped() const;
    bool isJukeBoxEepromWriteBusy() const;
    int  getJukeBoxEepromWriteBusyCycles() const;
    void resetJukeBoxEepromCounters();

    // Apple-1 DRAM refresh stall — see M6502::setDramRefreshEnabled().
    void setDramRefreshEnabled(bool enabled);
    bool isDramRefreshEnabled() const;
    uint64_t getDramRefreshStallCount() const;
    void resetDramRefreshStallCount();
    // TMS9918 silicon-strict diagnostics — forwarders to TMS9918::dropDiagnostics
    // family. Returns 0 / no-ops when the card is unplugged. Used by the
    // Hardware menu's "Dump TMS9918 drop diagnostics" item.
    uint64_t tms9918DropCount() const;
    void resetTms9918DropCount();
    void dumpTms9918DropDiagnostics(std::FILE* out = nullptr, int topN = 16) const;
    // Returns a COPY of the live drop diagnostics for the UI inspector.
    // Locks stateMutex internally so the unordered_map snapshot is safe.
    TMS9918::DropDiagnostics getTms9918DropDiagnostics() const;
    int getOutOfRangeAccessCount() const;
    void setOutOfRangeStrictMode(bool enable);
    bool isOutOfRangeStrictMode() const;
    bool reloadBasic(std::string& error);
    void unloadBasic();
    bool reloadApplesoftLite(std::string& error);
    bool reloadApplesoftLiteCFFA1(std::string& error);
    bool reloadApplesoftLiteSDCard(std::string& error);
    bool reloadWozMonitor(std::string& error);
    bool reloadKrusader(std::string& error);
    bool reloadAciRom(std::string& error);
    bool reloadSDCardRom(std::string& error);
    void clearMemory();

    bool loadTape(const std::string& path, std::string& error);
    bool loadProgramTape(const std::string& path, std::string& error);
    bool saveTape(const std::string& path, std::string& error);
    void rewindTape();
    void playTape();
    void stopTape();
    void pauseTape(bool paused);
    void seekTapeRelative(double deltaSeconds);
    void ejectTape();
    void clearTapeCapture();
    void setHardwareAccurateLiveAudio(bool enabled);
    void setCassetteVolume(float volume);
    // Arm recording on the tape deck without waiting for the first CPU
    // $C000 toggle. Used by the cassette deck's REC key and the CLI
    // `--rec` verb.
    void armCassetteRecord();

    // Apple Cassette Interface — unplug for the bare-4K preset.
    void setACIEnabled(bool enabled);
    bool isACIEnabled() const;

    // CassetteDevice audio source registration on the mixer. Separate
    // from the ACI plug because the audible playback belongs to the tape
    // deck, not the $C000/$C081 hooks. Deferred by MainWindow 15 frames
    // after CPU startup — adding the source before the CPU has run
    // reproduces the SID boot-silence bug on tapes.
    void activateCassetteAudioSource();
    void deactivateCassetteAudioSource();

    // P-LAB TMS9918 Graphic Card
    void setTMS9918Enabled(bool enabled);
    bool isTMS9918Enabled() const;

    // Uncle Bernie's GEN2 HGR Graphic Card. The rasteriser is owned by the
    // UI; this hook tells Memory whether the card is bus-attached — which
    // enables the $C250-$C257 soft-switch window + HST0 flag, advances the
    // cycle-accurate video scanner, and carves the HGR pages ($2000-$5FFF)
    // out of OOR strict mode.
    void setHgrFramebufferAttached(bool attached);
    bool isHgrFramebufferAttached() const;
    // GEN2 release card 50/60 Hz vertical-rate jumper (NTSC color either way).
    void setGen2FiftyHz(bool fiftyHz);
    bool isGen2FiftyHz() const;

    // P-LAB A1-SID Sound Card
    void setSIDEnabled(bool enabled);
    bool isSIDEnabled() const;
    void setSIDSpecialEditionEnabled(bool enabled);
    bool isSIDSpecialEditionEnabled() const;
    void setSIDChipModel(pom1::SID::ChipModel m);

    // P-LAB microSD Storage Card
    void setMicroSDEnabled(bool enabled);
    bool isMicroSDEnabled() const;
    // P-LAB IEC daughterboard (microSD daughterboard)
    void setIECCardEnabled(bool enabled);
    bool isIECCardEnabled() const;
    // UI thread-safe snapshot of IEC card state for the IEC Disk window.
    struct IECCardUIState {
        bool hasDisk = false;
        std::string diskPath;
        std::string label;
        std::string id;
        int blocksFree = 0;
        int totalBlocks = 0;
        struct Entry {
            std::string name;
            uint16_t blocks = 0;
            uint8_t type = 0;
        };
        std::vector<Entry> directory;
    };
    IECCardUIState getIECCardUIState() const;
    // Mount a .d64 disk image on the IEC card's virtual 1541. Replaces any
    // previously mounted disk. Returns true on success. Path may be absolute
    // or cwd-relative. Safe to call before/after setIECCardEnabled.
    bool mountIECDisk(const std::string& path);
    void unmountIECDisk();
    // Host filesystem root the microSD card maps onto. The desktop `Memory`
    // ctor probes `sdcard/`, `../sdcard`, `../../sdcard` and records the
    // first match; this getter returns that probed absolute path (empty
    // when no sdcard tree was found or on WASM). Used by the CLI
    // `--sd-mkdir` / `--sd-put` / `--sd-get` bypass verbs so host-side
    // fixture seeding operates on the same directory the emulator serves.
    std::string getMicroSDRootPath() const;

    // CFFA1 CompactFlash Interface
    void setCFFA1Enabled(bool enabled);
    bool isCFFA1Enabled() const;
    bool reloadCFFA1Rom(std::string& error);

    // P-LAB Apple-1 Juke-Box
    void setJukeBoxEnabled(bool enabled);
    bool isJukeBoxEnabled() const;
    void setJukeBoxJumper(JukeBox::Jumper jumper);
    JukeBox::Jumper getJukeBoxJumper() const;
    void setJukeBoxChipMode(JukeBox::ChipMode mode);
    JukeBox::ChipMode getJukeBoxChipMode() const;
    void setJukeBoxWritable(bool writable);
    bool isJukeBoxWritable() const;
    bool reloadJukeBoxRom(std::string& error);
    void setJukeBoxBankRegister(uint8_t value);
    bool copyJukeBoxPage(uint8_t fromPage, uint8_t toPage, std::string& error);
    bool saveJukeBoxRom(const std::string& path, std::string& error);

    // P-LAB CodeTank — fixed 16 kB ROM window at $4000-$7FFF, jumper picks
    // which 16 kB half of the 28c256 is wired in.
    void setCodeTankEnabled(bool enabled);
    bool isCodeTankEnabled() const;
    void setCodeTankJumper(CodeTank::Jumper jumper);
    CodeTank::Jumper getCodeTankJumper() const;
    bool loadCodeTankRom(const std::string& path, std::string& error);

    // P-LAB Apple-1 Wi-Fi Modem
    void setWiFiModemEnabled(bool enabled);
    bool isWiFiModemEnabled() const;
    void wifiModemDisconnect();
    void wifiModemReset();

    // P-LAB Apple-1 Terminal Card
    void setTerminalCardEnabled(bool enabled);
    bool isTerminalCardEnabled() const;

    // Dev telemetry side channel ($C440-$C443). setTelemetryListenPort() must
    // precede setTelemetryEnabled(true) — enabling opens the TCP server on the
    // current port. See doc/TELEMETRY_SIDE_CHANNEL.md.
    void setTelemetryEnabled(bool enabled);
    void setTelemetryListenPort(uint16_t port);
    void setTelemetryLogFile(const std::string& path);
    /// Serial Monitor → game: push synthetic inbound bytes into the TELE_IN FIFO,
    /// as if a TCP harness had sent them. No-op when telemetry is disabled.
    void telemetryInject(const uint8_t* data, std::size_t len);
    /// Release one frame while the CPU is parked on a lock-step ACK wait (the
    /// in-app "Step frame" button). Equivalent to a harness sending the ACK byte.
    void telemetryReleaseFrame();
    /// Render-loop accessor. Returns null when the card is disabled. The
    /// returned reference is owned by Memory and outlives any single frame;
    /// callers must not retain it across hardReset() / preset switch.
    class TerminalCard* getTerminalCardIfEnabled();

    // P-LAB Apple-1 I/O Board & RTC
    void setA1IO_RTCEnabled(bool enabled);
    bool isA1IO_RTCEnabled() const;

    // SWTPC PR-40 printer (Steve Jobs' Oct. 1976 Interface Age hack)
    void setPR40Enabled(bool enabled);
    bool isPR40Enabled() const;
    void setPR40SwitchMode(int mode);      // 0=Off 1=Mixed 2=PrintOnly
    int  getPR40SwitchMode() const;
    bool savePR40PaperRoll(const std::string& path, std::string& error) const;
    void clearPR40Paper();

    // SWTPC GT-6144 Graphic Terminal (1976) — write-only 64x96 framebuffer at $D00A.
    void setGT6144Enabled(bool enabled);
    bool isGT6144Enabled() const;
    // Freeze the A1-IO RTC to a wall-clock instant (seconds since epoch).
    // Used by `--rtc-freeze` for deterministic scripted runs. No-op when the
    // card isn't plugged (the offset still latches on A1IO_RTC so subsequent
    // enables pick it up).
    void setRtcOverrideTime(std::time_t target);

    // Jump the CPU to an arbitrary address (stop, rewrite reset vector,
    // hardReset, start). Used by `--run addr` when no binary was loaded at
    // that address — for a `--load addr:path --run addr` pair the
    // loadBinary path already handles reset, so `--run` is only required in
    // the naked form.
    void jumpTo(uint16_t address);

    /// Web (Emscripten) : pas de std::thread — avancer l’émulation depuis la boucle principale.
    void pumpEmulationMainThread(double deltaSeconds);

private:
    static constexpr uint16_t kDefaultResetVector = 0xFF00;

private:
    void emulationLoop();
    void runEmulationSlice(double elapsedSeconds);
    // Restore frame `pos` into Memory+CPU and republish. REQUIRES stateMutex held.
    void rewindRestoreFrame(std::size_t pos);

private:
    Screen_ImGui* screen = nullptr;
    std::unique_ptr<Memory> memory;
    std::unique_ptr<M6502> cpu;

    // Declared before emulationThread so their destructors run AFTER the
    // thread is joined in ~EmulationController — the emulation slice reads
    // and writes both `keyboard` (via drainTo) and `publisher` (via publish).
    KeyboardController keyboard;
    SnapshotPublisher publisher;

    mutable PriorityMutex stateMutex;
    std::condition_variable wakeCv;
    std::mutex wakeMutex;

    uint16_t preferredSoftResetVector = kDefaultResetVector;

#if !POM1_IS_WASM
    std::thread emulationThread;
#endif
    std::atomic<bool> terminateRequested { false };
    std::atomic<bool> runRequested { false };
    std::atomic<int> executionSpeedCyclesPerFrame { POM1_CPU_CYCLES_PER_FRAME_1X_60HZ };
    /// Dernière vitesse utilisée pour le budget temps réel (réinitialise le budget si elle change).
    int cycleBudgetAnchorCpf = -1;
    /// Budget de cycles partagé entre le fil d’émulation (natif) et pumpEmulationMainThread (Web).
    double emulationCycleBudget = 0.0;

    /// Wall-clock spent parked on a telemetry lock-step ACK wait (slice loop);
    /// reset when the CPU runs, trips kTelemetryStallTimeoutSec to auto-resume.
    double telemetryStallSeconds = 0.0;

    // ── State rewind ──────────────────────────────────────────────────────
    // rewindBuffer + rewindCaptureAccum are touched only under stateMutex
    // (capture in the slice, seek/clear via the public API). The atomics
    // mirror buffer status for lock-free reads from the UI thread.
    static constexpr double kRewindCaptureIntervalSec = 0.25;  // ~4 frames/s
    pom1::RewindBuffer rewindBuffer;
    double rewindCaptureAccum = 0.0;
    std::atomic<bool>        rewindEnabled_   { false };
    std::atomic<bool>        rewindPreviewing_{ false };
    std::atomic<std::size_t> rewindFrameCount_{ 0 };
    std::atomic<std::size_t> rewindPos_       { 0 };
    std::atomic<std::size_t> rewindStoredBytes_{ 0 };
};

#endif // EMULATIONCONTROLLER_H
