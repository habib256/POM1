#include "EmulationController.h"
#include "POM1Build.h"
#include "PR40Printer.h"
#include "RomLoader.h"
#include "TMS9918.h"
#include "TelemetryPort.h"   // complete type for memory->getTelemetryPort()
#include "Logger.h"

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

// Lock-step safety valve: if a parked frame goes un-ACKed this long (dead /
// missing harness), auto-resume so the telemetry port can't wedge the emulator.
constexpr double kTelemetryStallTimeoutSec = 5.0;

} // namespace

EmulationController::EmulationController(Screen_ImGui* screenWidget)
    : screen(screenWidget)
{
    memory = std::make_unique<Memory>();
    cpu = std::make_unique<M6502>(memory.get());

    memory->setDisplayDevice(screen);
    memory->setCpuForIrq(cpu.get());

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
    // Signal the CPU lock-free *before* contending for stateMutex. If a slice
    // is already inside cpu->run() (holding the mutex), this lets its loop
    // guard observe running==0 and exit within one instruction instead of
    // burning the rest of its ~6000-cycle budget. Without this, a Stop/Step
    // click while free-running (the normal state right after the DevBench Run
    // pill) advanced the CPU by the slice remainder before the single step —
    // it looked like Step didn't single-step. cpu->stop() is re-issued under
    // the lock below so the final state is unambiguously stopped.
    cpu->stop();
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

void EmulationController::warmResetToMonitor()
{
    // Like softReset(), but force the RESET vector back to the Woz Monitor so a
    // program that redirected preferredSoftResetVector can't hijack the red key.
    // RAM is preserved (cpu->softReset, not hardReset) — exactly the physical
    // Apple-1 RESET behaviour.
    //
    // Only the in-memory RES vector is rewritten (cpu->softReset() reads it back
    // below) — we deliberately do NOT touch the persistent preferredSoftResetVector
    // member. That member is softReset()'s "run on reset" preference; clobbering it
    // here would leak the red key's force-to-monitor into every LATER softReset()
    // (e.g. the Terminal-Card telnet reset), sending it to the monitor instead of
    // re-running the loaded program until the next program load re-armed it.
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->configureResetVectors(kDefaultResetVector);
    cpu->softReset();
    cpu->start();
    runRequested.store(true);
    publisher.publish(*memory, *cpu, runRequested.load());
    wakeCv.notify_all();
}

void EmulationController::hardReset(bool animateBoot)
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
    // Flush any keystrokes still queued/buffered from a prior run so they can't be
    // read by the freshly reset monitor and corrupt the next cold-start (e.g. a
    // BASIC injection's leftover keys mangling the next E000R/6000R).
    keyboard.clear();
    memory->clearKeyboardInput();
    preferredSoftResetVector = kDefaultResetVector;
    memory->configureResetVectors(kDefaultResetVector);
    cpu->hardReset();
    cpu->start();
    runRequested.store(true);

    if (sidWasPlugged) memory->setSIDEnabled(true);

    if (screen) {
        if (animateBoot)
            screen->resetDisplay(); // garbage screen → auto-clear → welcome
        else
            screen->clear();        // DevBench: skip the ~3 s power-on scenario
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    wakeCv.notify_all();
}

void EmulationController::stepCpu()
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->clearWatchTrip();   // fresh slate so this step shows its own access
    cpu->step();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::stepOverCpu()
{
    stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);

    // Peek the opcode through memRead (NOT the raw mem[] mirror) so bus-only
    // executable ROMs like CFFA1 firmware ($9000-$AFDF) are read correctly.
    // Done BEFORE clearWatchTrip() so a read-watch on PC fired by this peek is
    // immediately wiped and never shows as a spurious watchpoint hit.
    const uint8_t opcode = memory->memRead(cpu->getProgramCounter());
    memory->clearWatchTrip();

    if (opcode != 0x20) {           // not JSR → ordinary single step
        cpu->step();
        publisher.publish(*memory, *cpu, runRequested.load());
        return;
    }

    // JSR: run until the return address (PC+3), borrowing the single hardware
    // breakpoint for the target and restoring the user's afterwards. Capped so
    // a non-returning / very long subroutine can't wedge the UI thread; we also
    // drain any queued keystrokes up front so stepping over a routine that
    // consumes an already-typed key can complete (a routine blocking on input
    // typed LATER still can't progress here — the keyboard isn't drained inside
    // the loop — so it gives up at the cap; use plain Step for those).
    keyboard.drainTo(*memory);
    const uint16_t ret = static_cast<uint16_t>(cpu->getProgramCounter() + 3);
    const bool     hadUserBp = cpu->hasBreakpoint();
    const uint16_t userBp    = cpu->getBreakpoint();

    cpu->setBreakpoint(ret);
    cpu->start();
    constexpr uint64_t kStepOverMaxCycles = 5'000'000;    // bound the UI-thread hold
    uint64_t done = 0;
    while (done < kStepOverMaxCycles) {
        const int actual = cpu->run(kMaxSliceCycles);
        done += static_cast<uint64_t>(actual > 0 ? actual : 0);
        if (cpu->isBreakpointTripped()) break;     // returned to `ret`
        if (memory->isWatchpointTripped()) break;  // watch fired inside the sub
        if (actual <= 0) break;                     // CPU jammed
    }

    // Restore the user breakpoint (setBreakpoint/clearBreakpoint both clear the
    // trip latch, so the temporary `ret` halt never shows as a user breakpoint).
    if (hadUserBp) cpu->setBreakpoint(userBp);
    else           cpu->clearBreakpoint();

    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::runCyclesSync(uint64_t cycles)
{
    stopCpu();                               // pause the async emulation thread
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->clearWatchTrip();                // fresh slate — a stale trip would make
                                             // run() break after one instruction
    cpu->start();                            // clear the CPU stop flag so run() executes
    uint64_t done = 0;
    while (done < cycles) {
        const int slice = static_cast<int>(
            std::min<uint64_t>(cycles - done, static_cast<uint64_t>(kMaxSliceCycles)));
        const int actual = cpu->run(slice);  // run() returns the actual cycle count
        if (actual <= 0) break;              // CPU jammed — avoid an infinite loop
        done += static_cast<uint64_t>(actual);
    }
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::runFromSync(uint16_t entry, uint64_t maxCycles)
{
    stopCpu();                               // pause the async emulation thread
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->clearWatchTrip();                // fresh slate — a stale trip would make
                                             // run() break after one instruction
    cpu->setProgramCounter(entry);           // jump into the resident ROM routine
    cpu->start();                            // clear the CPU stop flag so run() executes
    uint64_t done = 0;
    while (done < maxCycles) {
        const int slice = static_cast<int>(
            std::min<uint64_t>(maxCycles - done, static_cast<uint64_t>(kMaxSliceCycles)));
        const int actual = cpu->run(slice);
        if (actual <= 0) break;              // CPU jammed — avoid an infinite loop
        done += static_cast<uint64_t>(actual);
    }
    // RAM + zero page are left as the routine initialised them; the async loop
    // stays paused (stopCpu above) until the caller resumes it (e.g. loadHexDump).
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::runFromAsync(uint16_t entry)
{
    stopCpu();                               // settle the thread before re-pointing PC
    {
        std::lock_guard<PriorityMutex> lock(stateMutex);
        cpu->setProgramCounter(entry);       // jump into the resident ROM routine
        cpu->start();
        runRequested.store(true);
        publisher.publish(*memory, *cpu, runRequested.load());
    }
    wakeCv.notify_all();                     // wake the emulation thread to run live
}

void EmulationController::setCpuBrkTraceEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->setDebugBrkTrace(enabled);
}

bool EmulationController::isCpuBrkTraceEnabled() const
{
    // The emulation thread mutates these CPU fields (breakpointTripped inside
    // cpu->run(), the rest under stateMutex), so a lock-free read here is a
    // formal data race. Take stateMutex, matching the setters below.
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return cpu->getDebugBrkTrace();
}

void EmulationController::dumpCpuPcTrace(const char* tag)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->dumpPcTrace(tag);
}

void EmulationController::setCpuBreakpoint(uint16_t address)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->setBreakpoint(address);
}

void EmulationController::clearCpuBreakpoint()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->clearBreakpoint();
}

bool EmulationController::hasCpuBreakpoint() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return cpu->hasBreakpoint();
}

uint16_t EmulationController::getCpuBreakpoint() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return cpu->getBreakpoint();
}

bool EmulationController::isCpuBreakpointTripped() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return cpu->isBreakpointTripped();
}

void EmulationController::setCpuWatchpoint(uint16_t address, bool onRead, bool onWrite)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setWatchpoint(address, onRead, onWrite);
}

void EmulationController::clearCpuWatchpoint(uint16_t address)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->clearWatchpoint(address);
}

void EmulationController::clearAllCpuWatchpoints()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->clearAllWatchpoints();
}

int EmulationController::cpuWatchpointCount() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->watchpointCount();
}

uint8_t EmulationController::cpuWatchpointFlags(uint16_t address) const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->watchpointFlags(address);
}

std::vector<std::pair<uint16_t, uint8_t>>
EmulationController::listCpuWatchpoints(int maxEntries) const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->listWatchpoints(maxEntries);
}

bool EmulationController::isCpuWatchpointTripped() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isWatchpointTripped();
}

uint16_t EmulationController::getCpuWatchAddress() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->watchHit().address;
}

bool EmulationController::getCpuWatchIsWrite() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->watchHit().write;
}

void EmulationController::queueKey(char key)
{
    keyboard.queueKey(key);
    wakeCv.notify_all();
}

void EmulationController::deliverQueuedKeys()
{
    // stateMutex serialises against cpu->run(): drainTo() writes $D010, which the
    // CPU reads. runCyclesSync pauses the async thread, so nothing else drains the
    // queue on the headless path — this is the one place it reaches Memory there.
    std::lock_guard<PriorityMutex> lock(stateMutex);
    keyboard.drainTo(*memory);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::hasPendingInjectedInput()
{
    // stateMutex serialises against the emulation thread's drainTo()/cpu->run(),
    // both of which mutate the keyboard/Memory key state under the same lock — so
    // this observes a consistent snapshot (no mid-drain false-empty window). Mutex
    // order stateMutex > keyMutex is honoured (hasQueuedKeys takes keyMutex).
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return keyboard.hasQueuedKeys() || memory->hasBufferedInput();
}

void EmulationController::writeMemory(uint16_t address, uint8_t value)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    // A UI memory edit must be invisible to watchpoints — restore the prior
    // trip state so a write-watch on `address` doesn't raise a false banner.
    const bool wasTripped = memory->isWatchpointTripped();
    memory->memWrite(address, value);
    if (!wasTripped) memory->clearWatchTrip();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::writeMemoryBatch(const std::vector<std::pair<uint16_t, uint8_t>>& writes)
{
    if (writes.empty()) return;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    const bool wasTripped = memory->isWatchpointTripped();   // UI edits don't trip watchpoints
    for (const auto& w : writes) memory->memWrite(w.first, w.second);
    if (!wasTripped) memory->clearWatchTrip();
    publisher.publish(*memory, *cpu, runRequested.load());   // one publish for the whole batch
}

void EmulationController::writeTms9918VramBatch(const std::vector<std::pair<uint16_t, uint8_t>>& writes)
{
    if (writes.empty()) return;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    TMS9918& tms = memory->getTMS9918();
    for (const auto& w : writes) tms.editorPokeVram(w.first, w.second);
    tms.editorRebuildFramebuffer();
    publisher.publish(*memory, *cpu, runRequested.load());   // one publish for the whole batch
}

void EmulationController::applyTms9918Registers(const uint8_t regs[8])
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    TMS9918& tms = memory->getTMS9918();
    for (int i = 0; i < 8; ++i) tms.editorSetRegister(static_cast<uint8_t>(i), regs[i]);
    tms.editorRebuildFramebuffer();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::pokeSidRegisters(const std::vector<std::pair<uint8_t, uint8_t>>& writes)
{
    if (writes.empty()) return;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    pom1::SID& sid = memory->getSID();
    for (const auto& w : writes) sid.writeRegister(w.first, w.second);
}

void EmulationController::previewBeepSfx(const std::vector<std::pair<uint32_t, bool>>& pulses)
{
    if (pulses.empty()) return;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    // The cassette AudioSource is only on the mixer when a tape is active, so a
    // freshly-opened editor's preview would queue segments nobody drains ->
    // silence. Ensure it's mixed (idempotent; harmless — it outputs 0 when the
    // pulse queue is empty). memory->… directly: activateCassetteAudioSource() on
    // the controller re-locks stateMutex (which we already hold) and would
    // deadlock the non-recursive PriorityMutex.
    memory->activateCassetteAudioSource();
    memory->getCassetteDevice().previewBeep(pulses);
}

void EmulationController::stopBeepPreview()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().stopPreviewBeep();
}

bool EmulationController::ejectAudioStreamTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    CassetteDevice& cd = memory->getCassetteDevice();
    if (cd.getDeckMode() != CassetteDevice::DeckMode::AudioStream) return false;
    cd.ejectTape();
    return true;
}

void EmulationController::readTms9918Vram(uint8_t* out16k)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    const uint8_t* v = memory->getTMS9918().vramData();
    std::copy(v, v + TMS9918::kVramSize, out16k);
}

void EmulationController::readTms9918Framebuffer(uint32_t* out)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getTMS9918().copyActiveFramebuffer(out);
}

bool EmulationController::loadBinaryToRam(const std::string& path, uint16_t address, std::string& error,
                                          bool pauseCpu)
{
    // pauseCpu: DevBench wants the CPU stopped (and left stopped) before its
    // load+run; HGR Paint wants it to keep running so a mid-session image load
    // doesn't freeze the emulator. Either way the load happens under stateMutex,
    // so it's race-free against a running slice.
    if (pauseCpu) stopCpu();
    std::lock_guard<PriorityMutex> lock(stateMutex);

    int result = memory->loadBinary(path.c_str(), address);
    if (result != 0) {
        error = std::string("Cannot open: ") + path;
        publisher.publish(*memory, *cpu, runRequested.load());
        return false;
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::loadInterpreterRom(const std::string& path, uint16_t address, std::string& error)
{
    // No stopCpu()/reset: drop the image into RAM under the state lock while the
    // CPU keeps running (the BASIC injector relies on the WOZ Monitor staying live
    // to process the cold-start command). Lift write-protect like the ROM reloaders.
    std::lock_guard<PriorityMutex> lock(stateMutex);
    const bool prev = memory->getWriteInRom();
    memory->setWriteInRom(true);
    const int result = memory->loadBinary(path.c_str(), address);
    memory->setWriteInRom(prev);
    publisher.publish(*memory, *cpu, runRequested.load());
    if (result != 0) { error = std::string("Cannot open: ") + path; return false; }
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
        // Use a 32-bit counter so the `a += 16` step can never wrap the 16-bit
        // address space: with a uint16_t counter, endAddress >= 0xFFF0 makes
        // a==0xFFF0 step to 0x10000 -> wraps to 0, a <= endAddress stays true
        // forever (infinite loop + unbounded file). The old `if (a + 16 < a)`
        // guard was dead code (a + 16 promotes to int and never wraps).
        for (uint32_t a = startAddress; a <= endAddress; a += 16) {
            file << std::hex << std::uppercase << std::setfill('0')
                 << std::setw(4) << a << ":";
            uint32_t lineEnd = std::min(a + 16, (uint32_t)endAddress + 1);
            for (uint32_t i = a; i < lineEnd; ++i) {
                file << " " << std::setfill('0') << std::setw(2) << (int)memPtr[(uint16_t)i];
            }
            file << "\n";
        }
    }
    return true;
}

bool EmulationController::saveSnapshot(const std::string& path, std::string& error) const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->saveSnapshot(path, error, cpu.get());
}

bool EmulationController::loadSnapshot(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->loadSnapshot(path, error, cpu.get())) return false;
    // Republish so the UI sees the new RAM/state immediately.
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

// ── State rewind ──────────────────────────────────────────────────────────

void EmulationController::setRewindEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (enabled == rewindEnabled_.load()) return;
    rewindEnabled_.store(enabled);
    rewindPreviewing_.store(false);
    rewindCaptureAccum = 0.0;
    if (!enabled) {
        rewindBuffer.clear();
        rewindFrameCount_.store(0);
        rewindStoredBytes_.store(0);
        rewindPos_.store(0);
    } else {
        // Seed with the current state so the timeline isn't empty.
        rewindBuffer.capture(memory->saveSnapshotToBuffer(cpu.get()));
        const std::size_t n = rewindBuffer.frameCount();
        rewindFrameCount_.store(n);
        rewindStoredBytes_.store(rewindBuffer.storedBytes());
        rewindPos_.store(n ? n - 1 : 0);
    }
}

void EmulationController::setRewindMemoryBudgetMB(int megabytes)
{
    if (megabytes < 1) megabytes = 1;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    rewindBuffer.setMemoryBudget(static_cast<std::size_t>(megabytes) * 1024u * 1024u);
    const std::size_t n = rewindBuffer.frameCount();
    rewindFrameCount_.store(n);
    rewindStoredBytes_.store(rewindBuffer.storedBytes());
    if (n && rewindPos_.load() >= n) rewindPos_.store(n - 1);
}

EmulationController::RewindStatus EmulationController::getRewindStatus() const
{
    RewindStatus s;
    s.enabled     = rewindEnabled_.load();
    s.previewing  = rewindPreviewing_.load();
    s.frameCount  = rewindFrameCount_.load();
    s.currentPos  = rewindPos_.load();
    s.storedBytes = rewindStoredBytes_.load();
    return s;
}

void EmulationController::rewindRestoreFrame(std::size_t pos)
{
    // REQUIRES stateMutex held by caller.
    std::vector<uint8_t> blob = rewindBuffer.reconstruct(pos);
    if (blob.empty()) return;
    std::string err;
    if (memory->loadSnapshotFromBuffer(blob, err, cpu.get())) {
        publisher.publish(*memory, *cpu, runRequested.load());
        rewindPos_.store(pos);
    }
}

void EmulationController::rewindSeekTo(std::size_t pos)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (pos >= rewindBuffer.frameCount()) return;
    // Pause on the previewed frame; capture stays suppressed while previewing.
    runRequested.store(false);
    cpu->stop();
    rewindPreviewing_.store(true);
    rewindRestoreFrame(pos);
}

void EmulationController::rewindResumeHere(std::size_t pos)
{
    {
        std::lock_guard<PriorityMutex> lock(stateMutex);
        const std::size_t n = rewindBuffer.frameCount();
        if (n == 0) return;
        if (pos >= n) pos = n - 1;
        rewindRestoreFrame(pos);
        // Discard the rewound-past future — new capture continues from here.
        rewindBuffer.truncateAfter(pos);
        const std::size_t m = rewindBuffer.frameCount();
        rewindFrameCount_.store(m);
        rewindStoredBytes_.store(rewindBuffer.storedBytes());
        rewindPos_.store(m ? m - 1 : 0);
        rewindPreviewing_.store(false);
        rewindCaptureAccum = 0.0;
        cpu->start();
        runRequested.store(true);
    }
    wakeCv.notify_all();
}

void EmulationController::rewindResumeLive()
{
    {
        std::lock_guard<PriorityMutex> lock(stateMutex);
        const std::size_t n = rewindBuffer.frameCount();
        if (n) rewindRestoreFrame(n - 1);
        rewindPreviewing_.store(false);
        rewindCaptureAccum = 0.0;
        cpu->start();
        runRequested.store(true);
    }
    wakeCv.notify_all();
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

void EmulationController::setCpuDecimalBugNMOS(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->setDecimalBugNMOS(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isCpuDecimalBugNMOS() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return cpu->isDecimalBugNMOS();
}

void EmulationController::setVramNoiseOnReset(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setVramNoiseOnReset(enabled);
}

bool EmulationController::isVramNoiseOnReset() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isVramNoiseOnReset();
}

void EmulationController::setTmsFrameFlagHostile(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTmsFrameFlagHostile(enabled);
}

bool EmulationController::isTmsFrameFlagHostile() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isTmsFrameFlagHostile();
}

void EmulationController::setRamPoison(bool enabled, uint8_t value)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setRamPoison(enabled, value);
}

void EmulationController::setRamWriteTrap(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setRamWriteTrap(enabled);
}

void EmulationController::setGen2RandomPowerOn(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2RandomPowerOn(enabled);
}

bool EmulationController::isGen2RandomPowerOn() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGen2RandomPowerOn();
}

void EmulationController::setGen2RandomLatch(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2RandomLatch(enabled);
}

void EmulationController::setGen2RandomFloatingBus(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2RandomFloatingBus(enabled);
}

void EmulationController::setGen2RandomScannerPhase(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2RandomScannerPhase(enabled);
}

void EmulationController::setGen2RandomDramNoise(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2RandomDramNoise(enabled);
}

bool EmulationController::isGen2RandomLatch() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGen2RandomLatch();
}

bool EmulationController::isGen2RandomFloatingBus() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGen2RandomFloatingBus();
}

bool EmulationController::isGen2RandomScannerPhase() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGen2RandomScannerPhase();
}

bool EmulationController::isGen2RandomDramNoise() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGen2RandomDramNoise();
}

Gen2VideoScanner::DisplayState EmulationController::getGen2DisplayState() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->gen2DisplayState();
}

void EmulationController::setGen2DisplayMode(bool grMode, bool page2)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2DisplayMode(grMode, page2);
    publisher.publish(*memory, *cpu, runRequested.load());
}

uint64_t EmulationController::getGen2ScannerCycle() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->peekGen2VideoCycle();
}

uint64_t EmulationController::getGen2CyclesPerFrame() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    // 65 cycles/line × (262 @ 60 Hz / 312 @ 50 Hz).
    return memory->isGen2FiftyHz()
        ? Gen2VideoScanner::kCyclesPerLine * Gen2VideoScanner::kLinesPerFrame50Hz
        : Gen2VideoScanner::kCyclesPerLine * Gen2VideoScanner::kLinesPerFrame;
}

bool EmulationController::isGen2InBlanking() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    const uint64_t fc = memory->peekGen2VideoCycle();
    const uint64_t line = fc / Gen2VideoScanner::kCyclesPerLine;
    const uint64_t hcnt = fc % Gen2VideoScanner::kCyclesPerLine;
    return Gen2VideoScanner::hst0State(static_cast<int>(line),
                                       static_cast<int>(hcnt)) != 0;
}

void EmulationController::setSystemRamNoiseOnReset(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setSystemRamNoiseOnReset(enabled);
}

bool EmulationController::isSystemRamNoiseOnReset() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isSystemRamNoiseOnReset();
}

void EmulationController::setJukeBoxEepromWriteCycleCpu(int cycles)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getJukeBox().setEepromWriteCycleCpu(cycles);
}

int EmulationController::getJukeBoxEepromWriteCycleCpu() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBox().getEepromWriteCycleCpu();
}

uint64_t EmulationController::getJukeBoxEepromWritesTotal() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBox().getEepromWritesTotal();
}

uint64_t EmulationController::getJukeBoxEepromWritesDropped() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBox().getEepromWritesDropped();
}

bool EmulationController::isJukeBoxEepromWriteBusy() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBox().isEepromWriteBusy();
}

int EmulationController::getJukeBoxEepromWriteBusyCycles() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBox().getEepromWriteBusyCycles();
}

void EmulationController::resetJukeBoxEepromCounters()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getJukeBox().resetEepromCounters();
}

void EmulationController::setDramRefreshEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->setDramRefreshEnabled(enabled);
    // NOTE: the screen "faint dots" crosstalk artefact is no longer mirrored
    // from this toggle — it is now shown by default in all modes (silicon
    // strict or not), see Screen_ImGui::dramRefreshDotsEnabled. This toggle
    // only controls the CPU refresh stall.
}

bool EmulationController::isDramRefreshEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return cpu->isDramRefreshEnabled();
}

uint64_t EmulationController::getDramRefreshStallCount() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return cpu->getDramRefreshStallCount();
}

void EmulationController::resetDramRefreshStallCount()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    cpu->resetDramRefreshStallCount();
}

uint64_t EmulationController::tms9918DropCount() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getTMS9918().droppedWriteCount();
}

void EmulationController::resetTms9918DropCount()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getTMS9918().resetDroppedWriteCount();
}

void EmulationController::dumpTms9918DropDiagnostics(std::FILE* out, int topN) const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getTMS9918().dumpDropDiagnostics(out ? out : stderr, topN);
}

TMS9918::DropDiagnostics EmulationController::getTms9918DropDiagnostics() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getTMS9918().dropDiagnostics();
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

void EmulationController::setHgrFramebufferAttached(bool attached)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setHgrFramebufferAttached(attached);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isHgrFramebufferAttached() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isHgrFramebufferAttached();
}

void EmulationController::setGen2FiftyHz(bool fiftyHz)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2FiftyHz(fiftyHz);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isGen2FiftyHz() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGen2FiftyHz();
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

void EmulationController::setIECCardEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setIECCardEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isIECCardEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isIECCardEnabled();
}

bool EmulationController::mountIECDisk(const std::string& path)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getIECCard().mountDisk(path);
}

void EmulationController::unmountIECDisk()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getIECCard().unmount();
}

EmulationController::IECCardUIState EmulationController::getIECCardUIState() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    IECCardUIState s;
    if (!memory->isIECCardEnabled()) return s;
    const auto& iec = memory->getIECCard();
    const auto& disk = iec.drive().image();
    s.hasDisk = iec.hasDisk();
    if (s.hasDisk) {
        s.diskPath = iec.diskPath();
        s.label = disk.labelAscii();
        s.id = disk.idAscii();
        s.blocksFree = disk.blocksFree();
        s.totalBlocks = disk.totalBlocks();
        for (const auto& e : disk.directory("*")) {
            if (e.type == 0) continue;
            IECCardUIState::Entry ue;
            for (uint8_t b : e.name) {
                if (b >= 0x20 && b <= 0x7E) ue.name += static_cast<char>(b);
                else if (b >= 0xC1 && b <= 0xDA) ue.name += static_cast<char>(b - 0x80);
                else ue.name += '?';
            }
            ue.blocks = e.blocks;
            ue.type = e.type;
            s.directory.push_back(std::move(ue));
        }
    }
    return s;
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

bool EmulationController::loadCodeTankRomBuffer(const std::vector<uint8_t>& data,
                                                const std::string& label, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    int rc = memory->loadCodeTankRomBuffer(data, label);
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

void EmulationController::setTelemetryEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTelemetryEnabled(enabled);
}

void EmulationController::setTelemetryListenPort(uint16_t port)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getTelemetryPort().setListenPort(port);
}

void EmulationController::setTelemetryLogFile(const std::string& path)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getTelemetryPort().setLogFile(path);
}

void EmulationController::telemetryInject(const uint8_t* data, std::size_t len)
{
    if (!data || len == 0) return;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->isTelemetryEnabled()) return;
    memory->getTelemetryPort().injectInbound(data, len);
}

void EmulationController::telemetryReleaseFrame()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->isTelemetryEnabled()) return;
    // Same effect as the harness ACK: drop the park gate so the slice loop
    // resumes the CPU until the next end-frame re-arms it (if lock-step is on).
    memory->getTelemetryPort().clearAwaitingAck();
}

void EmulationController::setTelemetryLockstep(bool on)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->isTelemetryEnabled()) return;
    auto& tp = memory->getTelemetryPort();
    tp.setLockstep(on);
    if (!on) tp.clearAwaitingAck();   // disarm → release any current park (resume)
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

    bool telemetryStalled = false;
    // Terminal Card pending reset/clear flags, captured under stateMutex below and
    // acted on after the lock is released (hardReset/softReset re-acquire stateMutex,
    // so they must run lock-free to avoid self-deadlock on the non-recursive mutex).
    bool termHardReset = false;
    bool termSoftReset = false;
    bool termClearScreen = false;
    {
        std::lock_guard<PriorityMutex> lock(stateMutex);
        memory->getCassetteDevice().setLiveAudioTimebaseHz(static_cast<uint32_t>(std::max(1.0, cyclesPerSecond)));
        keyboard.drainTo(*memory);

        // Lock-step: the CPU is parked at an end-frame marker until the harness
        // ACKs. Don't advance it — pump the telemetry socket so the ACK can
        // arrive (the normal poll runs inside cpu->run, which we skip here),
        // with a wall-clock timeout so a dead harness can't wedge the emulator.
        telemetryStalled = memory->isTelemetryEnabled()
                        && memory->getTelemetryPort().isAwaitingAck();
        if (telemetryStalled) {
            memory->getTelemetryPort().serviceStall();
            telemetryStallSeconds += elapsedSeconds;
            // A deliberate UI "Pause" holds indefinitely — only a harness-waiting
            // stall (game-armed lock-step, no/dead harness) trips the watchdog.
            if (telemetryStallSeconds > kTelemetryStallTimeoutSec
                && !memory->getTelemetryPort().isUserHeld()) {
                memory->getTelemetryPort().clearAwaitingAck();
                pom1::log().warn("Telemetry", "lock-step ACK timeout — auto-resuming");
                telemetryStallSeconds = 0.0;
            }
        }
        else {
            // Not stalled — clear the accumulator unconditionally. Doing this
            // only when runRequested was true left a stale value behind if the
            // harness ACKed while the CPU was stopped, which then tripped the
            // stall timeout early on the next lock-step park.
            telemetryStallSeconds = 0.0;
            // Re-vérifier sous le mutex : stopCpu()/step peut avoir eu lieu après le test du haut de boucle.
            // Sinon cpu->start() annule cpu->stop() et une tranche entière s'exécute entre deux F7.
            if (runRequested.load()) {
                // Clear last slice's watch latch so detection re-arms; a trip
                // set while parked (or by a UI memory edit) won't re-park us.
                memory->clearWatchTrip();
                cpu->start();
                const int actualCycles = cpu->run(cyclesToRun);
                emulationCycleBudget -= static_cast<double>(actualCycles);
                // A PC-matched breakpoint or a memory watchpoint halts the CPU
                // mid-slice (run() exits with the trip latched). Park the
                // emulation thread so the halt is *sticky*: without this,
                // runRequested stays true, the next slice's cpu->start() clears
                // the breakpoint trip and run() re-fires immediately — a
                // busy-spin that spams the log and never lets the UI show the
                // stopped state. Both stay armed; the UI resumes via Continue /
                // Resume (breakpoint: step past then run; watchpoint: just run,
                // since the access already executed and the latch is cleared
                // above on the next slice).
                if (cpu->isBreakpointTripped() || memory->isWatchpointTripped()) {
                    runRequested.store(false);
                }
            }
            else if (sidLivePreview_.load(std::memory_order_relaxed)
                     && (memory->isSIDEnabled() || memory->isSIDSpecialEditionEnabled())) {
                // CPU parked but the SID tracker is open: keep clocking the SID so
                // a poked preview note actually fills the audio ring (cpu->run,
                // which normally drives sid->advanceCycles, isn't running). Clock
                // the chip directly — advancing all peripherals here would fire
                // unrelated cycle side-effects. Producer stays single-threaded
                // (this slice runs on the emulation thread; pokeSidRegisters only
                // writes registers, never clocks).
                memory->getSID().advanceCycles(cyclesToRun);
                emulationCycleBudget -= static_cast<double>(cyclesToRun);
            }
        }

        // Terminal Card: read/clear pending reset/clear flags while holding
        // stateMutex (the enable bool + consume state are mutated by the UI /
        // TCP threads under the same lock). Act on the captured locals after
        // the lock releases — hardReset/softReset re-acquire stateMutex.
        if (memory->isTerminalCardEnabled()) {
            termHardReset = memory->getTerminalCard().consumeHardResetPending();
            if (!termHardReset) {
                termSoftReset = memory->getTerminalCard().consumeResetPending();
            }
            termClearScreen = memory->getTerminalCard().consumeClearScreenPending();
        }

        publisher.publish(*memory, *cpu, runRequested.load());

        // State-rewind capture: a few snapshots per second while the CPU is
        // actually running and we're not parked on a rewound preview frame.
        // Desktop only — the single-threaded WASM build can't afford the
        // periodic full-state capture on its one main-loop thread, so rewind is
        // disabled there (no UI entry points are shown either).
#if !POM1_IS_WASM
        if (rewindEnabled_.load() && runRequested.load() && !rewindPreviewing_.load() && !telemetryStalled) {
            rewindCaptureAccum += elapsedSeconds;
            if (rewindCaptureAccum >= kRewindCaptureIntervalSec) {
                rewindCaptureAccum = 0.0;
                rewindBuffer.capture(memory->saveSnapshotToBuffer(cpu.get()));
                const std::size_t n = rewindBuffer.frameCount();
                rewindFrameCount_.store(n);
                rewindStoredBytes_.store(rewindBuffer.storedBytes());
                rewindPos_.store(n ? n - 1 : 0);
            }
        }
#endif
    }

    // Park on the lock-step ACK without busy-spinning (this slice did no CPU
    // work). 1 ms keeps ACK latency low without pegging a core.
    if (telemetryStalled) {
#if !POM1_IS_WASM
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }

    // Terminal Card: act on the flags captured under stateMutex above, OUTSIDE
    // the lock (hardReset/softReset re-acquire stateMutex internally, so calling
    // them here avoids self-deadlock on the non-recursive PriorityMutex).
    if (termHardReset) {
        hardReset();
    } else if (termSoftReset) {
        softReset();
    }
    if (termClearScreen && screen) {
        screen->clear();
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
