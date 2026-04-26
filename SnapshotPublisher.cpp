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

    // Page-level dirty copy: Memory::memWrite sets one bit per touched page
    // (256 B). We walk the bitmap and memcpy only the dirty pages into the
    // snapshot's RAM mirror, then clear the bitmap so the next publish starts
    // fresh. Contiguous runs are collapsed into a single memcpy — typical
    // programs touch a handful of contiguous pages per frame (zero page,
    // stack, one or two program pages), so this is usually 1-3 memcpys of
    // a few hundred bytes each instead of a flat 64 KB copy. Idle Wozmon
    // (PIA polling only, no mem[] writes) has `anyDirtyPage() == false` and
    // the snapshot data stays fresh from the previous copy for free.
    if (mem.anyDirtyPage()) {
        const auto& pages = mem.getDirtyPages();
        const quint8* memPtr = mem.getMemoryPointer();
        quint8* dstPtr = snapshot.memory.data();
        int p = 0;
        while (p < 256) {
            if (!pages.test(static_cast<std::size_t>(p))) { ++p; continue; }
            const int runStart = p;
            while (p < 256 && pages.test(static_cast<std::size_t>(p))) ++p;
            const std::size_t offset = static_cast<std::size_t>(runStart) << 8;
            const std::size_t length = static_cast<std::size_t>(p - runStart) << 8;
            std::memcpy(dstPtr + offset, memPtr + offset, length);
        }
        mem.clearDirtyPages();
    }
    // Juke-Box reads come from the EEPROM buffer, not mem[]; mirror the
    // active ROM window into the snapshot every frame so the Memory Map and
    // hex viewer match the CPU (including after EEPROM writes in RW mode).
    if (mem.isJukeBoxEnabled()) {
        const JukeBox& jb = mem.getJukeBox();
        const uint8_t* rom = jb.getRomPointer();
        const size_t romSize = jb.getRomBufferSize();
        quint8* dst = snapshot.memory.data();
        const size_t pageOff = static_cast<size_t>(jb.getCurrentPage())
                               * JukeBox::kPageSize;
        if (jb.getJumper() == JukeBox::Jumper::RAM16_ROM32) {
            if (pageOff + JukeBox::kPageSize <= romSize)
                std::memcpy(dst + 0x4000, rom + pageOff, JukeBox::kPageSize);
        } else {
            const size_t subOff = static_cast<size_t>(jb.getCurrentSubPage())
                                  * JukeBox::kSubPageSize;
            if (pageOff + subOff + JukeBox::kSubPageSize <= romSize)
                std::memcpy(dst + 0x8000, rom + pageOff + subOff, JukeBox::kSubPageSize);
        }
    }
    // Same idea for CodeTank — the bus serves reads directly from the
    // CodeTank ROM buffer, so mirror the wired-in 16 kB half into the
    // snapshot for the Memory Viewer.
    if (mem.isCodeTankEnabled()) {
        const CodeTank& ct = mem.getCodeTank();
        const uint8_t* rom = ct.getRomPointer();
        const size_t romSize = ct.getRomSize();
        const size_t halfOff = (ct.getJumper() == CodeTank::Jumper::Upper16)
                               ? CodeTank::kHalfSize : 0u;
        if (halfOff + CodeTank::kHalfSize <= romSize)
            std::memcpy(snapshot.memory.data() + CodeTank::kBase,
                        rom + halfOff, CodeTank::kHalfSize);
    }
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
    snapshot.cassettePlaybackArmed             = cassette.isPlaybackArmed();
    snapshot.cassetteRewinding                 = cassette.isRewinding();
    snapshot.cassetteAudioAvailable            = cassette.isAudioAvailable();
    snapshot.cassetteHardwareAccurateLiveAudio = cassette.isHardwareAccurateLiveAudio();
    snapshot.cassettePlaybackPaused            = cassette.isPlaybackPaused();
    snapshot.cassetteAudioStreamMode           = cassette.isAudioStreamMode();
    snapshot.cassetteQueuedAudioSeconds        = cassette.getQueuedAudioSeconds();
    snapshot.cassettePlaybackPositionSeconds   = cassette.getPlaybackPositionSeconds();
    snapshot.cassettePlaybackTotalSeconds      = cassette.getPlaybackTotalSeconds();
    snapshot.cassetteLoadedTransitionCount     = cassette.getLoadedTransitionCount();
    snapshot.cassetteRecordedTransitionCount   = cassette.getRecordedTransitionCount();
    snapshot.cassetteVolume                    = cassette.getVolume();
    snapshot.cassetteLoadedTapePath            = cassette.getLoadedTapePath();
    snapshot.cassetteLoadInfo                  = cassette.getLoadInfo();

    snapshot.sidEnabled     = mem.isSIDEnabled();
    snapshot.sidSpecialEditionEnabled = mem.isSIDSpecialEditionEnabled();
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
    snapshot.jukeBoxEnabled = mem.isJukeBoxEnabled();
    // Always copy the Juke-Box snapshot — it's tiny (a path string + a
    // handful of flags) and the Hardware window wants to display the
    // current jumper/firmware state even when the card is "unplugged"
    // but loaded in the UI.
    mem.getJukeBox().copySnapshot(snapshot.jukeBox);
    snapshot.codeTankEnabled = mem.isCodeTankEnabled();
    mem.getCodeTank().copySnapshot(snapshot.codeTank);

    // PR-40 printer: always copy the snapshot so the UI can show recent
    // paper contents (and switch mode) even if the card is momentarily
    // unplugged via the menu — matches the Juke-Box pattern above.
    snapshot.pr40Enabled = mem.isPR40Enabled();
    mem.getPR40().copySnapshot(snapshot.pr40);

    // SWTPC GT-6144: skip the 768-byte framebuffer copy when the card is
    // unplugged (same rationale as TMS9918). Snapshot lingers at whatever
    // the UI last saw, which is harmless since the window is hidden.
    snapshot.gt6144Enabled = mem.isGT6144Enabled();
    if (snapshot.gt6144Enabled) {
        mem.getGT6144().copySnapshot(snapshot.gt6144);
    }
}

void SnapshotPublisher::copyTo(EmulationSnapshot& out) const
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    out = latestSnapshot;
}
