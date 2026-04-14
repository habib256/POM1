#ifndef EMULATIONCONTROLLER_H
#define EMULATIONCONTROLLER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
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
    bool reloadApplesoftLite(std::string& error);
    bool reloadWozMonitor(std::string& error);
    bool reloadKrusader(std::string& error);
    bool reloadAciRom(std::string& error);
    void clearMemory();

    bool loadTape(const std::string& path, std::string& error);
    bool saveTape(const std::string& path, std::string& error);
    void rewindTape();
    void playTape();
    void ejectTape();
    void clearTapeCapture();
    void setHardwareAccurateLiveAudio(bool enabled);

    // P-LAB TMS9918 Graphic Card
    void setTMS9918Enabled(bool enabled);
    bool isTMS9918Enabled() const;

    // P-LAB A1-SID Sound Card
    void setSIDEnabled(bool enabled);
    bool isSIDEnabled() const;

    // P-LAB microSD Storage Card
    void setMicroSDEnabled(bool enabled);
    bool isMicroSDEnabled() const;

    // CFFA1 CompactFlash Interface
    void setCFFA1Enabled(bool enabled);
    bool isCFFA1Enabled() const;
    bool reloadCFFA1Rom(std::string& error);

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

    mutable std::mutex stateMutex;
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
