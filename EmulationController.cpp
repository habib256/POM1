#include "EmulationController.h"
#include "POM1Build.h"
#include "PR40Printer.h"
#include "RomLoader.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

constexpr double kFramesPerSecond = 60.0;
#if POM1_IS_WASM
// WASM: pumpEmulationMainThread is called once per frame from the main loop.
// At 60 fps, ~1.022727 MHz needs ~17 045 cycles/frame and ~2.045 MHz ~34 091 cycles/frame.
// The desktop cap of 12 000 throttles WASM to 720 KHz — too slow for SID tunes.
// Set the cap well above the 2 MHz frame budget so a single call consumes it all.
constexpr int kMaxSliceCycles = 50000;
// WASM: emulation and audio share the main thread — need more lead time
// to avoid queue starvation between frames.
constexpr double kMaxLiveAudioLeadSeconds = 0.15;
#else
// Upper bound on cycles executed per stateMutex acquisition. Smaller slices
// shorten the worst-case latency a UI-thread lock() can wait when the
// emulation thread is running hot (MAX speed). With page-level dirty
// tracking the per-slice publish is cheap (~µs), so 6 000 cycles stays
// well above the overhead floor while keeping MAX-mode UI fluid: at a
// typical ~120 MHz emulated throughput, each slice holds the mutex for
// ~50 µs, and the PriorityMutex yield below gives the UI a clean opening
// between slices.
constexpr int kMaxSliceCycles = 6000;
constexpr double kMaxLiveAudioLeadSeconds = 0.025;
#endif

} // namespace

EmulationController::EmulationController(Screen_ImGui* screenWidget)
    : screen(screenWidget)
{
    memory = std::make_unique<Memory>();
    cpu = std::make_unique<M6502>(memory.get());

    memory->setDisplayDevice(screen);

    {
        std::lock_guard<PriorityMutex> lock(stateMutex);
        memory->configureResetVectors(kDefaultResetVector);
        cpu->hardReset();
        cpu->start();
        publisher.publish(*memory, *cpu, runRequested.load());
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
    publisher.copyTo(out);
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
        std::lock_guard<PriorityMutex> lock(stateMutex);
        cpu->start();
        publisher.publish(*memory, *cpu, runRequested.load());
    }
    wakeCv.notify_all();
}

void EmulationController::stopCpu()
{
    runRequested.store(false);
    {
        std::lock_guard<PriorityMutex> lock(stateMutex);
        cpu->stop();
        publisher.publish(*memory, *cpu, runRequested.load());
    }
    wakeCv.notify_all();
}

void EmulationController::softReset()
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->configureResetVectors(preferredSoftResetVector);
    cpu->softReset();
    cpu->start();
    runRequested.store(true);
    publisher.publish(*memory, *cpu, runRequested.load());
    wakeCv.notify_all();
}

void EmulationController::hardReset()
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);

    // Explicitly unplug the A1-SID card across the hardReset window:
    // setSIDEnabled(false) removes the SID from the audio mixer's source
    // list and disables its bus handler. The memory reset below is then
    // free to wipe RAM and reload ROMs without any risk of the audio
    // thread observing the SID mid-reset (no race on the sample ring,
    // no ghost callback reading a chip in reset). setSIDEnabled(true)
    // at the end re-plugs the card exactly as if the user had just
    // slotted it — clean addSource, fresh bus handler enable.
    const bool sidWasPlugged = memory->isSIDEnabled();
    if (sidWasPlugged) memory->setSIDEnabled(false);

    memory->resetMemory();
    memory->initMemory();
    preferredSoftResetVector = kDefaultResetVector;
    memory->configureResetVectors(kDefaultResetVector);
    cpu->hardReset();
    cpu->start();
    runRequested.store(true);

    if (sidWasPlugged) memory->setSIDEnabled(true);

    if (screen) {
        screen->resetDisplay(); // garbage screen → auto-clear → welcome
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    wakeCv.notify_all();
}

void EmulationController::stepCpu()
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->step();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setCpuBrkTraceEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->setDebugBrkTrace(enabled);
}

bool EmulationController::isCpuBrkTraceEnabled() const
{
    // Read-only access to a bool; safe without the state mutex.
    return cpu->getDebugBrkTrace();
}

void EmulationController::dumpCpuPcTrace(const char* tag)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->dumpPcTrace(tag);
}

void EmulationController::queueKey(char key)
{
    keyboard.queueKey(key);
    wakeCv.notify_all();
}

void EmulationController::writeMemory(uint16_t address, uint8_t value)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->memWrite(address, value);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::loadBinaryToRam(const std::string& path, uint16_t address, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    int result = memory->loadBinary(path.c_str(), address);
    if (result != 0) {
        error = "Cannot load file";
        return false;
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::loadHexDump(const std::string& path, uint16_t& startAddress, std::string& error,
                                      int* bytesLoaded,
                                      std::vector<std::pair<uint16_t,uint16_t>>* zones)
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);

    uint16_t addr = 0;
    int result = memory->loadHexDump(path.c_str(), addr, bytesLoaded, zones);
    if (result != 0) {
        error = "Error: unable to load file";
        publisher.publish(*memory, *cpu, runRequested.load());
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
    publisher.publish(*memory, *cpu, runRequested.load());
    wakeCv.notify_all();
    return true;
}

bool EmulationController::loadBinary(const std::string& path, uint16_t startAddress, std::string& error, int* bytesLoaded)
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);

    int result = memory->loadBinary(path.c_str(), startAddress, bytesLoaded);
    if (result != 0) {
        error = "Error: unable to load file";
        publisher.publish(*memory, *cpu, runRequested.load());
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
    publisher.publish(*memory, *cpu, runRequested.load());
    wakeCv.notify_all();
    return true;
}

bool EmulationController::saveMemoryRange(const std::string& path, uint16_t startAddress, uint16_t endAddress, bool binaryFormat, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    const uint8_t* memPtr = memory->getMemoryPointer();
    std::ofstream file(path, binaryFormat ? std::ios::binary : std::ios::out);
    if (!file.is_open()) {
        error = "Error: unable to write file";
        return false;
    }

    if (binaryFormat) {
        for (uint16_t a = startAddress; a <= endAddress; ++a) {
            uint8_t b = memPtr[a];
            file.write(reinterpret_cast<char*>(&b), 1);
            if (a == 0xFFFF) break;
        }
    } else {
        for (uint16_t a = startAddress; a <= endAddress; a += 16) {
            file << std::hex << std::uppercase << std::setfill('0')
                 << std::setw(4) << a << ":";
            int lineEnd = std::min((int)a + 16, (int)endAddress + 1);
            for (int i = a; i < lineEnd; ++i) {
                file << " " << std::setfill('0') << std::setw(2) << (int)memPtr[(uint16_t)i];
            }
            file << "\n";
            if (a + 16 < a) break;
        }
    }
    return true;
}

bool EmulationController::saveSnapshot(const std::string& path, std::string& error) const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->saveSnapshot(path, error);
}

bool EmulationController::loadSnapshot(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->loadSnapshot(path, error)) return false;
    // Republish so the UI sees the new RAM/state immediately.
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

void EmulationController::setWriteInRom(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setWriteInRom(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::getWriteInRom() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getWriteInRom();
}

void EmulationController::setTerminalSpeed(int charsPerSecond)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTerminalSpeed(charsPerSecond);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setPresetRamKB(int kb)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setPresetRamKB(kb);
}

void EmulationController::setSiliconStrictMode(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setSiliconStrictMode(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isSiliconStrictMode() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isSiliconStrictMode();
}

int EmulationController::getOutOfRangeAccessCount() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getOutOfRangeAccessCount();
}

void EmulationController::setOutOfRangeStrictMode(bool enable)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setOutOfRangeStrictMode(enable);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isOutOfRangeStrictMode() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isOutOfRangeStrictMode();
}

bool EmulationController::reloadBasic(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadBasic(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

void EmulationController::unloadBasic()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->unloadBasic();
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::reloadApplesoftLite(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadApplesoftLite(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadApplesoftLiteCFFA1(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadApplesoftLiteCFFA1(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadApplesoftLiteSDCard(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadApplesoftLiteSDCard(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadWozMonitor(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadWozMonitor(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadKrusader(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadKrusader(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadAciRom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadAciRom(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

void EmulationController::clearMemory()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->resetMemory();
    preferredSoftResetVector = kDefaultResetVector;
    memory->configureResetVectors(kDefaultResetVector);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::loadTape(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->getCassetteDevice().loadTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publisher.publish(*memory, *cpu, runRequested.load());
        return false;
    }
    memory->getCassetteDevice().rewindTape();
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::loadProgramTape(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->getCassetteDevice().loadProgramTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publisher.publish(*memory, *cpu, runRequested.load());
        return false;
    }
    memory->getCassetteDevice().rewindTape();
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::saveTape(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->getCassetteDevice().saveTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publisher.publish(*memory, *cpu, runRequested.load());
        return false;
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

void EmulationController::rewindTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().rewindTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::playTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().playTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::stopTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().stopTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::pauseTape(bool paused)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().setPlaybackPaused(paused);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::seekTapeRelative(double deltaSeconds)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().seekRelativeSeconds(deltaSeconds);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::ejectTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().ejectTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::clearTapeCapture()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().clearRecordedTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setHardwareAccurateLiveAudio(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().setHardwareAccurateLiveAudio(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setCassetteVolume(float volume)
{
    // No stateMutex: CassetteDevice::setVolume() only writes to a
    // std::atomic<float> that the audio thread reads with relaxed memory
    // order. Skipping the mutex keeps the +/- buttons instant even when
    // the emulation thread is burning cycles at MAX speed.
    memory->getCassetteDevice().setVolume(volume);
}

void EmulationController::armCassetteRecord()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().armRecording();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setTMS9918Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTMS9918Enabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isTMS9918Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isTMS9918Enabled();
}

void EmulationController::setACIEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setACIEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isACIEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isACIEnabled();
}

void EmulationController::setSIDEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setSIDEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::activateCassetteAudioSource()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->activateCassetteAudioSource();
}

void EmulationController::deactivateCassetteAudioSource()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->deactivateCassetteAudioSource();
}

void EmulationController::setSIDSpecialEditionEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setSIDSpecialEditionEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isSIDSpecialEditionEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isSIDSpecialEditionEnabled();
}

void EmulationController::setSIDChipModel(pom1::SID::ChipModel m)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getSID().setChipModel(m);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isSIDEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isSIDEnabled();
}

void EmulationController::setMicroSDEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setMicroSDEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isMicroSDEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isMicroSDEnabled();
}

std::string EmulationController::getMicroSDRootPath() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getMicroSD().getSDCardPath();
}

void EmulationController::setCFFA1Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setCFFA1Enabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isCFFA1Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isCFFA1Enabled();
}

bool EmulationController::reloadCFFA1Rom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadCFFA1Rom(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadSDCardRom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadSDCardRom(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

void EmulationController::setJukeBoxEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isJukeBoxEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isJukeBoxEnabled();
}

void EmulationController::setJukeBoxJumper(JukeBox::Jumper jumper)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxJumper(jumper);
    publisher.publish(*memory, *cpu, runRequested.load());
}

JukeBox::Jumper EmulationController::getJukeBoxJumper() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBoxJumper();
}

void EmulationController::setJukeBoxChipMode(JukeBox::ChipMode mode)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxChipMode(mode);
    publisher.publish(*memory, *cpu, runRequested.load());
}

JukeBox::ChipMode EmulationController::getJukeBoxChipMode() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBoxChipMode();
}

void EmulationController::setJukeBoxWritable(bool writable)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxWritable(writable);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isJukeBoxWritable() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isJukeBoxWritable();
}

bool EmulationController::reloadJukeBoxRom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadJukeBoxRom(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

void EmulationController::setJukeBoxBankRegister(uint8_t value)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxBankRegister(value);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::copyJukeBoxPage(uint8_t fromPage, uint8_t toPage, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = memory->copyJukeBoxPage(fromPage, toPage, error);
    if (ok) publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::saveJukeBoxRom(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->saveJukeBoxRom(path, error);
}

void EmulationController::setCodeTankEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setCodeTankEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isCodeTankEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isCodeTankEnabled();
}

void EmulationController::setCodeTankJumper(CodeTank::Jumper jumper)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setCodeTankJumper(jumper);
    publisher.publish(*memory, *cpu, runRequested.load());
}

CodeTank::Jumper EmulationController::getCodeTankJumper() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getCodeTankJumper();
}

bool EmulationController::loadCodeTankRom(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    int rc = memory->loadCodeTankRom(path);
    if (rc != 0) {
        error = memory->getLastError();
        return false;
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

void EmulationController::setWiFiModemEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setWiFiModemEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isWiFiModemEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isWiFiModemEnabled();
}

void EmulationController::wifiModemDisconnect()
{
    // No stateMutex needed: WiFiModem::requestDisconnect() takes its own modemMutex.
    if (memory->isWiFiModemEnabled()) {
        memory->getWiFiModem().requestDisconnect();
    }
}

void EmulationController::wifiModemReset()
{
    // No stateMutex needed: WiFiModem::reset() takes its own modemMutex.
    memory->getWiFiModem().reset();
}

void EmulationController::setTerminalCardEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTerminalCardEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isTerminalCardEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isTerminalCardEnabled();
}

TerminalCard* EmulationController::getTerminalCardIfEnabled()
{
    // No stateMutex: the card itself owns its own atomics + mutex, and the
    // render thread must not contend with the long-held emulation lock.
    if (!memory) return nullptr;
    if (!memory->isTerminalCardEnabled()) return nullptr;
    return &memory->getTerminalCard();
}

void EmulationController::setPR40Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setPR40Enabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isPR40Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isPR40Enabled();
}

void EmulationController::setPR40SwitchMode(int mode)
{
    PR40Printer::SwitchMode m = PR40Printer::SwitchMode::Mixed;
    switch (mode) {
        case 0: m = PR40Printer::SwitchMode::Off;       break;
        case 1: m = PR40Printer::SwitchMode::Mixed;     break;
        case 2: m = PR40Printer::SwitchMode::PrintOnly; break;
        default: return;
    }
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getPR40().setMode(m);
    publisher.publish(*memory, *cpu, runRequested.load());
}

int EmulationController::getPR40SwitchMode() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return static_cast<int>(memory->getPR40().getMode());
}

bool EmulationController::savePR40PaperRoll(const std::string& path, std::string& error) const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getPR40().savePaperRoll(path, error);
}

void EmulationController::clearPR40Paper()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getPR40().tearOffPage();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setA1IO_RTCEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setA1IO_RTCEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isA1IO_RTCEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isA1IO_RTCEnabled();
}

void EmulationController::setGT6144Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGT6144Enabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isGT6144Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGT6144Enabled();
}

void EmulationController::setRtcOverrideTime(std::time_t target)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getA1IO_RTC().setOverrideTime(target);
}

void EmulationController::jumpTo(uint16_t address)
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool prevWriteInRom = memory->getWriteInRom();
    memory->setWriteInRom(true);
    memory->configureResetVectors(address);
    memory->setWriteInRom(prevWriteInRom);
    preferredSoftResetVector = address;
    cpu->hardReset();
    cpu->start();
    runRequested.store(true);
    publisher.publish(*memory, *cpu, runRequested.load());
    wakeCv.notify_all();
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
        std::lock_guard<PriorityMutex> lock(stateMutex);
        memory->getCassetteDevice().setLiveAudioTimebaseHz(static_cast<uint32_t>(std::max(1.0, cyclesPerSecond)));
        keyboard.drainTo(*memory);
        // Re-vérifier sous le mutex : stopCpu()/step peut avoir eu lieu après le test du haut de boucle.
        // Sinon cpu->start() annule cpu->stop() et une tranche entière s'exécute entre deux F7.
        if (runRequested.load()) {
            cpu->start();
            const int actualCycles = cpu->run(cyclesToRun);
            emulationCycleBudget -= static_cast<double>(actualCycles);
        }
        publisher.publish(*memory, *cpu, runRequested.load());
    }

    // Terminal Card: consume pending reset/clear OUTSIDE stateMutex
    // to avoid deadlock (softReset acquires stateMutex internally)
    if (memory->isTerminalCardEnabled()) {
        if (memory->getTerminalCard().consumeHardResetPending()) {
            hardReset();
        } else if (memory->getTerminalCard().consumeResetPending()) {
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
        std::lock_guard<PriorityMutex> lock(stateMutex);
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

        // Faster-than-2× speeds (MAX mode, driven by the UI or --cpu-max)
        // must bypass the live-audio-lead throttle: the live audio queue
        // is filled by CPU cycles and drained by the audio callback at
        // 44.1 kHz wallclock, so any emulation speed above real time
        // grows the queue faster than it drains and would otherwise park
        // the emulation thread in the 1 ms sleep below indefinitely —
        // that's why `--cpu-max` previously behaved like 1×. Instead we
        // drop live audio altogether in that regime (the user won't hear
        // useful sound at 60× speed anyway) and keep the CPU running
        // flat out.
        const int cpfSnapshot = executionSpeedCyclesPerFrame.load();
        const bool maxSpeed = cpfSnapshot > POM1_CPU_CYCLES_PER_FRAME_2X_60HZ;
        double queuedAudioSeconds = 0.0;
        {
            std::lock_guard<PriorityMutex> lock(stateMutex);
            auto& cass = memory->getCassetteDevice();
            if (maxSpeed) {
                cass.dropLiveAudio();
            } else {
                queuedAudioSeconds = cass.getQueuedAudioSeconds();
            }
        }
        if (!maxSpeed && queuedAudioSeconds > kMaxLiveAudioLeadSeconds) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lastTick = clock::now();
            continue;
        }

        const auto now = clock::now();
        const std::chrono::duration<double> elapsed = now - lastTick;
        lastTick = now;

        runEmulationSlice(elapsed.count());

        // In MAX speed the emulation thread is a near-100% duty-cycle holder
        // of stateMutex: once runEmulationSlice() releases it, the loop head
        // (audio-lead check) reacquires within nanoseconds. Most schedulers
        // favour re-granting the mutex to the releasing thread, starving any
        // UI-thread lock() that has been waiting. The yield hint lets the
        // scheduler run a waiter — it's only issued when PriorityMutex tells
        // us someone is actually queued, so low-contention workloads (normal
        // 1×/2× speeds or MAX with idle UI) pay nothing for this.
        if (stateMutex.hasWaiters()) {
            std::this_thread::yield();
        }
    }
}
#else
void EmulationController::emulationLoop()
{
    /* WASM : jamais appelé — l’émulation est poussée par pumpEmulationMainThread(). */
}
#endif
