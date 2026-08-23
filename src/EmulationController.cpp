// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// EmulationController.cpp — the CPU thread and everything that drives it:
// construction / teardown, run + stop + reset, single-step and step-over, the
// breakpoint / watchpoint / PC-trace controls, keyboard injection, direct
// memory + peripheral pokes, and the emulation slice / pacing loop itself.
//
// The other three TUs of this class (_State, _Machine, _Cards — see their
// headers) are pure code motion out of what used to be one 2143-line file.
// The pacing constants below stay here because the run loop is their only
// user; if a future extraction needs them, promote them to a private internal
// header rather than duplicating the values.
//
// Mutex order, class-wide: stateMutex > keyboard.keyMutex > publisher.snapshotMutex.

#include "EmulationController.h"
#include "POM1Build.h"
#include "PR40Printer.h"
#include "RomLoader.h"
#include "TMS9918.h"
#include "TelemetryPort.h"   // complete type for memory->getTelemetryPort()
#include "CassetteDevice.h"  // complete type for memory->getCassetteDevice()
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

EmulationController::EmulationController(DisplayDevice* screenWidget)
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

double EmulationController::getMeasuredCpuHz() const
{
    return measuredCpuHz_.load(std::memory_order_relaxed);
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
    if (address >= 0x2000 && address < 0x6000)
        memory->gen2ReseedLatchFromRam();   // make the edit visible while paused
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::writeMemoryBatch(const std::vector<std::pair<uint16_t, uint8_t>>& writes)
{
    if (writes.empty()) return;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    const bool wasTripped = memory->isWatchpointTripped();   // UI edits don't trip watchpoints
    bool touchedHgr = false;
    for (const auto& w : writes) {
        memory->memWrite(w.first, w.second);
        touchedHgr |= (w.first >= 0x2000 && w.first < 0x6000);
    }
    if (!wasTripped) memory->clearWatchTrip();
    if (touchedHgr) memory->gen2ReseedLatchFromRam();        // make edits visible while paused
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

    // Throughput measurement window. The wall-clock half is accumulated HERE,
    // ahead of every early return below: the slice sleeps whenever the budget
    // isn't full yet (the normal case at x1, where the host is far faster than
    // an Apple-1), and charging only the running slices would report the burst
    // rate -- tens of MHz -- as the sustained one.
    //
    // One path still escapes it: the emulation loop's live-audio-lead throttle
    // `continue`s without entering this function at all, so a long tape
    // playback under-charges time and reads slightly high. Bounded and benign
    // -- MAX speed skips that throttle entirely (so the one mode that DISPLAYS
    // the raw number is unaffected), and at x1/x2 an over-estimate can only
    // suppress the "(real ...)" warning, never invent one.
    measuredTimeAccum_ += elapsedSeconds;
    if (measuredTimeAccum_ >= kMeasuredWindowSec) {
        measuredCpuHz_.store(measuredCycleAccum_ / measuredTimeAccum_,
                             std::memory_order_relaxed);
        measuredCycleAccum_ = 0.0;
        measuredTimeAccum_ = 0.0;
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
    // State-rewind capture, staged under stateMutex and encoded after it
    // (see the rewindMutex note in the header). Empty = nothing to encode.
    std::vector<uint8_t> rewindBlob;
    uint64_t rewindBlobGeneration = 0;
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
                // Only real 6502 cycles count towards the measured rate: the
                // SID-preview branch below advances the chip, not the CPU.
                measuredCycleAccum_ += static_cast<double>(actualCycles);
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
                // Copy the pages here — the state must not move under us —
                // but encode the delta below, once stateMutex is released.
                rewindBlob = memory->saveSnapshotToBuffer(cpu.get());
                rewindBlobGeneration = rewindGeneration_.load();
            }
        }
#endif
    }

#if !POM1_IS_WASM
    // Delta-encode the staged rewind frame OUTSIDE stateMutex: this is the
    // expensive half of a capture (keyframe-vs-delta diff over the whole blob +
    // eviction), and nothing in it needs the machine state — only the buffer.
    // A timeline edit that landed in between (resume-here truncation, disable,
    // clear) bumps the generation; the blob then belongs to a future that no
    // longer exists and is dropped rather than appended behind the new head.
    if (!rewindBlob.empty()) {
        std::lock_guard<pom1::RankedMutex<pom1::LockRank::Rewind>> rlock(rewindMutex);
        if (rewindBlobGeneration == rewindGeneration_.load()) {
            rewindBuffer.capture(rewindBlob);
            publishRewindStatusLocked();
        }
    }
#endif

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
        // Same reasoning as the native loop's parked branch: the measurement
        // window lives inside runEmulationSlice, so zero it here or a paused
        // machine keeps displaying its last running rate.
        measuredCpuHz_.store(0.0, std::memory_order_relaxed);
        measuredCycleAccum_ = 0.0;
        measuredTimeAccum_ = 0.0;
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
            // Re-test under the lock via the predicate overload: a notify that
            // lands between the load() above and acquiring wakeMutex would
            // otherwise be lost (the notifier stores runRequested then notifies
            // without the lock). Re-checking here returns immediately in that
            // case; the 2 ms timeout still bounds the one remaining micro-window.
            wakeCv.wait_for(waitLock, std::chrono::milliseconds(2),
                            [this] { return runRequested.load() ||
                                            terminateRequested.load(); });
            lastTick = clock::now();
            // Parked: no cycles are being executed, so the measured rate is 0.
            // The averaging window only refreshes inside runEmulationSlice,
            // which this branch skips — without zeroing here the last running
            // value would stay on screen and a paused machine would read as
            // busy (and the status bar's "(real ...)" alarm would be gated on a
            // stale number). Written from the emulation thread, which owns the
            // accumulators, so the single-writer discipline holds.
            measuredCpuHz_.store(0.0, std::memory_order_relaxed);
            measuredCycleAccum_ = 0.0;
            measuredTimeAccum_ = 0.0;
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
