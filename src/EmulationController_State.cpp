// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// EmulationController_State.cpp — memory images, snapshots, state rewind and ROM (re)loading
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
#include "RomLoader.h"
#include "Logger.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

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
    // Bytes at these addresses now belong to a different image.
    programGeneration_.fetch_add(1, std::memory_order_relaxed);
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
    // A different program now occupies these addresses (see programGeneration).
    programGeneration_.fetch_add(1, std::memory_order_relaxed);
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

std::vector<std::string> EmulationController::romFallbacksUsed() const
{
    // Copied under the state lock: the emulation thread may be re-loading ROMs
    // (a preset switch defers its card plug by 15 frames) while the UI asks.
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->romFallbacksUsed();
}

void EmulationController::publishRewindStatusLocked()
{
    const std::size_t n = rewindBuffer.frameCount();
    rewindFrameCount_.store(n);
    rewindStoredBytes_.store(rewindBuffer.storedBytes());
    rewindPos_.store(n ? n - 1 : 0);
}

void EmulationController::setRewindEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (enabled == rewindEnabled_.load()) return;
    rewindEnabled_.store(enabled);
    rewindPreviewing_.store(false);
    rewindCaptureAccum = 0.0;
    rewindGeneration_.fetch_add(1);
    std::lock_guard<pom1::RankedMutex<pom1::LockRank::Rewind>> rlock(rewindMutex);
    if (!enabled) {
        rewindBuffer.clear();
    } else {
        // Seed with the current state so the timeline isn't empty.
        rewindBuffer.capture(memory->saveSnapshotToBuffer(cpu.get()));
    }
    publishRewindStatusLocked();
}

void EmulationController::setRewindMemoryBudgetMB(int megabytes)
{
    if (megabytes < 1) megabytes = 1;
    // Eviction only drops leading segments: the head stays the head, so the
    // generation is untouched and a staged capture remains valid.
    std::lock_guard<pom1::RankedMutex<pom1::LockRank::Rewind>> rlock(rewindMutex);
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
    // These five atomics are updated as a group by the emulation thread but read
    // without a lock (deliberately — a lock-free UI read). A read can land mid
    // update and see frameCount/currentPos momentarily skewed; clamp so the
    // returned struct is always self-consistent (currentPos < frameCount, or
    // both 0) and the UI slider never renders an out-of-range thumb.
    if (s.frameCount == 0)             s.currentPos = 0;
    else if (s.currentPos >= s.frameCount) s.currentPos = s.frameCount - 1;
    return s;
}

void EmulationController::rewindRestoreFrame(std::size_t pos)
{
    // REQUIRES stateMutex held by caller.
    std::vector<uint8_t> blob;
    {
        std::lock_guard<pom1::RankedMutex<pom1::LockRank::Rewind>> rlock(rewindMutex);
        blob = rewindBuffer.reconstruct(pos);
    }
    if (blob.empty()) return;
    std::string err;
    if (memory->loadSnapshotFromBuffer(blob, err, cpu.get())) {
        // Restored memory: the resident program is whatever the snapshot held.
        programGeneration_.fetch_add(1, std::memory_order_relaxed);
        publisher.publish(*memory, *cpu, runRequested.load());
        rewindPos_.store(pos);
    }
}

void EmulationController::rewindSeekTo(std::size_t pos)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (pos >= rewindFrameCount_.load()) return;
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
        const std::size_t n = rewindFrameCount_.load();
        if (n == 0) return;
        if (pos >= n) pos = n - 1;
        rewindRestoreFrame(pos);
        // Discard the rewound-past future — new capture continues from here.
        // The generation bump retires any capture staged before this edit.
        rewindGeneration_.fetch_add(1);
        {
            std::lock_guard<pom1::RankedMutex<pom1::LockRank::Rewind>> rlock(rewindMutex);
            rewindBuffer.truncateAfter(pos);
            publishRewindStatusLocked();
        }
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
        const std::size_t n = rewindFrameCount_.load();
        if (n) rewindRestoreFrame(n - 1);
        rewindPreviewing_.store(false);
        rewindCaptureAccum = 0.0;
        cpu->start();
        runRequested.store(true);
    }
    wakeCv.notify_all();
}

bool EmulationController::reloadBasic(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadBasic(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadMsBasic(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadMsBasic(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadEhBasic(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadEhBasic(*memory, error);
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

bool EmulationController::reloadExtendedAciRom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadExtendedAciRom(*memory, error);
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
