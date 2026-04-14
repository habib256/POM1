// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "SnapshotPublisher.h"

#include <cstring>

#include "CassetteDevice.h"
#include "M6502.h"
#include "Memory.h"

void SnapshotPublisher::publish(Memory& mem, const M6502& cpu, bool cpuRunning)
{
    // Publish directly into latestSnapshot: its memory vector is already sized
    // to 64 KB, so a std::memcpy into the existing allocation avoids the
    // alloc-and-free of a fresh stack-constructed EmulationSnapshot every frame.
    std::lock_guard<std::mutex> lock(snapshotMutex);
    EmulationSnapshot& snapshot = latestSnapshot;

    const quint8* memPtr = mem.getMemoryPointer();
    std::memcpy(snapshot.memory.data(), memPtr, 0x10000);
    snapshot.programCounter = cpu.getProgramCounter();
    snapshot.accumulator    = cpu.getAccumulator();
    snapshot.xRegister      = cpu.getXRegister();
    snapshot.yRegister      = cpu.getYRegister();
    snapshot.stackPointer   = cpu.getStackPointer();
    snapshot.statusRegister = cpu.getStatusRegister();
    snapshot.cpuRunning     = cpuRunning;
    snapshot.keyReady       = mem.isKeyReady();
    snapshot.lastKey        = mem.getLastKey();
    snapshot.writeInRom     = mem.getWriteInRom();
    snapshot.ramSizeKB      = mem.getRamSizeKB();
    snapshot.oorStrictMode  = mem.isOutOfRangeStrictMode();

    CassetteDevice& cassette = mem.getCassetteDevice();
    snapshot.cassetteLoadedTape                = cassette.hasLoadedTape();
    snapshot.cassetteRecordedTape              = cassette.hasRecordedTape();
    snapshot.cassettePlaybackActive            = cassette.isPlaybackActive();
    snapshot.cassetteAudioAvailable            = cassette.isAudioAvailable();
    snapshot.cassetteHardwareAccurateLiveAudio = cassette.isHardwareAccurateLiveAudio();
    snapshot.cassetteQueuedAudioSeconds        = cassette.getQueuedAudioSeconds();
    snapshot.cassetteLoadedTransitionCount     = cassette.getLoadedTransitionCount();
    snapshot.cassetteRecordedTransitionCount   = cassette.getRecordedTransitionCount();
    snapshot.cassetteLoadedTapePath            = cassette.getLoadedTapePath();

    snapshot.sidEnabled     = mem.isSIDEnabled();
    snapshot.sidChipModel   = mem.getSID().getChipModel();
    snapshot.microSDEnabled = mem.isMicroSDEnabled();
    // TMS9918: skip when the card is unplugged — the UI doesn't render it so
    // stale snapshot contents are harmless and we save a 16 KB memcpy.
    if (mem.isTMS9918Enabled()) {
        mem.getTMS9918().copySnapshot(snapshot.tms9918);
    }
    snapshot.wifiModemEnabled = mem.isWiFiModemEnabled();
    if (snapshot.wifiModemEnabled) {
        mem.getWiFiModem().copySnapshot(snapshot.wifiModem);
    }
    snapshot.terminalCardEnabled = mem.isTerminalCardEnabled();
    if (snapshot.terminalCardEnabled) {
        mem.getTerminalCard().copySnapshot(snapshot.terminalCard);
    }
    snapshot.a1ioRtcEnabled = mem.isA1IO_RTCEnabled();
    if (snapshot.a1ioRtcEnabled) {
        mem.getA1IO_RTC().copySnapshot(snapshot.a1ioRtc);
    }
}

void SnapshotPublisher::copyTo(EmulationSnapshot& out) const
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    out = latestSnapshot;
}
