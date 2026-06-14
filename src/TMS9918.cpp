// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 Video Display Processor emulation
// Implements the P-LAB Apple-1 Graphic Card
// https://p-l4b.github.io/graphic/
//
// Reference: TMS9918A datasheet, nippur72/apple1-videocard-lib

#include "TMS9918.h"
#include "SnapshotIO.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

// --------------------------------------------------------------------------
// openMSX VDP slot tables — TMS9918/MSX1 branch
// Verbatim copy from openMSX src/video/VDPAccessSlots.cc (lines 71-127).
// openMSX is GPL-2.0+, POM1 is GPL-2.0+ — license-compatible.
// Source: https://github.com/openMSX/openMSX
//
// Each table publishes the absolute tick positions (within a 1368-tick
// scanline) where the VDP grants the CPU a VRAM access slot. The trailing
// `1368+x` cyclic duplicates ensure a "next slot ≥ targetTick" search always
// terminates without manual wrap-around for positions near tick 1300+.
//
// `slotsMsx1ScreenOff` — display blanked (R1.6=0)
// `slotsMsx1Gfx12`     — Mode I + Mode II (Graphic 1/2; sprites on or off,
//                        openMSX does not split them on MSX1)
// `slotsMsx1Gfx3`      — Multicolor (Mode 3)
// `slotsMsx1Text`      — Text (Mode 1)
// --------------------------------------------------------------------------
namespace {

constexpr std::array<int16_t, 107 + 18> slotsMsx1ScreenOff = {
       4,   12,   20,   28,   36,   44,   52,   60,   68,   76,
      84,   92,  100,  108,  116,  124,  132,  140,  148,  156,
     164,  172,  180,  188,  196,  204,  220,  236,  252,  268,
     284,  300,  316,  332,  348,  364,  380,  396,  412,  428,
     444,  460,  476,  492,  508,  524,  540,  556,  572,  588,
     604,  620,  636,  652,  668,  684,  700,  716,  732,  748,
     764,  780,  796,  812,  828,  844,  860,  876,  892,  908,
     924,  940,  956,  972,  988, 1004, 1020, 1036, 1052, 1068,
    1084, 1100, 1116, 1132, 1148, 1164, 1180, 1196, 1212, 1228,
    1236, 1244, 1252, 1260, 1268, 1276, 1284, 1292, 1300, 1308,
    1316, 1324, 1332, 1340, 1348, 1356, 1364,
    1368+  4, 1368+ 12, 1368+ 20, 1368+ 28, 1368+ 36,
    1368+ 44, 1368+ 52, 1368+ 60, 1368+ 68, 1368+ 76,
    1368+ 84, 1368+ 92, 1368+100, 1368+108, 1368+116,
    1368+124, 1368+132, 1368+140,
};

constexpr std::array<int16_t, 19 + 8> slotsMsx1Gfx12 = {
       4,   12,   20,   28,  116,  124,  132,  140,  220,  348,
     476,  604,  732,  860,  988, 1116, 1236, 1244, 1364,
    1368+  4, 1368+ 12, 1368+ 20, 1368+ 28, 1368+116,
    1368+124, 1368+132, 1368+140,
};

constexpr std::array<int16_t, 51 + 8> slotsMsx1Gfx3 = {
       4,   12,   20,   28,  116,  124,  132,  140,  220,  228,
     260,  292,  324,  348,  356,  388,  420,  452,  476,  484,
     516,  548,  580,  604,  612,  644,  676,  708,  732,  740,
     772,  804,  836,  860,  868,  900,  932,  964,  988,  996,
    1028, 1060, 1092, 1116, 1124, 1156, 1188, 1220, 1236, 1244,
    1364,
    1368+  4, 1368+ 12, 1368+ 20, 1368+ 28, 1368+116,
    1368+124, 1368+132, 1368+140,
};

constexpr std::array<int16_t, 91 + 18> slotsMsx1Text = {
       4,   12,   20,   28,   36,   44,   52,   60,   68,   76,
      84,   92,  100,  108,  116,  124,  132,  140,  148,  156,
     164,  172,  180,  188,  196,  204,  212,  220,  228,  244,
     268,  292,  316,  340,  364,  388,  412,  436,  460,  484,
     508,  532,  556,  580,  604,  628,  652,  676,  700,  724,
     748,  772,  796,  820,  844,  868,  892,  916,  940,  964,
     988, 1012, 1036, 1060, 1084, 1108, 1132, 1156, 1180, 1196,
    1204, 1212, 1220, 1228, 1236, 1244, 1252, 1260, 1268, 1276,
    1284, 1292, 1300, 1308, 1316, 1324, 1332, 1340, 1348, 1356,
    1364,
    1368+  4, 1368+ 12, 1368+ 20, 1368+ 28, 1368+ 36,
    1368+ 44, 1368+ 52, 1368+ 60, 1368+ 68, 1368+ 76,
    1368+ 84, 1368+ 92, 1368+100, 1368+108, 1368+116,
    1368+124, 1368+132, 1368+140,
};

const char* slotTableName(const int16_t* data)
{
    if (data == slotsMsx1ScreenOff.data()) return "ScreenOff";
    if (data == slotsMsx1Gfx12.data())     return "Gfx12";
    if (data == slotsMsx1Gfx3.data())      return "Gfx3";
    if (data == slotsMsx1Text.data())      return "Text";
    return "?";
}

} // namespace

// --------------------------------------------------------------------------
// TMS9918A standard color palette (RGBA via IM_COL32)
// --------------------------------------------------------------------------
const ImU32 TMS9918::kPalette[16] = {
    IM_COL32(  0,   0,   0,   0),   //  0  Transparent
    IM_COL32(  0,   0,   0, 255),   //  1  Black
    IM_COL32( 33, 200,  66, 255),   //  2  Medium Green
    IM_COL32( 94, 220, 120, 255),   //  3  Light Green
    IM_COL32( 84,  85, 237, 255),   //  4  Dark Blue
    IM_COL32(125, 118, 252, 255),   //  5  Light Blue
    IM_COL32(212,  82,  77, 255),   //  6  Dark Red
    IM_COL32( 66, 235, 245, 255),   //  7  Cyan
    IM_COL32(252,  85,  84, 255),   //  8  Medium Red
    IM_COL32(255, 121, 120, 255),   //  9  Light Red
    IM_COL32(212, 193,  84, 255),   // 10  Dark Yellow
    IM_COL32(230, 206, 128, 255),   // 11  Light Yellow
    IM_COL32( 33, 176,  59, 255),   // 12  Dark Green
    IM_COL32(201,  91, 186, 255),   // 13  Magenta
    IM_COL32(204, 204, 204, 255),   // 14  Grey
    IM_COL32(255, 255, 255, 255),   // 15  White
};

// --------------------------------------------------------------------------
// Construction / reset
// --------------------------------------------------------------------------
TMS9918::TMS9918()
{
    reset();
}

void TMS9918::reset()
{
    // Power-on VRAM. Two profiles:
    //   - vramNoiseOnReset == false (default): MSX1 silicon bistable per
    //     meisei vdp.c:212-217 — $FF on even, $00 on odd. MSX2 settles to
    //     all-$FF. Preserved as default for backwards compatibility with
    //     existing tests and recorded snapshots.
    //   - vramNoiseOnReset == true: true mt19937 noise, which is what
    //     the warm P-LAB DRAM actually shows on cold boot. Surfaces the
    //     class of bugs from doc/TMS9918-SPRITE_INIT.md §4.2 (uninit SAT,
    //     ghost terminators, etc.) under POM1's silicon-strict mode.
    if (vramNoiseOnReset) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& byte : vram) byte = static_cast<uint8_t>(dist(gen));
    } else {
        for (size_t i = 0; i < vram.size(); ++i) {
            vram[i] = (i & 1) ? 0x00 : 0xFF;
        }
    }
    regs.fill(0);
    // Power-on backdrop: white. Empirical observation on Claudio's Replica-1
    // 1:1 silicon (2026-05-12): hardReset boots into a white-on-white screen
    // (white backdrop, display ON appears white because pattern table holds
    // unpredictable bits that mostly render through the bistable VRAM). POM1
    // emulates the visual result by setting R7 = $0F (colour 15 = white) on
    // reset; with R1 bit 6 = 0 (display blanked from regs.fill(0)) the
    // renderer paints just the backdrop, giving uniform white.
    regs[7] = 0x0F;
    statusReg       = 0;
    controlLatch    = 0;
    latchIsSecond   = false;
    vramAddr        = 0;
    readAheadBuffer = 0;
    frameCycleCounter = 0;
    pendingDrainCycles = 0;
    lastScanlineProcessed = -1;
    lastScanlineRendered  = -1;
    framebuffer.fill(kPalette[15]);            // start with all-white screen (matches Replica-1 silicon)
    droppedWrites = 0;
    droppedWriteTraceCount = 0;
    dropStats = DropDiagnostics{};
    snapshotDirty   = true;
}

void TMS9918::setSiliconStrictMode(bool enabled)
{
    siliconStrictMode = enabled;
    pendingDrainCycles = 0;         // chip frais — first access always lands
    droppedWrites = 0;
    droppedWriteTraceCount = 0;     // restart the counter on toggle so users see new drops only
    dropStats = DropDiagnostics{};
}

void TMS9918::resetDroppedWriteCount()
{
    droppedWrites = 0;
    droppedWriteTraceCount = 0;
    dropStats = DropDiagnostics{};
}

uint16_t TMS9918::vramMaskForRegs(const uint8_t* regs, bool strict)
{
    return (strict && (regs[1] & 0x80) == 0) ? 0x0FFF : 0x3FFF;
}

uint16_t TMS9918::liveVramMask() const
{
    return vramMaskForRegs(regs.data(), siliconStrictMode);
}

// --------------------------------------------------------------------------
// Slot-table model (openMSX port)
//
// Replaces the old min-distance threshold with openMSX's slot-position
// approach. Each CPU access at $CC00/$CC01 looks up the next free slot in
// the active scanline-tick table; if the chip is still draining a previous
// access (`pendingDrainCycles > 0`) the new byte is silently overwritten
// (== silicon's "tooFastCallback" behaviour with allowTooFastAccess=false).
//
// Side note: openMSX does not split sprites-on/off on MSX1 — the same
// `slotsMsx1Gfx12` table covers both. We follow suit and removed the
// old SAT[0]==$D0 fast-path (silicon doesn't discriminate either).
//
// Reference: src/video/VDPAccessSlots.cc in the openMSX repository.
// --------------------------------------------------------------------------
TMS9918::SlotSpan TMS9918::selectSlotTable() const
{
    // Silicon-correct relaxation: during vertical retrace (VBlank, ~70 lines
    // out of 262 NTSC) the chip's pixel scan + sprite scan don't fetch VRAM,
    // so the CPU bandwidth becomes free regardless of R1.6. openMSX's stock
    // `getTab(vdp)` does not model this — it always returns the mode-specific
    // table. We deliberately diverge here because real silicon programs
    // (Rogue, Galaga init) batch their VRAM uploads in VBlank and silicon
    // accepts every byte, while the unmodified openMSX model would
    // (incorrectly) drop bursts < ~7c gap during VBlank.
    //
    // Threshold: frameCycleCounter >= kActiveDisplayCycles. This is the same
    // gate used by the F-flag set in advanceCycles (vertical-retrace edge
    // detection) — keeps the two behaviours consistent.
    const bool inVBlank = frameCycleCounter >= kActiveDisplayCycles;
    const bool displayEnabled = (regs[1] & 0x40) != 0;
    if (!displayEnabled || inVBlank)
        return SlotSpan{slotsMsx1ScreenOff.data(), slotsMsx1ScreenOff.size()};
    const bool m1 = (regs[1] & 0x10) != 0;   // text
    const bool m2 = (regs[1] & 0x08) != 0;   // multicolor
    if (m1) return SlotSpan{slotsMsx1Text.data(),  slotsMsx1Text.size()};
    if (m2) return SlotSpan{slotsMsx1Gfx3.data(),  slotsMsx1Gfx3.size()};
    return SlotSpan{slotsMsx1Gfx12.data(), slotsMsx1Gfx12.size()};
}

TMS9918::SlotTableId TMS9918::activeSlotTableId() const
{
    const bool inVBlank = frameCycleCounter >= kActiveDisplayCycles;
    const bool displayEnabled = (regs[1] & 0x40) != 0;
    if (!displayEnabled || inVBlank)  return kSlotTableScreenOff;
    if (regs[1] & 0x10)               return kSlotTableText;
    if (regs[1] & 0x08)               return kSlotTableGfx3;
    return kSlotTableGfx12;
}

void TMS9918::noteDroppedAccess(char port, uint8_t value)
{
    ++droppedWrites;
    DropDiagnostics& d = dropStats;
    ++d.total;
    if (port == 'D') ++d.writeData; else ++d.writeCtrl;
    const SlotTableId tbl = activeSlotTableId();
    ++d.byTable[tbl];
    const bool inVBlank = frameCycleCounter >= kActiveDisplayCycles;
    if (inVBlank) ++d.inVBlank; else ++d.inActive;
    ++d.byPc[lastAccessPc];

    // First N drops also dump a single-line trace so devs see the problem
    // without having to call dumpDropDiagnostics(). Trace cap survives the
    // session — call resetDroppedWriteCount() to start a fresh window.
    if (droppedWriteTraceCount < 60) {
        const int posTicks = (frameCycleCounter * kVdpTicksPerCpuCycle) % kVdpTicksPerLine;
        const auto slots   = selectSlotTable();
        const int targetTick = posTicks + kVdpDeltaD28Ticks;
        int slotTick = slots.back();
        for (int v : slots) { if (v >= targetTick) { slotTick = v; break; } }
        const int slotShortBy = pendingDrainCycles;     // cycles still owed to chip
        std::fprintf(stderr,
            "[TMS9918 DROP #%llu] %c val=%02X vramAddr=%04X latch2=%d "
            "drain=%dc linePos=%d nextSlot=%d tbl=%s vblank=%d "
            "frameCycle=%d R1=%02X PC=$%04X\n",
            (unsigned long long)droppedWrites,
            (port == 'D' ? 'D' : 'C'),
            value, vramAddr, (int)latchIsSecond,
            slotShortBy, posTicks, slotTick, slotTableName(slots.data),
            (int)inVBlank, frameCycleCounter, regs[1], lastAccessPc);
        ++droppedWriteTraceCount;
    }
}

void TMS9918::dumpDropDiagnostics(std::FILE* out, int topN) const
{
    if (!out) out = stderr;
    const DropDiagnostics& d = dropStats;
    std::fprintf(out, "===== TMS9918 drop diagnostics =====\n");
    std::fprintf(out, "  silicon strict   : %s\n", siliconStrictMode ? "ON" : "OFF");
    std::fprintf(out, "  total drops      : %llu\n", (unsigned long long)d.total);
    if (d.total == 0) {
        std::fprintf(out, "  (no drops recorded since last reset / strict toggle)\n");
        std::fprintf(out, "====================================\n");
        return;
    }
    std::fprintf(out, "  by port          : data($CC00) %llu  ctrl($CC01) %llu\n",
                 (unsigned long long)d.writeData, (unsigned long long)d.writeCtrl);
    std::fprintf(out, "  by display phase : active %llu  vblank %llu\n",
                 (unsigned long long)d.inActive, (unsigned long long)d.inVBlank);
    static const char* kTableNames[kSlotTableCount] = {
        "ScreenOff", "Gfx12 (Mode I/II)", "Gfx3 (Multicolor)", "Text"
    };
    std::fprintf(out, "  by slot table    :\n");
    for (int i = 0; i < kSlotTableCount; ++i) {
        if (d.byTable[i] == 0) continue;
        std::fprintf(out, "    %-20s %llu\n", kTableNames[i],
                     (unsigned long long)d.byTable[i]);
    }
    // Sort PC histogram, keep top N.
    std::vector<std::pair<uint16_t, uint64_t>> top(d.byPc.begin(), d.byPc.end());
    std::sort(top.begin(), top.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    const int n = std::min<int>(topN, (int)top.size());
    std::fprintf(out, "  top %d PC sites  : (PC = mid-instruction; STA site is PC-3 for STA abs / STA abs,X)\n", n);
    std::fprintf(out, "    %-8s %-12s %-s\n", "PC", "drops", "share");
    for (int i = 0; i < n; ++i) {
        const double pct = 100.0 * (double)top[i].second / (double)d.total;
        std::fprintf(out, "    $%04X    %-12llu %5.1f%%\n",
                     (unsigned)top[i].first,
                     (unsigned long long)top[i].second,
                     pct);
    }
    if ((int)top.size() > n) {
        std::fprintf(out, "    ...      (%d more PC sites omitted)\n", (int)top.size() - n);
    }
    std::fprintf(out, "====================================\n");
    std::fflush(out);
}

bool TMS9918::canAcceptAccess() const
{
    return !siliconStrictMode || pendingDrainCycles <= 0;
}

void TMS9918::noteAcceptedAccess()
{
    if (!siliconStrictMode) return;
    const int posTicks   = (frameCycleCounter * kVdpTicksPerCpuCycle) % kVdpTicksPerLine;
    const int targetTick = posTicks + kVdpDeltaD28Ticks;
    auto slots = selectSlotTable();
    int slotTick = slots.back();              // wrap duplicates guarantee a hit
    for (int v : slots) {
        if (v >= targetTick) { slotTick = v; break; }
    }
    const int drainTicks = slotTick - posTicks;
    pendingDrainCycles   = (drainTicks + kVdpTicksPerCpuCycle - 1) / kVdpTicksPerCpuCycle;
}

// --------------------------------------------------------------------------
// I/O — Data port ($CC00)
// --------------------------------------------------------------------------
void TMS9918::writeData(uint8_t value)
{
    latchIsSecond = false;                    // data-port access resets latch state
    if (!canAcceptAccess()) {
        noteDroppedAccess('D', value);
        return;
    }
    noteAcceptedAccess();
    const uint16_t mask = liveVramMask();
    vram[vramAddr & mask] = value;
    readAheadBuffer = value;
    vramAddr = (vramAddr + 1) & mask;
    snapshotDirty = true;
}

uint8_t TMS9918::readData()
{
    latchIsSecond = false;                    // data-port access resets latch state
    if (!canAcceptAccess()) return readAheadBuffer;
    noteAcceptedAccess();
    const uint16_t mask = liveVramMask();
    uint8_t result = readAheadBuffer;
    readAheadBuffer = vram[vramAddr & mask];
    vramAddr = (vramAddr + 1) & mask;
    return result;
}

// --------------------------------------------------------------------------
// I/O — Control port ($CC01)
// --------------------------------------------------------------------------
void TMS9918::writeControl(uint8_t value)
{
    if (!canAcceptAccess()) {
        noteDroppedAccess('C', value);
        return;
    }
    noteAcceptedAccess();
    if (!latchIsSecond) {
        // First byte — store in latch
        controlLatch  = value;
        latchIsSecond = true;
        return;
    }

    // Second byte — decode command
    latchIsSecond = false;

    uint8_t cmd = value & 0xC0;

    if (cmd == 0x00) {
        // Set VRAM read address
        vramAddr = ((uint16_t)(value & 0x3F) << 8) | controlLatch;
        // Pre-fetch first byte into read-ahead buffer
        const uint16_t mask = liveVramMask();
        readAheadBuffer = vram[vramAddr & mask];
        vramAddr = (vramAddr + 1) & mask;
    }
    else if (cmd == 0x40) {
        // Set VRAM write address
        vramAddr = ((uint16_t)(value & 0x3F) << 8) | controlLatch;
    }
    else if (cmd == 0x80) {
        // Write to register
        uint8_t regNum = value & 0x07;
        regs[regNum] = controlLatch;
        snapshotDirty = true;             // register change alters rendering
    }
    // cmd == 0xC0 is undefined on TMS9918, ignored
}

uint8_t TMS9918::readControl()
{
    if (!canAcceptAccess()) return statusReg;
    noteAcceptedAccess();
    latchIsSecond = false;
    uint8_t result = statusReg;
    // Clear frame flag (bit 7), collision flag (bit 5) on read
    statusReg &= ~0xE0;
    return result;
}

// --------------------------------------------------------------------------
// Cycle counting — frame flag generation
// --------------------------------------------------------------------------
void TMS9918::advanceCycles(int cycles)
{
    if (pendingDrainCycles > 0) {
        pendingDrainCycles -= cycles;
        if (pendingDrainCycles < 0) pendingDrainCycles = 0;
    }
    const int prevCounter = frameCycleCounter;
    frameCycleCounter += cycles;

    // F flag (bit 7) and the per-frame sprite-engine status scan fire at
    // the START of VBlank — the moment the beam crosses from line 191 into
    // line 192 (real silicon: flag goes high during vertical retrace, see
    // TMS9918A datasheet §3.5.1). The 6502 idiom for VBlank-synced VRAM
    // bursts depends on this:
    //
    //     @wait_vbl: BIT $CC01      ; clear stale F + N := bit 7
    //                BPL @wait_vbl  ; spin until fresh F set
    //     ; ... 4554c of free 2c-gate VRAM bandwidth before active display ...
    //
    // Originally POM1 fired F at the frame ROLLOVER (counter wraps to 0,
    // == start of next active display) which left zero VBlank window
    // post-poll. Detecting the "just crossed kActiveDisplayCycles" edge
    // restores the silicon-correct cadence.
    // Per-scanline sprite scan (Bug N°5/N°6/N°10) AND per-scanline progressive
    // rendering: walk every active scanline the simulated raster has just
    // crossed since the previous advanceCycles. Each crossing both updates
    // statusReg bits 5/6/0..4 silicon-correctly AND writes the active 256
    // pixels of the line into the live framebuffer using LIVE state — so
    // mid-frame R7/R1/VRAM changes propagate progressively (rainbow demos /
    // raster split). Top border is painted at frame start, bottom at line 192.
    {
        const int totalScanlineNow = (int)((int64_t)frameCycleCounter * 262 / kCyclesPerFrame);
        const int activeNow = std::min(totalScanlineNow, kScreenHeight);

        // Paint the top border once per frame, on the first non-zero progress.
        if (lastScanlineRendered == -1 && activeNow >= 0) {
            paintTopBorder();
        }

        if (activeNow > lastScanlineProcessed) {
            for (int line = lastScanlineProcessed + 1; line < activeNow; ++line) {
                scanSpritesForLine(line);
            }
            lastScanlineProcessed = activeNow - 1 < kScreenHeight - 1
                                  ? activeNow - 1
                                  : kScreenHeight - 1;
            if (activeNow >= kScreenHeight) lastScanlineProcessed = kScreenHeight;
        }
        if (activeNow > lastScanlineRendered) {
            for (int line = lastScanlineRendered + 1; line < activeNow; ++line) {
                renderActiveLine(line);
            }
            lastScanlineRendered = activeNow - 1 < kScreenHeight - 1
                                 ? activeNow - 1
                                 : kScreenHeight - 1;
            if (activeNow >= kScreenHeight) {
                lastScanlineRendered = kScreenHeight;
                paintBottomBorder();
            }
        }
    }

    if (prevCounter < kActiveDisplayCycles && frameCycleCounter >= kActiveDisplayCycles) {
        // F flag rises at VBlank entry. Per-scanline scan above has already
        // updated bits 5/6/0..4 progressively; if display was blanked the
        // whole active period (no scanSpritesForLine work happened),
        // scanSpritesForStatus runs as a fallback — silicon does walk the
        // SAT during vertical retrace, but on POM1 the per-line path is
        // strictly more accurate, so the fallback only fires when the
        // per-line path stayed dormant.
        if ((regs[1] & 0x40) && lastScanlineProcessed < kScreenHeight) {
            scanSpritesForStatus(vram.data(), regs.data(), siliconStrictMode, statusReg);
        }
        statusReg |= 0x80;
    }

    // Frame counter rollover at the END of VBlank (== start of next active
    // display). No flag side-effect — the F flag stays sticky-set until
    // software reads $CC01 to clear it. Loop with `while` so a single huge
    // advanceCycles (e.g. tickFrame test passing 2M cycles) can rollover
    // multiple times instead of leaving frameCycleCounter stuck above
    // kCyclesPerFrame for the next call.
    while (frameCycleCounter >= kCyclesPerFrame) {
        frameCycleCounter -= kCyclesPerFrame;
        lastScanlineProcessed = -1;               // reset per-line scan progress
        lastScanlineRendered  = -1;               // reset per-line render progress
    }
}

// --------------------------------------------------------------------------
// Snapshot
// --------------------------------------------------------------------------
void TMS9918::copySnapshot(Snapshot& out)
{
    // Status register changes on every frame tick, so always mirror it.
    out.statusReg = statusReg;
    out.siliconStrictMode = siliconStrictMode;
    // VRAM (16 KB) + register file + 288×216 framebuffer (~243 KB) only move
    // when the card is actually touched by software — a dirty flag avoids
    // unnecessary memcpy on idle frames. The framebuffer is the silicon-
    // progressive raster — UI uploads it to GL directly without any further
    // per-snapshot rendering.
    if (snapshotDirty) {
        std::memcpy(out.vram.data(), vram.data(), vram.size());
        std::memcpy(out.regs.data(), regs.data(), regs.size());
        std::memcpy(out.framebuffer.data(), framebuffer.data(),
                    framebuffer.size() * sizeof(uint32_t));
        snapshotDirty = false;
    }
}

// --------------------------------------------------------------------------
// Top-level render — fills a kScreenWidth×kScreenHeight RGBA pixel buffer.
// IM_COL32 byte order on little-endian is [R,G,B,A] which matches
// GL_RGBA/GL_UNSIGNED_BYTE, so the buffer can be uploaded to an OpenGL
// texture and displayed with nearest-neighbour filtering at any window size.
// --------------------------------------------------------------------------
void TMS9918::renderToBuffer(uint32_t* pixels, const Snapshot& snap)
{
    // Refactored 2026-05 to delegate to the per-line raw renderers
    // (the same path used by renderActiveLine for live progressive
    // rasterisation). Eliminates the previous divergence where the
    // legacy snapshot path used its own mode dispatch (no meisei
    // hybrid handling, 8/8 text borders) while the live path got
    // the openMSX/meisei-correct dispatch. Keeps the 4 frame-at-a-
    // time renderers (renderGraphicsI/II/Text/Multicolor/Sprites)
    // intact for any direct callers but routes the snapshot entry
    // through the line-raw path.
    const uint8_t backdropIdx = snap.regs[7] & 0x0F;
    const ImU32   backdrop    = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];

    for (int i = 0; i < kScreenWidth * kScreenHeight; ++i) pixels[i] = backdrop;

    if ((snap.regs[1] & 0x40) == 0) return;       // display blanked

    const bool m1 = (snap.regs[1] & 0x10) != 0;
    const bool m2 = (snap.regs[1] & 0x08) != 0;
    const bool m3 = (snap.regs[0] & 0x02) != 0;
    const uint16_t mask = vramMaskForRegs(snap.regs.data(), snap.siliconStrictMode);

    // meisei mode encoding: bit 0 = M1, bit 1 = M3, bit 2 = M2.
    int mode = (m1 ? 1 : 0) | (m3 ? 2 : 0) | (m2 ? 4 : 0);
    if ((mode & 2) && (mode & 5)) mode ^= 2;

    for (int line = 0; line < kScreenHeight; ++line) {
        uint32_t* lineBuf = &pixels[line * kScreenWidth];
        // Backdrop pre-fill already done above.

        switch (mode) {
            case 0: renderGfxILineRaw      (line, lineBuf, snap.vram.data(), snap.regs.data(), mask, backdrop); break;
            case 1: renderTextLineRaw      (line, lineBuf, snap.vram.data(), snap.regs.data(), mask, backdrop); break;
            case 2: renderGfxIILineRaw     (line, lineBuf, snap.vram.data(), snap.regs.data(), mask, backdrop); break;
            case 4: renderMulticolorLineRaw(line, lineBuf, snap.vram.data(), snap.regs.data(), mask, backdrop); break;
            case 5: renderTextBarsLineRaw  (line, lineBuf, snap.regs.data(), backdrop); break;
            default: break;
        }

        if (!m1) {
            renderSpritesLineRaw(line, lineBuf, snap.vram.data(), snap.regs.data(), mask);
            if (isCloningActive(snap.regs.data(), ChipType::TMS9918A)) {
                renderCloneSpritesLineRaw(line, lineBuf, snap.vram.data(), snap.regs.data(), mask);
            }
        }
    }
}

// --------------------------------------------------------------------------
// renderToBufferWithBorder — 288×216 with R7-coloured borders.
// Renders the 256×192 active rect at offset (kBorderLeft, kBorderTop).
// Border pixels are painted with kPalette[regs[7] & 0x0F] regardless of
// display blank state — silicon's "border colour" stays alive even when
// R1 bit 6 = 0 (blanked active area).
// --------------------------------------------------------------------------
void TMS9918::renderToBufferWithBorder(uint32_t* pixels, const Snapshot& snap)
{
    const uint8_t backdropIdx = snap.regs[7] & 0x0F;
    const ImU32   border      = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];

    // Fill the entire 288×216 surface with the border colour first; the
    // active 256×192 rect is overwritten below.
    for (int i = 0; i < kFullWidth * kFullHeight; ++i) pixels[i] = border;

    // Render the active rect into a temporary 256×192 buffer, then blit
    // it centred into the full frame. Reusing renderToBuffer keeps the
    // display-mode dispatch logic in one place. The temp lives in static
    // thread_local storage (~192 KB) — too big for stack, called every
    // frame so reuse beats heap alloc.
    static thread_local uint32_t active[kScreenWidth * kScreenHeight];
    renderToBuffer(active, snap);
    for (int sy = 0; sy < kScreenHeight; ++sy) {
        const int dstY = sy + kBorderTop;
        std::memcpy(&pixels[dstY * kFullWidth + kBorderLeft],
                    &active[sy * kScreenWidth],
                    kScreenWidth * sizeof(uint32_t));
    }
}

// --------------------------------------------------------------------------
// Per-line raw renderers — silicon-progressive rasterisation building blocks.
// Each writes 256 pixels into `lineBuf` for scanline `line`, reading from
// raw vram/regs pointers (live or snapshot — caller decides). Backdrop
// pre-fill is the caller's responsibility; these renderers overwrite every
// pixel of `lineBuf` they visit. Sprites are NOT included — call
// renderSpritesLineRaw separately after the background pass.
// --------------------------------------------------------------------------

void TMS9918::renderGfxILineRaw(int line, uint32_t* lineBuf,
                                const uint8_t* vram, const uint8_t* regs,
                                uint16_t vramMask, ImU32 backdrop)
{
    const uint16_t nameBase    = (uint16_t)(regs[2] & 0x0F) << 10;
    const uint16_t colorBase   = (uint16_t)regs[3]          << 6;
    const uint16_t patternBase = (uint16_t)(regs[4] & 0x07) << 11;
    const int row       = line / 8;
    const int lineInRow = line % 8;
    for (int col = 0; col < 32; ++col) {
        const uint8_t  name      = vram[(nameBase + row * 32 + col) & vramMask];
        const uint8_t  colorByte = vram[(colorBase + (name >> 3))    & vramMask];
        const uint8_t  fgIdx     = (colorByte >> 4) & 0x0F;
        const uint8_t  bgIdx     =  colorByte       & 0x0F;
        const ImU32    fg        = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
        const ImU32    bg        = (bgIdx == 0) ? backdrop : kPalette[bgIdx];
        const uint16_t patAddr   = (patternBase + (uint16_t)name * 8 + lineInRow) & vramMask;
        const uint8_t  pat       = vram[patAddr];
        for (int bit = 0; bit < 8; ++bit)
            lineBuf[col * 8 + bit] = (pat & (0x80 >> bit)) ? fg : bg;
    }
}

void TMS9918::renderGfxIILineRaw(int line, uint32_t* lineBuf,
                                 const uint8_t* vram, const uint8_t* regs,
                                 uint16_t vramMask, ImU32 backdrop)
{
    const uint16_t nameBase    = (uint16_t)(regs[2] & 0x0F) << 10;
    const uint16_t colorBase   = (uint16_t)(regs[3] & 0x80) << 6;
    const uint16_t colorMask   = ((uint16_t)(regs[3] & 0x7F) << 6) | 0x003F;
    const uint16_t patternBase = (uint16_t)(regs[4] & 0x04) << 11;
    const uint16_t patternMask = ((uint16_t)(regs[4] & 0x03) << 11) | 0x07FF;
    const int row       = line / 8;
    const int lineInRow = line % 8;
    const int section   = row / 8;
    for (int col = 0; col < 32; ++col) {
        const uint8_t  name       = vram[(nameBase + row * 32 + col) & vramMask];
        const uint16_t charOffset = (uint16_t)(section * 256 + name) * 8 + lineInRow;
        const uint16_t patAddr    = patternBase + (charOffset & patternMask);
        const uint16_t colAddr    = colorBase   + (charOffset & colorMask);
        const uint8_t  pat        = vram[patAddr & vramMask];
        const uint8_t  colorByte  = vram[colAddr & vramMask];
        const uint8_t  fgIdx      = (colorByte >> 4) & 0x0F;
        const uint8_t  bgIdx      =  colorByte       & 0x0F;
        const ImU32    fg         = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
        const ImU32    bg         = (bgIdx == 0) ? backdrop : kPalette[bgIdx];
        for (int bit = 0; bit < 8; ++bit)
            lineBuf[col * 8 + bit] = (pat & (0x80 >> bit)) ? fg : bg;
    }
}

void TMS9918::renderTextLineRaw(int line, uint32_t* lineBuf,
                                const uint8_t* vram, const uint8_t* regs,
                                uint16_t vramMask, ImU32 backdrop)
{
    // Text mode: 240 pixels wide centred in 256 with ASYMMETRIC borders
    // per the TMS9918A datasheet and meisei vdp.c:475-510:
    //   left border  = 6 pixels of backdrop
    //   text content = 40 cols × 6 px = 240 pixels
    //   right border = 10 pixels of backdrop
    // (NOT 8/8 symmetric — fixed 2026-05 to match silicon.)
    // Caller already pre-filled lineBuf with backdrop, so we paint only
    // the inner 240 px and leave the borders untouched.
    const uint16_t nameBase    = (uint16_t)(regs[2] & 0x0F) << 10;
    const uint16_t patternBase = (uint16_t)(regs[4] & 0x07) << 11;
    const uint8_t  fgIdx       = (regs[7] >> 4) & 0x0F;
    const uint8_t  bgIdx       =  regs[7]       & 0x0F;
    const ImU32    fg          = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
    const ImU32    bg          = (bgIdx == 0) ? backdrop : kPalette[bgIdx];
    const int row       = line / 8;
    const int lineInRow = line % 8;
    for (int col = 0; col < 40; ++col) {
        const uint8_t  name    = vram[(nameBase + row * 40 + col) & vramMask];
        const uint16_t patAddr = (patternBase + (uint16_t)name * 8 + lineInRow) & vramMask;
        const uint8_t  pat     = vram[patAddr];
        for (int bit = 0; bit < 6; ++bit)
            lineBuf[6 + col * 6 + bit] = (pat & (0x80 >> bit)) ? fg : bg;
    }
}

// --------------------------------------------------------------------------
// renderTextBarsLineRaw — static "vertical bars" glitch for hybrid mode 5
// (M1+M2, optionally with M3 ignored). Per meisei vdp.c:480-488 the chip
// emits a deterministic pattern of 4 px text-color + 2 px backdrop, ×40,
// fully independent of VRAM contents. Borders match standard text mode
// (6 left / 10 right). Triggered by Lotus F3 in MSX1-palette mode and by
// dvik/joyrex's "scr5.rom" / Illusions demo.
// --------------------------------------------------------------------------
void TMS9918::renderTextBarsLineRaw(int line, uint32_t* lineBuf,
                                    const uint8_t* regs, ImU32 backdrop)
{
    (void)line;                                  // pattern is identical every line
    const uint8_t fgIdx = (regs[7] >> 4) & 0x0F;
    const ImU32   tc    = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
    for (int x = 0; x < 40; ++x) {
        const int base = 6 + x * 6;              // 6-px left border per text mode
        lineBuf[base]     = tc;
        lineBuf[base + 1] = tc;
        lineBuf[base + 2] = tc;
        lineBuf[base + 3] = tc;
        lineBuf[base + 4] = backdrop;
        lineBuf[base + 5] = backdrop;
    }
}

void TMS9918::renderMulticolorLineRaw(int line, uint32_t* lineBuf,
                                      const uint8_t* vram, const uint8_t* regs,
                                      uint16_t vramMask, ImU32 backdrop)
{
    const uint16_t nameBase    = (uint16_t)(regs[2] & 0x0F) << 10;
    const uint16_t patternBase = (uint16_t)(regs[4] & 0x07) << 11;
    const int row    = line / 8;
    const int subRow = (line % 8) / 4;             // 0 or 1 within the 8-row band
    const int patRow = (row % 4) * 2 + subRow;
    for (int col = 0; col < 32; ++col) {
        const uint8_t  name      = vram[(nameBase + row * 32 + col) & vramMask];
        const uint8_t  colorByte = vram[(patternBase + (uint16_t)name * 8 + patRow) & vramMask];
        const uint8_t  leftIdx   = (colorByte >> 4) & 0x0F;
        const uint8_t  rightIdx  =  colorByte       & 0x0F;
        const ImU32    leftCol   = (leftIdx  == 0) ? backdrop : kPalette[leftIdx];
        const ImU32    rightCol  = (rightIdx == 0) ? backdrop : kPalette[rightIdx];
        for (int px = 0; px < 4; ++px) lineBuf[col * 8 + px]     = leftCol;
        for (int px = 0; px < 4; ++px) lineBuf[col * 8 + 4 + px] = rightCol;
    }
}

void TMS9918::renderSpritesLineRaw(int line, uint32_t* lineBuf,
                                   const uint8_t* vram, const uint8_t* regs,
                                   uint16_t vramMask)
{
    const uint16_t sprAttrBase    = (uint16_t)(regs[5] & 0x7F) << 7;
    const uint16_t sprPatternBase = (uint16_t)(regs[6] & 0x07) << 11;
    const bool doubleSize = (regs[1] & 0x02) != 0;
    const bool magnified  = (regs[1] & 0x01) != 0;
    const int  sprPixelSize = doubleSize ? 16 : 8;
    const int  mag          = magnified  ? 2  : 1;
    const int  spriteH      = sprPixelSize * mag;

    // Up to 4 visible sprites, in SAT order (priority high → low).
    int visIdx[4];
    int nVis = 0;
    for (int i = 0; i < 32 && nVis < 4; ++i) {
        const uint16_t attrAddr = (sprAttrBase + i * 4) & vramMask;
        const uint8_t  yRaw = vram[attrAddr];
        if (yRaw == 0xD0) break;
        const int y = (int)yRaw - ((yRaw > 0xD0) ? 256 : 0) + 1;
        if (line < y || line >= y + spriteH) continue;
        visIdx[nVis++] = i;
    }
    // Render reverse-priority (sprite 0 paints last → on top).
    for (int k = nVis - 1; k >= 0; --k) {
        const int     i        = visIdx[k];
        const uint16_t attrAddr = (sprAttrBase + i * 4) & vramMask;
        const int      y        = (int)vram[attrAddr] - ((vram[attrAddr] > 0xD0) ? 256 : 0) + 1;
        const uint8_t  color    = vram[(attrAddr + 3) & vramMask];
        if ((color & 0x0F) == 0) continue;        // transparent — skip pixel paint
        const ImU32    sprColor = kPalette[color & 0x0F];
        int            x        = vram[(attrAddr + 1) & vramMask];
        if (color & 0x80) x -= 32;
        uint8_t patName = vram[(attrAddr + 2) & vramMask];
        if (doubleSize) patName &= 0xFC;
        const int rowInSpr = (line - y) / mag;
        const int halves = doubleSize ? 2 : 1;
        for (int half = 0; half < halves; ++half) {
            uint16_t patAddr;
            if (!doubleSize) {
                patAddr = (sprPatternBase + (uint16_t)patName * 8 + rowInSpr) & vramMask;
            } else {
                const int quadrant = (rowInSpr < 8) ? half * 2 : half * 2 + 1;
                patAddr = (sprPatternBase + (uint16_t)(patName + quadrant) * 8 + (rowInSpr & 7)) & vramMask;
            }
            const uint8_t patByte = vram[patAddr];
            for (int bit = 0; bit < 8; ++bit) {
                if (!(patByte & (0x80 >> bit))) continue;
                const int screenX = x + (half * 8 + bit) * mag;
                for (int mx = 0; mx < mag; ++mx) {
                    const int sx = screenX + mx;
                    if (sx < 0 || sx >= kScreenWidth) continue;
                    lineBuf[sx] = sprColor;
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// isIllegalModeRegs — hybrid-mode detector for the BG-render bypass path.
// Returns true when 2 or more of M1/M2/M3 are simultaneously set
// (reserved combinations the chip doesn't define a playfield render for).
// In this regime POM1 falls back to backdrop-only and lets the sprite
// engine still drive — see renderActiveLine().
//
// Note: the *cloning* effect (Bug N°8) does NOT depend on hybrid mode —
// it depends on M3 + R4 (see isCloningActive below). The two predicates
// are deliberately independent now.
// --------------------------------------------------------------------------
bool TMS9918::isIllegalModeRegs(const uint8_t* regs)
{
    int n = 0;
    if (regs[1] & 0x10) ++n;        // M1 (text)
    if (regs[1] & 0x08) ++n;        // M2 (multicolor)
    if (regs[0] & 0x02) ++n;        // M3 (graphic 2 / bitmap)
    return n >= 2;
}

// --------------------------------------------------------------------------
// isCloningActive — meisei vdp.c:592 condition. The TMS9918A NMOS
// chip clones sprites 8..31 into the top of the frame whenever:
//   1. M3 (R0 bit 1) is set, AND
//   2. R4 bits 0-1 are NOT both set (i.e. R4 & 3 != 3 — the chip needs
//      at least one pattern-table block-mask bit cleared for the leak
//      path to expose itself)
// Toshiba clones and Yamaha V9938+ have factory-fixed addressing and
// never clone. POM1 hard-codes "TI silicon" — no chip-kind dispatch.
//
// Independence from isIllegalModeRegs: hap's MSX BASIC test
// (openMSX issue #593) sets up Mode II perfectly legally (only M3
// set) but with R4=0 → cloning fires. Conversely, M1+M2 hybrid
// (without M3) is illegal but does NOT clone. So we gate cloning on
// this predicate and leave the BG-skip path on isIllegalModeRegs.
// --------------------------------------------------------------------------
bool TMS9918::isCloningActive(const uint8_t* regs, ChipType chip)
{
    // Toshiba clones (T7937A, T6950) factory-fixed the address routing
    // that produces sprite cloning — they NEVER clone, regardless of
    // R0/R4 (per meisei vdp.c:592 `!toshiba` guard). Yamaha V9938+
    // also fixed the addressing but POM1 doesn't currently model V99x8.
    if (chip == ChipType::T7937A || chip == ChipType::T6950) return false;

    // Meisei vdp.c:571 + 592 conditions combined. Cloning fires when:
    //   - M1 (R1 bit 4) is NOT set — sprite engine is gated on `~mode&1`
    //     in meisei, so cloning (part of sprite preprocessing) is
    //     inactive in any text-derived mode.
    //   - M3 (R0 bit 1) IS set
    //   - R4 bits 0-1 are NOT both set (`(R4 & 3) != 3`)
    return !(regs[1] & 0x10)             // M1 = 0
        && (regs[0] & 0x02)               // M3 = 1
        && ((regs[4] & 0x03) != 0x03);    // R4 condition
}

// --------------------------------------------------------------------------
// inHBlank — port of openMSX VDP::getHR (VDP.hh:948-961). Returns true
// when the simulated raster is currently in horizontal blanking.
// Constants for TMS9918A NTSC, derived from openMSX VDP.hh:
//   getLeftSprites = 100 + 102 + 56 + (hAdjust−7)·4 + (text? 24 : 0)
//                  = 258 (graphics) | 282 (text)   when hAdjust=7 (MSX1 default)
//   getRightBorder = getLeftSprites + (text? 960 : 1024)
// HBlank window length: 404 ticks text mode, 312 ticks graphics mode.
// All in 21.477 MHz VDP ticks; 21 ticks per 1.022 MHz 6502 cycle.
// --------------------------------------------------------------------------
bool TMS9918::inHBlank() const
{
    constexpr int kLeftSpritesText = 282;
    constexpr int kLeftSpritesGfx  = 258;

    const bool textMode  = (regs[1] & 0x10) != 0;
    const int  rightBdr  = (textMode ? kLeftSpritesText : kLeftSpritesGfx)
                         + (textMode ? 960 : 1024);
    const int  hblankLen = textMode ? kHBlankLenText : kHBlankLenGfx;

    const int ticksThisFrame = frameCycleCounter * kVdpTicksPerCpuCycle;
    const int ticksThisLine  = ticksThisFrame % kVdpTicksPerLine;

    return (ticksThisLine + kVdpTicksPerLine - rightBdr) % kVdpTicksPerLine < hblankLen;
}

// --------------------------------------------------------------------------
// renderCloneSpritesLineRaw — Bug N°8 cloning, port of meisei vdp.c
// lines 591-670 ("the only known correct implementation", per hap's
// commit message — tested side-by-side against real MSX1 silicon with
// all sprite-Y / addressmask combinations).
//
// The chip splits scan-line space into 4 blocks of 64 lines:
//   block 0 (lines   0.. 63): no cloning — sprites 0..7 are
//                              preprocessed in HBlank and unaffected
//   block 1 (lines  64..127): cloning IF R4 bit 0 = 0
//   block 2 (lines 128..191): cloning IF R4 bit 1 = 0
//   block 3 (lines 192..255): always cloning (off-screen for POM1's
//                              192-line render, so we omit it)
// Only sprite slots 8..31 are affected. The Y address fetched from
// the SAT for each cloned slot is mangled by `clonemask[6]` according
// to the per-block table; the sprite then renders at the resulting
// y-offset within the line, even though its real SAT-Y would put it
// elsewhere on the frame.
//
// POM1 caveats:
//   - kScreenHeight = 192 → block 3 (192..255) never reaches us, so
//     we don't emit those clones (they would correspond to "wrap"
//     sprites placed at Y_attr 192..254 displaying at the top of the
//     screen — meisei handles this via the wrap math; for POM1's
//     visible 0..191 frame, block 0..2 covers all the visible cases).
//   - Thermal drift (clones fading after the chip warms up) is
//     not modelled.
// --------------------------------------------------------------------------
void TMS9918::renderCloneSpritesLineRaw(int line, uint32_t* lineBuf,
                                        const uint8_t* vram, const uint8_t* regs,
                                        uint16_t vramMask)
{
    // Per-block clonemask setup (meisei vdp.c:613-658).
    // Defaults: cm[0]/cm[3] = 0xFF (full line mask), cm[1..2,4..5] = 0
    //           → yc = (line & 0xFF) - ((y ^ 0) | 0) = line - y (no clone)
    uint8_t cm[6] = {0xFF, 0, 0, 0xFF, 0, 0};
    bool active = false;

    if (line >= 128 && line <= 191) {
        // Block 2: cloning iff R4 bit 1 = 0
        if (!(regs[4] & 0x02)) {
            cm[1] = cm[4] = 0x80;
            cm[5] = static_cast<uint8_t>((regs[3] << 1) & 0x80);
            active = true;
        }
    } else if (line >= 64 && line <= 127) {
        // Block 1: cloning iff R4 bit 0 = 0
        if (!(regs[4] & 0x01)) {
            cm[0] = 0x3F;
            cm[5] = static_cast<uint8_t>((~regs[3] << 1) & 0x40);
            active = true;
        }
    }
    // line < 64 (block 0) or line > 191 (block 3 — off-screen for POM1)
    // → no clone path on this line.

    if (!active) return;

    const uint16_t sprAttrBase    = static_cast<uint16_t>(regs[5] & 0x7F) << 7;
    const uint16_t sprPatternBase = static_cast<uint16_t>(regs[6] & 0x07) << 11;
    const bool doubleSize = (regs[1] & 0x02) != 0;
    const bool magnified  = (regs[1] & 0x01) != 0;
    const int  sprPixelSize = doubleSize ? 16 : 8;
    const int  mag          = magnified  ? 2  : 1;
    const int  spriteH      = sprPixelSize * mag;

    // Walk SAT — only slots 8..31 are subject to cloning. Slots 0..7
    // were preprocessed in HBlank and are rendered normally by
    // renderSpritesLineRaw. We still respect the chain terminator
    // at any earlier slot (Y=$D0).
    for (int i = 0; i < 32; ++i) {
        const uint16_t attrAddr = (sprAttrBase + i * 4) & vramMask;
        const uint8_t  yRaw     = vram[attrAddr];
        if (yRaw == 0xD0) break;
        if (i < 8) continue;        // slots 0..7 not affected by cloning

        // meisei yc formula (vdp.c:667-668):
        //   yc = (line & cm[0]) - ((y ^ cm[1]) | cm[2])
        //   if yc out of bounds, retry with cm[3..5]
        const uint8_t y = yRaw;
        int yc = static_cast<int>(static_cast<uint8_t>((line & cm[0]) -
                                  ((y ^ cm[1]) | cm[2])));
        if (yc >= spriteH) {
            yc = static_cast<int>(static_cast<uint8_t>((line & cm[3]) -
                                  ((y ^ cm[4]) | cm[5])));
        }
        if (yc < 0 || yc >= spriteH) continue;

        // Standard sprite render at this polluted yc — same path as
        // renderSpritesLineRaw, just with yc in place of (line - y).
        const uint8_t color = vram[(attrAddr + 3) & vramMask];
        if ((color & 0x0F) == 0) continue;     // transparent
        const ImU32 sprColor = kPalette[color & 0x0F];
        int x = vram[(attrAddr + 1) & vramMask];
        if (color & 0x80) x -= 32;
        uint8_t patName = vram[(attrAddr + 2) & vramMask];
        if (doubleSize) patName &= 0xFC;

        const int rowInSpr = yc / mag;
        const int halves = doubleSize ? 2 : 1;
        for (int half = 0; half < halves; ++half) {
            uint16_t patAddr;
            if (!doubleSize) {
                patAddr = (sprPatternBase + static_cast<uint16_t>(patName) * 8 + rowInSpr) & vramMask;
            } else {
                const int quadrant = (rowInSpr < 8) ? half * 2 : half * 2 + 1;
                patAddr = (sprPatternBase + static_cast<uint16_t>(patName + quadrant) * 8 + (rowInSpr & 7)) & vramMask;
            }
            const uint8_t patByte = vram[patAddr];
            for (int bit = 0; bit < 8; ++bit) {
                if (!(patByte & (0x80 >> bit))) continue;
                const int screenX = x + (half * 8 + bit) * mag;
                for (int mx = 0; mx < mag; ++mx) {
                    const int sx = screenX + mx;
                    if (sx < 0 || sx >= kScreenWidth) continue;
                    lineBuf[sx] = sprColor;
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// renderActiveLine — instance method, paints one active scanline of the
// live framebuffer (offset row = line + kBorderTop). Uses LIVE state so
// mid-frame R7/R1/VRAM changes are silicon-correctly progressive.
// --------------------------------------------------------------------------
void TMS9918::renderActiveLine(int line)
{
    if (line < 0 || line >= kScreenHeight) return;

    const uint8_t backdropIdx = regs[7] & 0x0F;
    const ImU32   backdrop    = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];

    uint32_t lineBuf[kScreenWidth];
    for (int i = 0; i < kScreenWidth; ++i) lineBuf[i] = backdrop;

    const bool blank = (regs[1] & 0x40) == 0;
    if (!blank) {
        const bool m1 = (regs[1] & 0x10) != 0;
        const bool m2 = (regs[1] & 0x08) != 0;
        const bool m3 = (regs[0] & 0x02) != 0;
        const uint16_t mask = liveVramMask();

        // meisei vdp.c:419 mode encoding (port verbatim):
        //   bit 0 = M1 (R1 bit 4)
        //   bit 1 = M3 (R0 bit 1)
        //   bit 2 = M2 (R1 bit 3)
        int mode = (m1 ? 1 : 0) | (m3 ? 2 : 0) | (m2 ? 4 : 0);

        // Hybrid M3 + (M1 or M2): chip silently clears M3 internally
        // (meisei vdp.c:443-444). Mode falls back to whatever M1/M2
        // selects:  M1+M3 → text;  M3+M2 → multicolor;  all → text+M2
        // (= mode 5, the "vertical bars" glitch).
        if ((mode & 2) && (mode & 5)) mode ^= 2;

        switch (mode) {
            case 0: renderGfxILineRaw      (line, lineBuf, vram.data(), regs.data(), mask, backdrop); break;
            case 1: renderTextLineRaw      (line, lineBuf, vram.data(), regs.data(), mask, backdrop); break;
            case 2: renderGfxIILineRaw     (line, lineBuf, vram.data(), regs.data(), mask, backdrop); break;
            case 4: renderMulticolorLineRaw(line, lineBuf, vram.data(), regs.data(), mask, backdrop); break;
            case 5: renderTextBarsLineRaw  (line, lineBuf, regs.data(), backdrop); break;
            // No other modes possible after the XOR rule:
            //   3 → 1 (text), 6 → 4 (multicolor), 7 → 5 (text+bars).
            default: break;
        }

        // Sprite engine runs ONLY when M1 is clear — text mode
        // (mode 1 or 5) disables sprite scanning on silicon
        // (meisei vdp.c:571 `if (~mode&1)`). Cloning is part of
        // the sprite engine and inherits the same gate.
        if (!m1) {
            renderSpritesLineRaw(line, lineBuf, vram.data(), regs.data(), mask);
            // Bug N°8 cloning (meisei vdp.c:592 condition).
            if (isCloningActive(regs.data(), chipType)) {
                renderCloneSpritesLineRaw(line, lineBuf, vram.data(), regs.data(), mask);
            }
        }
    }

    // Blit the 256-pixel active line into framebuffer at row (line + kBorderTop),
    // cols [kBorderLeft, kBorderLeft + 256).
    const int dstRow = line + kBorderTop;
    std::memcpy(&framebuffer[dstRow * kFullWidth + kBorderLeft],
                lineBuf, kScreenWidth * sizeof(uint32_t));
    paintLeftRightBorderForActiveLine(line);
    snapshotDirty = true;
}

void TMS9918::paintLeftRightBorderForActiveLine(int line)
{
    const uint8_t backdropIdx = regs[7] & 0x0F;
    const ImU32   border      = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];
    const int     dstRow      = line + kBorderTop;
    uint32_t* row = &framebuffer[dstRow * kFullWidth];
    for (int x = 0; x < kBorderLeft; ++x)                                   row[x] = border;
    for (int x = kBorderLeft + kScreenWidth; x < kFullWidth; ++x)            row[x] = border;
}

void TMS9918::paintTopBorder()
{
    const uint8_t backdropIdx = regs[7] & 0x0F;
    const ImU32   border      = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];
    for (int row = 0; row < kBorderTop; ++row)
        for (int x = 0; x < kFullWidth; ++x)
            framebuffer[row * kFullWidth + x] = border;
    snapshotDirty = true;
}

void TMS9918::paintBottomBorder()
{
    const uint8_t backdropIdx = regs[7] & 0x0F;
    const ImU32   border      = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];
    for (int row = kBorderTop + kScreenHeight; row < kFullHeight; ++row)
        for (int x = 0; x < kFullWidth; ++x)
            framebuffer[row * kFullWidth + x] = border;
    snapshotDirty = true;
}

// Repaint the WHOLE framebuffer from the current VRAM + regs in one shot.
// The progressive raster builds the image scanline-by-scanline only while the
// CPU runs; after a snapshot / rewind restore (often paused) the framebuffer
// would otherwise keep the pre-restore image. renderActiveLine already paints
// the per-line left/right borders, so we just add the top/bottom bands.
void TMS9918::rebuildFramebufferFromVram()
{
    paintTopBorder();
    for (int line = 0; line < kScreenHeight; ++line)
        renderActiveLine(line);
    paintBottomBorder();
    snapshotDirty = true;
}

// --------------------------------------------------------------------------
// Graphics Mode I — 32x24 tiles, 256 patterns, 32 color groups
// --------------------------------------------------------------------------
void TMS9918::renderGraphicsI(uint32_t* pixels, const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase    = (uint16_t)(s.regs[2] & 0x0F) << 10;
    uint16_t colorBase   = (uint16_t) s.regs[3] << 6;
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x07) << 11;
    const uint16_t mask = vramMaskForRegs(s.regs.data(), s.siliconStrictMode);

    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 32; col++) {
            uint8_t name = s.vram[(nameBase + row * 32 + col) & mask];

            uint8_t colorByte = s.vram[(colorBase + (name >> 3)) & mask];
            uint8_t fgIdx = (colorByte >> 4) & 0x0F;
            uint8_t bgIdx =  colorByte       & 0x0F;
            ImU32 fg = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
            ImU32 bg = (bgIdx == 0) ? backdrop : kPalette[bgIdx];

            uint16_t patAddr = (patternBase + (uint16_t)name * 8) & mask;

            for (int line = 0; line < 8; line++) {
                uint8_t pat = s.vram[(patAddr + line) & mask];
                int py = row * 8 + line;
                for (int bit = 0; bit < 8; bit++) {
                    ImU32 color = (pat & (0x80 >> bit)) ? fg : bg;
                    if (color != backdrop)
                        pixels[py * kScreenWidth + (col * 8 + bit)] = color;
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Graphics Mode II — full bitmap, per-row colors
// --------------------------------------------------------------------------
void TMS9918::renderGraphicsII(uint32_t* pixels, const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase = (uint16_t)(s.regs[2] & 0x0F) << 10;
    const uint16_t mask = vramMaskForRegs(s.regs.data(), s.siliconStrictMode);

    uint16_t colorBase   = (uint16_t)(s.regs[3] & 0x80) << 6;
    uint16_t colorMask   = ((uint16_t)(s.regs[3] & 0x7F) << 6) | 0x003F;
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x04) << 11;
    uint16_t patternMask = ((uint16_t)(s.regs[4] & 0x03) << 11) | 0x07FF;

    for (int row = 0; row < 24; row++) {
        int section = row / 8;
        for (int col = 0; col < 32; col++) {
            uint8_t name = s.vram[(nameBase + row * 32 + col) & mask];

            uint16_t charOffset = (uint16_t)(section * 256 + name) * 8;

            for (int line = 0; line < 8; line++) {
                uint16_t offset   = charOffset + line;
                uint16_t patAddr  = patternBase + (offset & patternMask);
                uint16_t colAddr  = colorBase   + (offset & colorMask);

                uint8_t pat       = s.vram[patAddr & mask];
                uint8_t colorByte = s.vram[colAddr & mask];

                uint8_t fgIdx = (colorByte >> 4) & 0x0F;
                uint8_t bgIdx =  colorByte       & 0x0F;
                ImU32 fg = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
                ImU32 bg = (bgIdx == 0) ? backdrop : kPalette[bgIdx];

                int py = row * 8 + line;
                for (int bit = 0; bit < 8; bit++) {
                    ImU32 color = (pat & (0x80 >> bit)) ? fg : bg;
                    if (color != backdrop)
                        pixels[py * kScreenWidth + (col * 8 + bit)] = color;
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Text Mode — 40x24 characters, 6-pixel wide glyphs
// --------------------------------------------------------------------------
void TMS9918::renderText(uint32_t* pixels, const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase    = (uint16_t)(s.regs[2] & 0x0F) << 10;
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x07) << 11;
    const uint16_t mask = vramMaskForRegs(s.regs.data(), s.siliconStrictMode);

    uint8_t fgIdx = (s.regs[7] >> 4) & 0x0F;
    ImU32 fg = (fgIdx == 0) ? backdrop : kPalette[fgIdx];

    // Text mode: 240 pixels wide, centered in 256 (8-pixel border each side)
    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 40; col++) {
            uint8_t name = s.vram[(nameBase + row * 40 + col) & mask];
            uint16_t patAddr = (patternBase + (uint16_t)name * 8) & mask;

            for (int line = 0; line < 8; line++) {
                uint8_t pat = s.vram[(patAddr + line) & mask];
                int py = row * 8 + line;
                for (int bit = 0; bit < 6; bit++) {
                    if (pat & (0x80 >> bit))
                        pixels[py * kScreenWidth + (8 + col * 6 + bit)] = fg;
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Multicolor Mode — 64x48 color blocks
// --------------------------------------------------------------------------
void TMS9918::renderMulticolor(uint32_t* pixels, const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase    = (uint16_t)(s.regs[2] & 0x0F) << 10;
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x07) << 11;
    const uint16_t mask = vramMaskForRegs(s.regs.data(), s.siliconStrictMode);

    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 32; col++) {
            uint8_t name = s.vram[(nameBase + row * 32 + col) & mask];

            int patRow = (row % 4) * 2;
            uint16_t patAddr = (patternBase + (uint16_t)name * 8 + patRow) & mask;

            for (int subRow = 0; subRow < 2; subRow++) {
                uint8_t colorByte = s.vram[(patAddr + subRow) & mask];
                uint8_t leftIdx  = (colorByte >> 4) & 0x0F;
                uint8_t rightIdx =  colorByte       & 0x0F;

                int baseY = row * 8 + subRow * 4;
                int baseX = col * 8;

                if (leftIdx != 0) {
                    ImU32 lc = kPalette[leftIdx];
                    for (int dy = 0; dy < 4; dy++)
                        for (int dx = 0; dx < 4; dx++)
                            pixels[(baseY + dy) * kScreenWidth + (baseX + dx)] = lc;
                }
                if (rightIdx != 0) {
                    ImU32 rc = kPalette[rightIdx];
                    for (int dy = 0; dy < 4; dy++)
                        for (int dx = 0; dx < 4; dx++)
                            pixels[(baseY + dy) * kScreenWidth + (baseX + 4 + dx)] = rc;
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Sprites — per-scanline scan with 4-sprites-per-line hardware drop.
//
// Real TMS9918A scans the SAT against the active scanline; only the first
// four sprites whose vertical band covers that line are drawn. The rest
// vanish on that line (the source of authentic flicker). renderSprites
// implements that visual rule. The status flags (collision bit 5, 5S bit 6
// and index) are computed independently in scanSpritesForStatus on the
// emulation thread (see advanceCycles).
//
// Within a scanline's visible-sprite list, sprite 0 has the highest priority
// (drawn last, on top), so we emit in reverse-priority order.
// --------------------------------------------------------------------------
void TMS9918::renderSprites(uint32_t* pixels, const Snapshot& s)
{
    uint16_t sprAttrBase    = (uint16_t)(s.regs[5] & 0x7F) << 7;
    uint16_t sprPatternBase = (uint16_t)(s.regs[6] & 0x07) << 11;
    const uint16_t mask = vramMaskForRegs(s.regs.data(), s.siliconStrictMode);

    bool doubleSize = (s.regs[1] & 0x02) != 0;
    bool magnified  = (s.regs[1] & 0x01) != 0;

    int sprPixelSize = doubleSize ? 16 : 8;
    int mag = magnified ? 2 : 1;
    int spriteH = sprPixelSize * mag;

    struct SpriteInfo { int y, x; uint8_t name, color; };
    SpriteInfo sprites[32];
    int spriteCount = 0;

    for (int i = 0; i < 32; i++) {
        uint16_t attrAddr = (sprAttrBase + i * 4) & mask;
        uint8_t yRaw = s.vram[attrAddr];
        if (yRaw == 0xD0) break;

        int y = (int)yRaw - ((yRaw > 0xD0) ? 256 : 0) + 1;
        int x = s.vram[(attrAddr + 1) & mask];
        uint8_t name  = s.vram[(attrAddr + 2) & mask];
        uint8_t color = s.vram[(attrAddr + 3) & mask];
        if (color & 0x80) x -= 32;
        sprites[spriteCount++] = { y, x, name, (uint8_t)(color & 0x0F) };
    }

    for (int sy = 0; sy < kScreenHeight; sy++) {
        // Collect at most 4 sprites whose vertical band contains this scanline.
        int visible[4]; int nVisible = 0;
        for (int i = 0; i < spriteCount && nVisible < 4; i++) {
            const auto& spr = sprites[i];
            if (sy < spr.y || sy >= spr.y + spriteH) continue;
            visible[nVisible++] = i;
        }

        // Render in reverse-priority order so sprite 0 paints last (on top).
        for (int k = nVisible - 1; k >= 0; k--) {
            const auto& spr = sprites[visible[k]];
            if (spr.color == 0) continue; // visually transparent — but still in slot count
            ImU32 sprColor = kPalette[spr.color];

            uint8_t patName = spr.name;
            if (doubleSize) patName &= 0xFC;

            int rowInSpr = (sy - spr.y) / mag; // 0..7 or 0..15
            // Fetch left half (8 bits), then right half if 16-wide.
            int halves = doubleSize ? 2 : 1;
            for (int half = 0; half < halves; half++) {
                uint16_t addr;
                if (!doubleSize) {
                    addr = (sprPatternBase + (uint16_t)patName * 8 + rowInSpr) & mask;
                } else {
                    int quadrant = (rowInSpr < 8) ? half * 2 : half * 2 + 1;
                    addr = (sprPatternBase + (uint16_t)(patName + quadrant) * 8 + (rowInSpr & 7)) & mask;
                }
                uint8_t patByte = s.vram[addr];
                for (int bit = 0; bit < 8; bit++) {
                    if (!(patByte & (0x80 >> bit))) continue;
                    int screenX = spr.x + (half * 8 + bit) * mag;
                    for (int mx = 0; mx < mag; mx++) {
                        int sx = screenX + mx;
                        if (sx < 0 || sx >= kScreenWidth) continue;
                        pixels[sy * kScreenWidth + sx] = sprColor;
                    }
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// scanSpritesForLine — per-scanline sprite-scan helper. Updates sticky
// status bits silicon-correctly as the simulated raster crosses each
// active line:
//   bit 5 ($20) — sprite-sprite collision (ANY two opaque pattern bits
//                 overlap, even when one or both sprites have color = 0).
//                 Collision detection extends into the overscan zone
//                 [-32, 288) to catch early-clock sprites colliding off
//                 the visible screen (cf. dev/SILICONBUGS.md Bug N°4).
//   bit 6 ($40) — 5th-sprite-on-scanline overflow.
//   bits 0..4   — when bit 6 is latched, the SAT index of the 5th sprite.
//                 Otherwise, the index of the last sprite the chip walked
//                 on this line (the Y=$D0 terminator entry, or sprite 31
//                 if no terminator). Cf. Bug N°6.
//
// Source-of-truth: openMSX SpriteChecker::checkSprites1 (line-major variant
// — POM1 walks the SAT once per scanline, openMSX's sprite-major loop
// optimization isn't needed at our frame rate).
// --------------------------------------------------------------------------
void TMS9918::scanSpritesForLine(int line)
{
    if (line < 0 || line >= kScreenHeight) return;
    if ((regs[1] & 0x40) == 0) {
        // Display blanked: silicon doesn't sprite-scan, but DOES set
        // status bits 0..4 to $1F (per meisei vdp.c:437,512 — the
        // last-walked-slot index defaults to 31 in blank). POM1
        // previously left bits 0..4 untouched, which kept stale
        // values. (Fixed 2026-05.)
        statusReg = (uint8_t)((statusReg & 0xE0) | 0x1F);
        return;
    }

    const uint16_t sprAttrBase    = (uint16_t)(regs[5] & 0x7F) << 7;
    const uint16_t sprPatternBase = (uint16_t)(regs[6] & 0x07) << 11;
    const uint16_t vramMask       = vramMaskForRegs(regs.data(), siliconStrictMode);
    const bool doubleSize = (regs[1] & 0x02) != 0;
    const bool magnified  = (regs[1] & 0x01) != 0;
    const int  sprPixelSize = doubleSize ? 16 : 8;
    const int  mag          = magnified  ? 2  : 1;
    const int  spriteH      = sprPixelSize * mag;

    // Collision mask covers the VISIBLE area [0, 256) only — per
    // openMSX (`SpriteChecker.cc:187-191`): "Sprites that are partially
    // off-screen position can collide, but only on the in-screen
    // pixels. In other words: sprites cannot collide in the left or
    // right border, only in the visible screen area."
    // meisei vdp.c:587-589 implements the same semantics via guard
    // bytes set to 0x80 (no collision bit) on either side of the
    // 256-byte sprite line buffer.
    // (Was [-32, 288) per Nouspikel before mai 2026 — corrected
    // to match openMSX which is the silicon source of truth.)
    constexpr int kMaskBytes = kScreenWidth / 8;  // 256 / 8 = 32 bytes
    uint8_t mask[kMaskBytes];
    bool maskInUse = false;

    bool fiveAlreadyLatched      = (statusReg & 0x40) != 0;
    bool collisionAlreadyLatched = (statusReg & 0x20) != 0;
    int  visible = 0;
    int  lastSpriteIdx = 0;                       // last SAT slot walked this line
    bool sawTerminator = false;

    for (int i = 0; i < 32; ++i) {
        const uint16_t attrAddr = (sprAttrBase + i * 4) & vramMask;
        const uint8_t  yRaw     = vram[attrAddr];
        if (yRaw == 0xD0) { lastSpriteIdx = i; sawTerminator = true; break; }

        const int y = (int)yRaw - ((yRaw > 0xD0) ? 256 : 0) + 1;
        if (line < y || line >= y + spriteH) { lastSpriteIdx = i; continue; }

        // Visible on this line.
        if (visible == 4) {
            if (!fiveAlreadyLatched && (statusReg & 0x80) == 0) {
                // Per TMS9918.pdf: 5S detection only when F flag is zero.
                statusReg = (uint8_t)((statusReg & 0xE0) | (i & 0x1F));
                statusReg |= 0x40;
                fiveAlreadyLatched = true;
            }
            // 5th+ sprites are dropped from rendering and don't contribute
            // to collision detection.
            lastSpriteIdx = i;
            break;
        }
        ++visible;
        lastSpriteIdx = i;

        // Skip per-pixel work when both flags are sticky for the rest of
        // the frame — no further state can change.
        if (collisionAlreadyLatched && fiveAlreadyLatched) continue;

        uint8_t patName = vram[(attrAddr + 2) & vramMask];
        const uint8_t color = vram[(attrAddr + 3) & vramMask];
        const bool earlyClock = (color & 0x80) != 0;
        int x = vram[(attrAddr + 1) & vramMask];
        if (earlyClock) x -= 32;

        if (doubleSize) patName &= 0xFC;
        const int rowInSpr = (line - y) / mag;

        uint8_t patLeft = 0, patRight = 0;
        if (!doubleSize) {
            patLeft = vram[(sprPatternBase + (uint16_t)patName * 8 + rowInSpr) & vramMask];
        } else {
            const int qLeft  = (rowInSpr < 8) ? 0 : 1;
            const int qRight = (rowInSpr < 8) ? 2 : 3;
            patLeft  = vram[(sprPatternBase + (uint16_t)(patName + qLeft)  * 8 + (rowInSpr & 7)) & vramMask];
            patRight = vram[(sprPatternBase + (uint16_t)(patName + qRight) * 8 + (rowInSpr & 7)) & vramMask];
        }

        if (!maskInUse) {
            std::memset(mask, 0, sizeof(mask));
            maskInUse = true;
        }
        const int halves = doubleSize ? 2 : 1;
        for (int half = 0; half < halves; ++half) {
            const uint8_t patByte = (half == 0) ? patLeft : patRight;
            for (int bit = 0; bit < 8; ++bit) {
                if (!(patByte & (0x80 >> bit))) continue;
                const int baseX = x + (half * 8 + bit) * mag;
                for (int mx = 0; mx < mag; ++mx) {
                    const int sx = baseX + mx;
                    // Bug N°4 (corrected mai 2026 to match openMSX):
                    // collisions only count in visible [0, 256). Pixels
                    // in the left/right border (sprites partially off-
                    // screen) are silently dropped from the mask.
                    if (sx < 0 || sx >= kScreenWidth) continue;
                    const uint8_t bm = (uint8_t)(0x80 >> (sx & 7));
                    uint8_t& cell = mask[sx >> 3];
                    if (cell & bm) {
                        if (!collisionAlreadyLatched) {
                            statusReg |= 0x20;
                            collisionAlreadyLatched = true;
                        }
                    } else {
                        cell |= bm;
                    }
                }
            }
        }
    }

    // Bug N°6: bits 0..4 reflect the last SAT index walked on this line when
    // 5S is not latched. Silicon-correct: the chip latches whichever sprite
    // index the line-scanner reached last (terminator, sprite 31, or
    // no-match cases). When 5S latches, bits 0..4 stay at the 5th's index.
    if (!fiveAlreadyLatched) {
        statusReg = (uint8_t)((statusReg & 0xE0) | (lastSpriteIdx & 0x1F));
    }
    (void)sawTerminator;                          // documented for future use
}

// --------------------------------------------------------------------------
// scanSpritesForStatus — legacy 1×/frame VBlank-edge fallback. Used only
// when no per-line work happened during the active period (e.g. display
// blanked the whole frame). Walks every line and applies the same logic
// as scanSpritesForLine but in one shot. Idempotent.
// --------------------------------------------------------------------------
void TMS9918::scanSpritesForStatus(const uint8_t* vram, const uint8_t* regs,
                                   bool strict, uint8_t& statusOut)
{
    uint16_t sprAttrBase    = (uint16_t)(regs[5] & 0x7F) << 7;
    uint16_t sprPatternBase = (uint16_t)(regs[6] & 0x07) << 11;
    const uint16_t vramMask = vramMaskForRegs(regs, strict);

    bool doubleSize = (regs[1] & 0x02) != 0;
    bool magnified  = (regs[1] & 0x01) != 0;

    int sprPixelSize = doubleSize ? 16 : 8;
    int mag = magnified ? 2 : 1;
    int spriteH = sprPixelSize * mag;

    struct SpriteInfo { int y, x; uint8_t name; bool earlyClock; };
    SpriteInfo sprites[32];
    int spriteCount = 0;

    for (int i = 0; i < 32; i++) {
        uint16_t attrAddr = (sprAttrBase + i * 4) & vramMask;
        uint8_t yRaw = vram[attrAddr];
        if (yRaw == 0xD0) break;

        int y = (int)yRaw - ((yRaw > 0xD0) ? 256 : 0) + 1;
        int x = vram[(attrAddr + 1) & vramMask];
        uint8_t name  = vram[(attrAddr + 2) & vramMask];
        uint8_t color = vram[(attrAddr + 3) & vramMask];
        bool earlyClock = (color & 0x80) != 0;
        if (earlyClock) x -= 32;
        sprites[spriteCount++] = { y, x, name, earlyClock };
    }

    bool fiveAlreadyLatched = (statusOut & 0x40) != 0;
    bool collisionAlreadyLatched = (statusOut & 0x20) != 0;

    for (int sy = 0; sy < kScreenHeight; sy++) {
        uint8_t mask[32];
        bool maskInUse = false;
        int visible = 0;

        for (int i = 0; i < spriteCount; i++) {
            const auto& spr = sprites[i];
            if (sy < spr.y || sy >= spr.y + spriteH) continue;

            if (visible == 4) {
                if (!fiveAlreadyLatched) {
                    statusOut = (statusOut & 0xE0) | (uint8_t)(i & 0x1F);
                    statusOut |= 0x40;
                    fiveAlreadyLatched = true;
                }
                // 5th+ sprites are dropped; they don't contribute to collision.
                break;
            }
            visible++;

            // Skip per-pixel work when both flags are already sticky for the
            // rest of the frame — index/collision can't change.
            if (collisionAlreadyLatched && fiveAlreadyLatched) continue;

            uint8_t patName = spr.name;
            if (doubleSize) patName &= 0xFC;
            int rowInSpr = (sy - spr.y) / mag;

            // Build a 16-bit pattern row (left half then optional right half).
            uint8_t patLeft = 0, patRight = 0;
            if (!doubleSize) {
                patLeft = vram[(sprPatternBase + (uint16_t)patName * 8 + rowInSpr) & vramMask];
            } else {
                int qLeft  = (rowInSpr < 8) ? 0 : 1;
                int qRight = (rowInSpr < 8) ? 2 : 3;
                patLeft  = vram[(sprPatternBase + (uint16_t)(patName + qLeft)  * 8 + (rowInSpr & 7)) & vramMask];
                patRight = vram[(sprPatternBase + (uint16_t)(patName + qRight) * 8 + (rowInSpr & 7)) & vramMask];
            }

            int halves = doubleSize ? 2 : 1;
            if (!maskInUse) {
                std::memset(mask, 0, sizeof(mask));
                maskInUse = true;
            }
            for (int half = 0; half < halves; half++) {
                uint8_t patByte = (half == 0) ? patLeft : patRight;
                for (int bit = 0; bit < 8; bit++) {
                    if (!(patByte & (0x80 >> bit))) continue;
                    int baseX = spr.x + (half * 8 + bit) * mag;
                    for (int mx = 0; mx < mag; mx++) {
                        int sx = baseX + mx;
                        if (sx < 0 || sx >= kScreenWidth) continue;
                        uint8_t bm = (uint8_t)(0x80 >> (sx & 7));
                        if (mask[sx >> 3] & bm) {
                            if (!collisionAlreadyLatched) {
                                statusOut |= 0x20;
                                collisionAlreadyLatched = true;
                            }
                        } else {
                            mask[sx >> 3] |= bm;
                        }
                    }
                }
            }
        }
    }
}

void TMS9918::serialize(pom1::SnapshotWriter& w) const
{
    w.writeBytes(vram.data(), vram.size());
    w.writeBytes(regs.data(), regs.size());
    w.writeU8 (statusReg);
    w.writeU8 (controlLatch);
    w.writeU8 (latchIsSecond ? 1 : 0);
    w.writeU16(vramAddr);
    w.writeU8 (readAheadBuffer);
    w.writeU32(static_cast<uint32_t>(frameCycleCounter));
    w.writeU32(static_cast<uint32_t>(pendingDrainCycles));
    w.writeU32(static_cast<uint32_t>(lastScanlineProcessed));
    w.writeU32(static_cast<uint32_t>(lastScanlineRendered));
    w.writeU8 (siliconStrictMode ? 1 : 0);
    w.writeU8 (irqStrapped ? 1 : 0);
    w.writeU8 (static_cast<uint8_t>(chipType));
}

void TMS9918::deserialize(pom1::SnapshotReader& r)
{
    r.readBytes(vram.data(), vram.size());
    r.readBytes(regs.data(), regs.size());
    statusReg             = r.readU8();
    controlLatch          = r.readU8();
    latchIsSecond         = r.readU8() != 0;
    vramAddr              = r.readU16();
    readAheadBuffer       = r.readU8();
    frameCycleCounter     = static_cast<int>(r.readU32());
    pendingDrainCycles    = static_cast<int>(r.readU32());
    lastScanlineProcessed = static_cast<int>(r.readU32());
    lastScanlineRendered  = static_cast<int>(r.readU32());
    siliconStrictMode     = r.readU8() != 0;
    irqStrapped           = r.readU8() != 0;
    chipType              = static_cast<ChipType>(r.readU8());
    // The framebuffer was intentionally not snapshotted (derived state, would
    // inflate the .snap by ~300 KB). Rebuild it now from the restored VRAM so a
    // save-state load OR a paused rewind preview shows the correct image
    // immediately — the progressive raster only runs while the CPU executes,
    // which doesn't happen during a rewind scrub.
    rebuildFramebufferFromVram();
}
