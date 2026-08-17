// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// EmulationController_Machine.cpp — machine-fidelity and diagnostic knobs (write-in-ROM, silicon-strict, NMOS decimal bug, RAM/VRAM power-on noise, RAM poison + write trap, the GEN2 randomisation family, DRAM refresh, TMS9918 drop diagnostics, out-of-range accounting)
//
// One of four translation units implementing the single EmulationController
// class. The class was a 2143-line god file: 207 method definitions covering
// the CPU thread, snapshots, silicon-fidelity knobs and a passthrough per
// expansion card, all in one place. Splitting it along those axes is pure code
// motion — no behaviour, no signature and no call site changed — and follows
// the pattern MainWindow_ImGui already uses (9 TUs behind one class).
//
// The mutex discipline is unchanged and applies to EVERY TU:
//     stateMutex > keyboard.keyMutex > publisher.snapshotMutex
// Anything touching `memory` or `cpu` takes `stateMutex` first; the pacing
// constants and the emulation thread itself stay in EmulationController.cpp.

#include "EmulationController.h"
#include "POM1Build.h"
#include "TMS9918.h"
#include "Logger.h"

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

pom1::Tms9918DropDiagnostics EmulationController::getTms9918DropDiagnostics() const
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
