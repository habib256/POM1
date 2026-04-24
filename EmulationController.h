#ifndef EMULATIONCONTROLLER_H
#define EMULATIONCONTROLLER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>

#include "CpuClock.h"
#include "POM1Build.h"
#if !POM1_IS_WASM
#include <thread>
#endif

#include "EmulationSnapshot.h"
#include "KeyboardController.h"
#include "M6502.h"
#include "Memory.h"
#include "Screen_ImGui.h"
#include "SnapshotPublisher.h"

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

    void queueKey(char key);
    void writeMemory(quint16 address, quint8 value);

    bool loadHexDump(const std::string& path, quint16& startAddress, std::string& error, int* bytesLoaded = nullptr);
    bool loadBinary(const std::string& path, quint16 startAddress, std::string& error, int* bytesLoaded = nullptr);
    bool loadBinaryToRam(const std::string& path, quint16 address, std::string& error);
    bool saveMemoryRange(const std::string& path, quint16 startAddress, quint16 endAddress, bool binaryFormat, std::string& error);

    void setWriteInRom(bool enabled);
    bool getWriteInRom() const;
    void setTerminalSpeed(int charsPerSecond);
    void setPresetRamKB(int kb);
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

    // P-LAB A1-SID Sound Card
    void setSIDEnabled(bool enabled);
    bool isSIDEnabled() const;
    void setSIDSpecialEditionEnabled(bool enabled);
    bool isSIDSpecialEditionEnabled() const;
    void setSIDChipModel(pom1::SID::ChipModel m);

    // P-LAB microSD Storage Card
    void setMicroSDEnabled(bool enabled);
    bool isMicroSDEnabled() const;
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

    // P-LAB Apple-1 Wi-Fi Modem
    void setWiFiModemEnabled(bool enabled);
    bool isWiFiModemEnabled() const;
    void wifiModemDisconnect();
    void wifiModemReset();

    // P-LAB Apple-1 Terminal Card
    void setTerminalCardEnabled(bool enabled);
    bool isTerminalCardEnabled() const;

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
    void jumpTo(quint16 address);

    /// Web (Emscripten) : pas de std::thread — avancer l’émulation depuis la boucle principale.
    void pumpEmulationMainThread(double deltaSeconds);

private:
    static constexpr quint16 kDefaultResetVector = 0xFF00;

private:
    void emulationLoop();
    void runEmulationSlice(double elapsedSeconds);

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

    quint16 preferredSoftResetVector = kDefaultResetVector;

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
};

#endif // EMULATIONCONTROLLER_H
