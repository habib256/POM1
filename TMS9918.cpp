// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 Video Display Processor emulation
// Implements the P-LAB Apple-1 Graphic Card
// https://p-l4b.github.io/graphic/
//
// Reference: TMS9918A datasheet, nippur72/apple1-videocard-lib

#include "TMS9918.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
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
    vram.fill(0);
    regs.fill(0);
    statusReg       = 0;
    controlLatch    = 0;
    latchIsSecond   = false;
    vramAddr        = 0;
    readAheadBuffer = 0;
    frameCycleCounter = 0;
    pendingDrainCycles = 0;
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
    const bool displayEnabled = (regs[1] & 0x40) != 0;
    if (!displayEnabled)
        return SlotSpan{slotsMsx1ScreenOff.data(), slotsMsx1ScreenOff.size()};
    const bool m1 = (regs[1] & 0x10) != 0;   // text
    const bool m2 = (regs[1] & 0x08) != 0;   // multicolor
    if (m1) return SlotSpan{slotsMsx1Text.data(),  slotsMsx1Text.size()};
    if (m2) return SlotSpan{slotsMsx1Gfx3.data(),  slotsMsx1Gfx3.size()};
    return SlotSpan{slotsMsx1Gfx12.data(), slotsMsx1Gfx12.size()};
}

TMS9918::SlotTableId TMS9918::activeSlotTableId() const
{
    const bool displayEnabled = (regs[1] & 0x40) != 0;
    if (!displayEnabled)              return kSlotTableScreenOff;
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
    if (prevCounter < kActiveDisplayCycles && frameCycleCounter >= kActiveDisplayCycles) {
        // Sprite-engine status scan happens at VBlank start on real silicon
        // too (the chip walks the SAT during vertical retrace), only when
        // display is enabled (R1 bit 6).
        if (regs[1] & 0x40) {
            scanSpritesForStatus(vram.data(), regs.data(), siliconStrictMode, statusReg);
        }
        statusReg |= 0x80;
    }

    // Frame counter rollover at the END of VBlank (== start of next active
    // display). No flag side-effect — the F flag stays sticky-set until
    // software reads $CC01 to clear it.
    if (frameCycleCounter >= kCyclesPerFrame) {
        frameCycleCounter -= kCyclesPerFrame;
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
    // VRAM (16 KB) and register file only move when the card is actually
    // touched by software — a dirty flag avoids a 16 KB memcpy on idle frames.
    if (snapshotDirty) {
        std::memcpy(out.vram.data(), vram.data(), vram.size());
        std::memcpy(out.regs.data(), regs.data(), regs.size());
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
    uint8_t backdropIdx = snap.regs[7] & 0x0F;
    ImU32 backdrop = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];

    for (int i = 0; i < kScreenWidth * kScreenHeight; i++) pixels[i] = backdrop;

    bool blank = (snap.regs[1] & 0x40) == 0;
    if (blank) return;

    bool m1 = (snap.regs[1] & 0x10) != 0; // R1 bit 4
    bool m2 = (snap.regs[1] & 0x08) != 0; // R1 bit 3
    bool m3 = (snap.regs[0] & 0x02) != 0; // R0 bit 1

    if (!m1 && !m2 && !m3) {
        renderGraphicsI(pixels, snap, backdrop);
    } else if (!m1 && !m2 && m3) {
        renderGraphicsII(pixels, snap, backdrop);
    } else if (m1 && !m2 && !m3) {
        renderText(pixels, snap, backdrop);
    } else if (!m1 && m2 && !m3) {
        renderMulticolor(pixels, snap, backdrop);
    }
    // else: undefined mode combination — backdrop only

    if (!m1) {
        renderSprites(pixels, snap);
    }
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
// scanSpritesForStatus — emulation-thread helper. Walks the live SAT against
// every scanline and updates sticky status bits:
//   bit 5 ($20) — sprite-sprite collision (ANY two opaque pattern bits
//                 overlap, even when one or both sprites have color = 0).
//   bit 6 ($40) — 5th-sprite-on-scanline overflow. Bits 0..4 latch the SAT
//                 index of the offending sprite.
// Both bits are sticky until readControl() clears them via the ~0xE0 mask.
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
