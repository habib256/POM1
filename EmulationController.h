#ifndef EMULATIONCONTROLLER_H
#define EMULATIONCONTROLLER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "M6502.h"
#include "Memory.h"
#include "Screen_ImGui.h"

struct EmulationSnapshot
{
    std::vector<quint8> memory = std::vector<quint8>(0x10000, 0);
    quint16 programCounter = 0;
    quint8 accumulator = 0;
    quint8 xRegister = 0;
    quint8 yRegister = 0;
    quint8 stackPointer = 0;
    quint8 statusRegister = 0;
    bool cpuRunning = false;
    bool keyReady = false;
    char lastKey = 0;
    bool writeInRom = false;
    int ramSizeKB = 0;
    bool cassetteLoadedTape = false;
    bool cassetteRecordedTape = false;
    bool cassettePlaybackActive = false;
    bool cassetteAudioAvailable = false;
    bool cassetteHardwareAccurateLiveAudio = true;
    double cassetteQueuedAudioSeconds = 0.0;
    size_t cassetteLoadedTransitionCount = 0;
    size_t cassetteRecordedTransitionCount = 0;
    std::string cassetteLoadedTapePath;
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

    void queueKey(char key);
    void writeMemory(quint16 address, quint8 value);

    bool loadHexDump(const std::string& path, quint16& startAddress, std::string& error);
    bool loadBinary(const std::string& path, quint16 startAddress, std::string& error);
    bool saveMemoryRange(const std::string& path, quint16 startAddress, quint16 endAddress, bool binaryFormat, std::string& error);

    void setWriteInRom(bool enabled);
    bool getWriteInRom() const;
    void setTerminalSpeed(int charsPerSecond);
    bool reloadBasic(std::string& error);
    bool reloadWozMonitor(std::string& error);
    bool reloadKrusader(std::string& error);
    void clearMemory();

    bool loadTape(const std::string& path, std::string& error);
    bool saveTape(const std::string& path, std::string& error);
    void rewindTape();
    void ejectTape();
    void clearTapeCapture();
    void setHardwareAccurateLiveAudio(bool enabled);

private:
    static constexpr quint16 kDefaultResetVector = 0xFF00;

private:
    void emulationLoop();
    void publishSnapshotLocked();
    void processQueuedKeysLocked();

private:
    Screen_ImGui* screen = nullptr;
    std::unique_ptr<Memory> memory;
    std::unique_ptr<M6502> cpu;

    mutable std::mutex stateMutex;
    mutable std::mutex snapshotMutex;
    mutable std::mutex keyMutex;
    std::condition_variable wakeCv;
    std::mutex wakeMutex;

    std::queue<char> queuedKeys;
    EmulationSnapshot latestSnapshot;
    quint16 preferredSoftResetVector = kDefaultResetVector;

    std::thread emulationThread;
    std::atomic<bool> terminateRequested { false };
    std::atomic<bool> runRequested { false };
    std::atomic<int> executionSpeedCyclesPerFrame { 16667 };
};

#endif // EMULATIONCONTROLLER_H
