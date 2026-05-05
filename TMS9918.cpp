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
    cyclesSinceIoAccess = 1000000;
    droppedWrites = 0;
    droppedWriteTraceCount = 0;
    snapshotDirty   = true;
}

void TMS9918::setSiliconStrictMode(bool enabled)
{
    siliconStrictMode = enabled;
    cyclesSinceIoAccess = 1000000;
    droppedWrites = 0;
    droppedWriteTraceCount = 0;     // restart the counter on toggle so users see new drops only
}

uint16_t TMS9918::vramMaskForRegs(const uint8_t* regs, bool strict)
{
    return (strict && (regs[1] & 0x80) == 0) ? 0x0FFF : 0x3FFF;
}

uint16_t TMS9918::liveVramMask() const
{
    return vramMaskForRegs(regs.data(), siliconStrictMode);
}

int TMS9918::requiredAccessCycles() const
{
    if (!siliconStrictMode) return 0;
    // Display blanked (R1 bit 6 = 0) OR beam currently in VBlank: VRAM is
    // free, only the ~2 µs preparation delay applies. The VBlank check uses
    // frameCycleCounter (the physical beam position), NOT statusReg bit 7
    // — bit 7 is sticky-until-readControl, so a program that never polls
    // $CC01 (Galaga, for one) keeps it set forever and would otherwise see
    // a permanently-relaxed window across the whole active display.
    if ((regs[1] & 0x40) == 0 || frameCycleCounter >= kActiveDisplayCycles) return 2;

    // Hardened silicon-strict thresholds (cf. dev/SILICONBUGS.md Bug N°1).
    // The "1 slot per N VDP cycles" model gives the *typical* worst case at
    // ideal CPU↔VDP phase alignment; the *worst-worst* case adds one full
    // slot wait (CPU access arrives just after a slot consumed) plus the
    // 4-cycle STA latch + ~2c bus turnaround on warm NMOS. We dimension each
    // mode so passing POM1 strict guarantees silicon — the contract is:
    //   gap ≥ 1 full VDP slot period + STA + phase margin.
    // May 2026: Mode I+sprites bumped 16→24c after Galaga/LOGO sprite-table
    // overflows. Patcher emits JSR tms9918_pad24 (24c) to match. Other modes
    // keep their 4/6/6c thresholds — their slot periods are physically shorter.
    const bool textMode       = (regs[1] & 0x10) != 0;
    const bool multicolorMode = (regs[1] & 0x08) != 0;
    if (textMode) return 4;          // 1 slot/3 VDP cycles, +1c phase margin
    if (multicolorMode) return 6;    // 1 slot/4 VDP cycles, +2c phase margin

    // Mode I / Mode II : check sprite-active terminator.
    const uint16_t satBase = static_cast<uint16_t>(regs[5] & 0x7F) << 7;
    const uint16_t mask = liveVramMask();
    bool spritesActive = false;
    for (int i = 0; i < 32; ++i) {
        const uint8_t y = vram[(satBase + i * 4) & mask];
        if (y == 0xD0) break;
        spritesActive = true;
        break;
    }
    if (!spritesActive) return 6;    // 1 slot/6 VDP cycles when SAT[0]=$D0
    return 24;                        // hardened: 1 slot/16 VDP cycles + STA + 8c phase margin
                                      // (was 16c — Galaga sprite tables and LOGO turtle redraws
                                      //  overflowed at 16c under unfavourable phase, May 2026 bump)
}

bool TMS9918::canAcceptAccess() const
{
    return !siliconStrictMode || cyclesSinceIoAccess >= requiredAccessCycles();
}

void TMS9918::noteAcceptedAccess()
{
    cyclesSinceIoAccess = 0;
}

// --------------------------------------------------------------------------
// I/O — Data port ($CC00)
// --------------------------------------------------------------------------
void TMS9918::writeData(uint8_t value)
{
    latchIsSecond = false;                    // data-port access resets latch state
    if (!canAcceptAccess()) {
        ++droppedWrites;
        if (droppedWriteTraceCount < 60) {
            std::fprintf(stderr,
                "[TMS9918 DROP #%llu] writeData val=%02X vramAddr=%04X gap=%d "
                "required=%d frameCycle=%d R1=%02X\n",
                (unsigned long long)droppedWrites, value, vramAddr,
                cyclesSinceIoAccess, requiredAccessCycles(),
                frameCycleCounter, regs[1]);
            ++droppedWriteTraceCount;
        }
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
        ++droppedWrites;
        if (droppedWriteTraceCount < 60) {
            std::fprintf(stderr,
                "[TMS9918 DROP #%llu] writeControl val=%02X latch2=%d gap=%d "
                "required=%d frameCycle=%d R1=%02X\n",
                (unsigned long long)droppedWrites, value, latchIsSecond,
                cyclesSinceIoAccess, requiredAccessCycles(),
                frameCycleCounter, regs[1]);
            ++droppedWriteTraceCount;
        }
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
    cyclesSinceIoAccess = std::min(cyclesSinceIoAccess + cycles, 1000000);
    frameCycleCounter += cycles;
    if (frameCycleCounter >= kCyclesPerFrame) {
        frameCycleCounter -= kCyclesPerFrame;
        // Update sticky sprite-engine flags (collision bit 5, 5S bit 6 + index)
        // before raising VBlank — only when display is enabled (R1 bit 6).
        if (regs[1] & 0x40) {
            scanSpritesForStatus(vram.data(), regs.data(), siliconStrictMode, statusReg);
        }
        statusReg |= 0x80; // set frame interrupt flag
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
