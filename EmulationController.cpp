#include "EmulationController.h"
#include "POM1Build.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

constexpr double kFramesPerSecond = 60.0;
#if POM1_IS_WASM
// WASM: pumpEmulationMainThread is called once per frame from the main loop.
// At 60 fps, 1 MHz needs ~16 667 cycles/frame and 2 MHz ~33 333 cycles/frame.
// The desktop cap of 12 000 throttles WASM to 720 KHz — too slow for SID tunes.
// Set the cap well above the 2 MHz frame budget so a single call consumes it all.
constexpr int kMaxSliceCycles = 50000;
// WASM: emulation and audio share the main thread — need more lead time
// to avoid queue starvation between frames.
constexpr double kMaxLiveAudioLeadSeconds = 0.15;
#else
constexpr int kMaxSliceCycles = 12000;
constexpr double kMaxLiveAudioLeadSeconds = 0.025;
#endif

} // namespace

EmulationController::EmulationController(Screen_ImGui* screenWidget)
    : screen(screenWidget)
{
    memory = std::make_unique<Memory>();
    cpu = std::make_unique<M6502>(memory.get());

    memory->setDisplayCallback(Screen_ImGui::displayCallback);
    cpu->setDisplayCallback(Screen_ImGui::displayCallback);

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        memory->configureResetVectors(kDefaultResetVector);
        cpu->hardReset();
        cpu->start();
        publishSnapshotLocked();
    }

    runRequested.store(true);
#if !POM1_IS_WASM
    emulationThread = std::thread(&EmulationController::emulationLoop, this);
#endif
}

EmulationController::~EmulationController()
{
    terminateRequested.store(true);
    wakeCv.notify_all();
#if !POM1_IS_WASM
    if (emulationThread.joinable()) {
        emulationThread.join();
    }
#endif
}

void EmulationController::copySnapshot(EmulationSnapshot& out) const
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    out = latestSnapshot;
}

void EmulationController::setExecutionSpeedCyclesPerFrame(int cyclesPerFrame)
{
    executionSpeedCyclesPerFrame.store(cyclesPerFrame);
}

int EmulationController::getExecutionSpeedCyclesPerFrame() const
{
    return executionSpeedCyclesPerFrame.load();
}

void EmulationController::startCpu()
{
    runRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        cpu->start();
        publishSnapshotLocked();
    }
    wakeCv.notify_all();
}

void EmulationController::stopCpu()
{
    runRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        cpu->stop();
        publishSnapshotLocked();
    }
    wakeCv.notify_all();
}

void EmulationController::softReset()
{
    stopCpu();
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->configureResetVectors(preferredSoftResetVector);
    cpu->softReset();
    cpu->start();
    runRequested.store(true);
    publishSnapshotLocked();
    wakeCv.notify_all();
}

void EmulationController::hardReset()
{
    stopCpu();
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->resetMemory();
    memory->initMemory();
    preferredSoftResetVector = kDefaultResetVector;
    memory->configureResetVectors(kDefaultResetVector);
    cpu->hardReset();
    cpu->start();
    runRequested.store(true);
    if (screen) {
        screen->resetDisplay(); // garbage screen → auto-clear → welcome
    }
    publishSnapshotLocked();
    wakeCv.notify_all();
}

void EmulationController::stepCpu()
{
    stopCpu();
    std::lock_guard<std::mutex> lock(stateMutex);
    cpu->step();
    publishSnapshotLocked();
}

void EmulationController::queueKey(char key)
{
    {
        std::lock_guard<std::mutex> lock(keyMutex);
        queuedKeys.push(key);
    }
    wakeCv.notify_all();
}

void EmulationController::writeMemory(quint16 address, quint8 value)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->memWrite(address, value);
    publishSnapshotLocked();
}

bool EmulationController::loadBinaryToRam(const std::string& path, quint16 address, std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    int result = memory->loadBinary(path.c_str(), address);
    if (result != 0) {
        error = "Cannot load file";
        return false;
    }
    publishSnapshotLocked();
    return true;
}

bool EmulationController::loadHexDump(const std::string& path, quint16& startAddress, std::string& error, int* bytesLoaded)
{
    stopCpu();
    std::lock_guard<std::mutex> lock(stateMutex);

    quint16 addr = 0;
    int result = memory->loadHexDump(path.c_str(), addr, bytesLoaded);
    if (result != 0) {
        error = "Error: unable to load file";
        publishSnapshotLocked();
        return false;
    }

    if (screen) {
        screen->clear();
    }

    bool prevWriteInRom = memory->getWriteInRom();
    memory->setWriteInRom(true);
    memory->configureResetVectors(addr);
    memory->setWriteInRom(prevWriteInRom);
    preferredSoftResetVector = addr;
    cpu->hardReset();
    cpu->start();
    runRequested.store(true);
    startAddress = addr;
    publishSnapshotLocked();
    wakeCv.notify_all();
    return true;
}

bool EmulationController::loadBinary(const std::string& path, quint16 startAddress, std::string& error, int* bytesLoaded)
{
    stopCpu();
    std::lock_guard<std::mutex> lock(stateMutex);

    int result = memory->loadBinary(path.c_str(), startAddress, bytesLoaded);
    if (result != 0) {
        error = "Error: unable to load file";
        publishSnapshotLocked();
        return false;
    }

    if (screen) {
        screen->clear();
    }

    bool prevWriteInRom = memory->getWriteInRom();
    memory->setWriteInRom(true);
    memory->configureResetVectors(startAddress);
    memory->setWriteInRom(prevWriteInRom);
    preferredSoftResetVector = startAddress;
    cpu->hardReset();
    cpu->start();
    runRequested.store(true);
    publishSnapshotLocked();
    wakeCv.notify_all();
    return true;
}

bool EmulationController::saveMemoryRange(const std::string& path, quint16 startAddress, quint16 endAddress, bool binaryFormat, std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    const quint8* memPtr = memory->getMemoryPointer();
    std::ofstream file(path, binaryFormat ? std::ios::binary : std::ios::out);
    if (!file.is_open()) {
        error = "Error: unable to write file";
        return false;
    }

    if (binaryFormat) {
        for (quint16 a = startAddress; a <= endAddress; ++a) {
            quint8 b = memPtr[a];
            file.write(reinterpret_cast<char*>(&b), 1);
            if (a == 0xFFFF) break;
        }
    } else {
        for (quint16 a = startAddress; a <= endAddress; a += 16) {
            file << std::hex << std::uppercase << std::setfill('0')
                 << std::setw(4) << a << ":";
            int lineEnd = std::min((int)a + 16, (int)endAddress + 1);
            for (int i = a; i < lineEnd; ++i) {
                file << " " << std::setfill('0') << std::setw(2) << (int)memPtr[(quint16)i];
            }
            file << "\n";
            if (a + 16 < a) break;
        }
    }
    return true;
}

void EmulationController::setWriteInRom(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setWriteInRom(enabled);
    publishSnapshotLocked();
}

bool EmulationController::getWriteInRom() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return memory->getWriteInRom();
}

void EmulationController::setTerminalSpeed(int charsPerSecond)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setTerminalSpeed(charsPerSecond);
    publishSnapshotLocked();
}

bool EmulationController::reloadBasic(std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    bool prev = memory->getWriteInRom();
    memory->setWriteInRom(true);
    int result = memory->loadBasic();
    memory->setWriteInRom(prev);
    if (result != 0) {
        error = memory->getLastError();
    }
    publishSnapshotLocked();
    return result == 0;
}

bool EmulationController::reloadWozMonitor(std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    bool prev = memory->getWriteInRom();
    memory->setWriteInRom(true);
    int result = memory->loadWozMonitor();
    memory->setWriteInRom(prev);
    if (result != 0) {
        error = memory->getLastError();
    }
    publishSnapshotLocked();
    return result == 0;
}

bool EmulationController::reloadKrusader(std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    bool prev = memory->getWriteInRom();
    memory->setWriteInRom(true);
    int result = memory->loadKrusader();
    memory->setWriteInRom(prev);
    if (result != 0) {
        error = memory->getLastError();
    }
    publishSnapshotLocked();
    return result == 0;
}

bool EmulationController::reloadAciRom(std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    bool prev = memory->getWriteInRom();
    memory->setWriteInRom(true);
    int result = memory->loadAciRom();
    memory->setWriteInRom(prev);
    if (result != 0) {
        error = memory->getLastError();
    }
    publishSnapshotLocked();
    return result == 0;
}

void EmulationController::clearMemory()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->resetMemory();
    preferredSoftResetVector = kDefaultResetVector;
    memory->configureResetVectors(kDefaultResetVector);
    publishSnapshotLocked();
}

bool EmulationController::loadTape(const std::string& path, std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!memory->getCassetteDevice().loadTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publishSnapshotLocked();
        return false;
    }
    memory->getCassetteDevice().rewindTape();
    publishSnapshotLocked();
    return true;
}

bool EmulationController::saveTape(const std::string& path, std::string& error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!memory->getCassetteDevice().saveTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publishSnapshotLocked();
        return false;
    }
    publishSnapshotLocked();
    return true;
}

void EmulationController::rewindTape()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->getCassetteDevice().rewindTape();
    publishSnapshotLocked();
}

void EmulationController::ejectTape()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->getCassetteDevice().ejectTape();
    publishSnapshotLocked();
}

void EmulationController::clearTapeCapture()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->getCassetteDevice().clearRecordedTape();
    publishSnapshotLocked();
}

void EmulationController::setHardwareAccurateLiveAudio(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->getCassetteDevice().setHardwareAccurateLiveAudio(enabled);
    publishSnapshotLocked();
}

void EmulationController::setTMS9918Enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setTMS9918Enabled(enabled);
    publishSnapshotLocked();
}

bool EmulationController::isTMS9918Enabled() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return memory->isTMS9918Enabled();
}

void EmulationController::setSIDEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setSIDEnabled(enabled);
    publishSnapshotLocked();
}

bool EmulationController::isSIDEnabled() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return memory->isSIDEnabled();
}

void EmulationController::setMicroSDEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setMicroSDEnabled(enabled);
    publishSnapshotLocked();
}

bool EmulationController::isMicroSDEnabled() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return memory->isMicroSDEnabled();
}

void EmulationController::setWiFiModemEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setWiFiModemEnabled(enabled);
    publishSnapshotLocked();
}

bool EmulationController::isWiFiModemEnabled() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return memory->isWiFiModemEnabled();
}

void EmulationController::setTerminalCardEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setTerminalCardEnabled(enabled);
    publishSnapshotLocked();
}

bool EmulationController::isTerminalCardEnabled() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return memory->isTerminalCardEnabled();
}

void EmulationController::setA1IO_RTCEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    memory->setA1IO_RTCEnabled(enabled);
    publishSnapshotLocked();
}

bool EmulationController::isA1IO_RTCEnabled() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return memory->isA1IO_RTCEnabled();
}

void EmulationController::processQueuedKeysLocked()
{
    std::queue<char> localKeys;
    {
        std::lock_guard<std::mutex> lock(keyMutex);
        std::swap(localKeys, queuedKeys);
    }

    while (!localKeys.empty()) {
        memory->setKeyPressed(localKeys.front());
        localKeys.pop();
    }
}

void EmulationController::publishSnapshotLocked()
{
    EmulationSnapshot snapshot;
    const quint8* memPtr = memory->getMemoryPointer();
    std::copy(memPtr, memPtr + 0x10000, snapshot.memory.begin());
    snapshot.programCounter = cpu->getProgramCounter();
    snapshot.accumulator = cpu->getAccumulator();
    snapshot.xRegister = cpu->getXRegister();
    snapshot.yRegister = cpu->getYRegister();
    snapshot.stackPointer = cpu->getStackPointer();
    snapshot.statusRegister = cpu->getStatusRegister();
    snapshot.cpuRunning = runRequested.load();
    snapshot.keyReady = memory->isKeyReady();
    snapshot.lastKey = memory->getLastKey();
    snapshot.writeInRom = memory->getWriteInRom();
    snapshot.ramSizeKB = memory->getRamSizeKB();

    CassetteDevice& cassette = memory->getCassetteDevice();
    snapshot.cassetteLoadedTape = cassette.hasLoadedTape();
    snapshot.cassetteRecordedTape = cassette.hasRecordedTape();
    snapshot.cassettePlaybackActive = cassette.isPlaybackActive();
    snapshot.cassetteAudioAvailable = cassette.isAudioAvailable();
    snapshot.cassetteHardwareAccurateLiveAudio = cassette.isHardwareAccurateLiveAudio();
    snapshot.cassetteQueuedAudioSeconds = cassette.getQueuedAudioSeconds();
    snapshot.cassetteLoadedTransitionCount = cassette.getLoadedTransitionCount();
    snapshot.cassetteRecordedTransitionCount = cassette.getRecordedTransitionCount();
    snapshot.cassetteLoadedTapePath = cassette.getLoadedTapePath();

    memory->getTMS9918().copySnapshot(snapshot.tms9918);
    snapshot.sidEnabled = memory->isSIDEnabled();
    snapshot.microSDEnabled = memory->isMicroSDEnabled();
    snapshot.wifiModemEnabled = memory->isWiFiModemEnabled();
    if (snapshot.wifiModemEnabled) {
        memory->getWiFiModem().copySnapshot(snapshot.wifiModem);
    }
    snapshot.terminalCardEnabled = memory->isTerminalCardEnabled();
    if (snapshot.terminalCardEnabled) {
        memory->getTerminalCard().copySnapshot(snapshot.terminalCard);
    }
    snapshot.a1ioRtcEnabled = memory->isA1IO_RTCEnabled();
    if (snapshot.a1ioRtcEnabled) {
        memory->getA1IO_RTC().copySnapshot(snapshot.a1ioRtc);
    }

    std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
    latestSnapshot = std::move(snapshot);
}

void EmulationController::runEmulationSlice(double elapsedSeconds)
{
    const int cpf = executionSpeedCyclesPerFrame.load();
    if (cpf != cycleBudgetAnchorCpf) {
        emulationCycleBudget = 0.0;
        cycleBudgetAnchorCpf = cpf;
    }

    const double cyclesPerSecond = static_cast<double>(cpf) * kFramesPerSecond;
    emulationCycleBudget += cyclesPerSecond * elapsedSeconds;

    // Cap budget to 2 frames to prevent runaway accumulation (e.g. after a speed change).
    const double maxBudget = cyclesPerSecond / kFramesPerSecond * 2.0;
    if (emulationCycleBudget > maxBudget) emulationCycleBudget = maxBudget;

    int cyclesToRun = std::min(kMaxSliceCycles, static_cast<int>(emulationCycleBudget));
    if (cyclesToRun <= 0) {
#if !POM1_IS_WASM
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        memory->getCassetteDevice().setLiveAudioTimebaseHz(static_cast<uint32_t>(std::max(1.0, cyclesPerSecond)));
        processQueuedKeysLocked();
        // Re-vérifier sous le mutex : stopCpu()/step peut avoir eu lieu après le test du haut de boucle.
        // Sinon cpu->start() annule cpu->stop() et une tranche entière s'exécute entre deux F7.
        if (runRequested.load()) {
            emulationCycleBudget -= static_cast<double>(cyclesToRun);
            cpu->start();
            cpu->run(cyclesToRun);
        }
        publishSnapshotLocked();
    }

    // Terminal Card: consume pending reset/clear OUTSIDE stateMutex
    // to avoid deadlock (softReset acquires stateMutex internally)
    if (memory->isTerminalCardEnabled()) {
        if (memory->getTerminalCard().consumeResetPending()) {
            softReset();
        }
        if (memory->getTerminalCard().consumeClearScreenPending()) {
            if (screen) screen->clear();
        }
    }
}

void EmulationController::pumpEmulationMainThread(double deltaSeconds)
{
#if POM1_IS_WASM
    if (!runRequested.load()) {
        return;
    }

    double queuedAudioSeconds = 0.0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        queuedAudioSeconds = memory->getCassetteDevice().getQueuedAudioSeconds();
    }
    if (queuedAudioSeconds > kMaxLiveAudioLeadSeconds) {
        return;
    }

    const double elapsed =
        (deltaSeconds > 0.0 && deltaSeconds < 0.5) ? deltaSeconds : (1.0 / kFramesPerSecond);
    runEmulationSlice(elapsed);
#else
    (void)deltaSeconds;
#endif
}

#if !POM1_IS_WASM
void EmulationController::emulationLoop()
{
    using clock = std::chrono::steady_clock;
    auto lastTick = clock::now();
    emulationCycleBudget = 0.0;

    while (!terminateRequested.load()) {
        if (!runRequested.load()) {
            std::unique_lock<std::mutex> waitLock(wakeMutex);
            wakeCv.wait_for(waitLock, std::chrono::milliseconds(2));
            lastTick = clock::now();
            continue;
        }

        double queuedAudioSeconds = 0.0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            queuedAudioSeconds = memory->getCassetteDevice().getQueuedAudioSeconds();
        }
        if (queuedAudioSeconds > kMaxLiveAudioLeadSeconds) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lastTick = clock::now();
            continue;
        }

        const auto now = clock::now();
        const std::chrono::duration<double> elapsed = now - lastTick;
        lastTick = now;

        runEmulationSlice(elapsed.count());
    }
}
#else
void EmulationController::emulationLoop()
{
    /* WASM : jamais appelé — l’émulation est poussée par pumpEmulationMainThread(). */
}
#endif
