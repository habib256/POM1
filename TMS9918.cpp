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
}

// --------------------------------------------------------------------------
// I/O — Data port ($CC00)
// --------------------------------------------------------------------------
void TMS9918::writeData(uint8_t value)
{
    latchIsSecond = false;                    // data-port access resets latch state
    vram[vramAddr & 0x3FFF] = value;
    readAheadBuffer = value;
    vramAddr = (vramAddr + 1) & 0x3FFF;
}

uint8_t TMS9918::readData()
{
    latchIsSecond = false;                    // data-port access resets latch state
    uint8_t result = readAheadBuffer;
    readAheadBuffer = vram[vramAddr & 0x3FFF];
    vramAddr = (vramAddr + 1) & 0x3FFF;
    return result;
}

// --------------------------------------------------------------------------
// I/O — Control port ($CC01)
// --------------------------------------------------------------------------
void TMS9918::writeControl(uint8_t value)
{
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
        readAheadBuffer = vram[vramAddr & 0x3FFF];
        vramAddr = (vramAddr + 1) & 0x3FFF;
    }
    else if (cmd == 0x40) {
        // Set VRAM write address
        vramAddr = ((uint16_t)(value & 0x3F) << 8) | controlLatch;
    }
    else if (cmd == 0x80) {
        // Write to register
        uint8_t regNum = value & 0x07;
        regs[regNum] = controlLatch;
    }
    // cmd == 0xC0 is undefined on TMS9918, ignored
}

uint8_t TMS9918::readControl()
{
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
    frameCycleCounter += cycles;
    if (frameCycleCounter >= kCyclesPerFrame) {
        frameCycleCounter -= kCyclesPerFrame;
        statusReg |= 0x80; // set frame interrupt flag
    }
}

// --------------------------------------------------------------------------
// Snapshot
// --------------------------------------------------------------------------
void TMS9918::copySnapshot(Snapshot& out) const
{
    std::memcpy(out.vram.data(), vram.data(), vram.size());
    std::memcpy(out.regs.data(), regs.data(), regs.size());
    out.statusReg = statusReg;
}

// --------------------------------------------------------------------------
// Top-level render (dispatches to mode-specific renderer)
// --------------------------------------------------------------------------
void TMS9918::render(ImDrawList* drawList, ImVec2 origin, float pixelScale,
                     const Snapshot& snap)
{
    // Backdrop color = low nibble of register 7
    uint8_t backdropIdx = snap.regs[7] & 0x0F;
    ImU32 backdrop = (backdropIdx == 0) ? kPalette[1] : kPalette[backdropIdx];

    // Fill entire area with backdrop
    ImVec2 size(kScreenWidth * pixelScale, kScreenHeight * pixelScale);
    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), backdrop);

    // Determine display mode from M1, M2, M3 bits
    bool m1 = (snap.regs[1] & 0x10) != 0; // R1 bit 4
    bool m2 = (snap.regs[1] & 0x08) != 0; // R1 bit 3
    bool m3 = (snap.regs[0] & 0x02) != 0; // R0 bit 1

    // Check blank bit — if screen is blanked, show only backdrop
    bool blank = (snap.regs[1] & 0x40) == 0;
    if (blank) return;

    if (!m1 && !m2 && !m3) {
        renderGraphicsI(drawList, origin, pixelScale, snap, backdrop);
    } else if (!m1 && !m2 && m3) {
        renderGraphicsII(drawList, origin, pixelScale, snap, backdrop);
    } else if (m1 && !m2 && !m3) {
        renderText(drawList, origin, pixelScale, snap, backdrop);
    } else if (!m1 && m2 && !m3) {
        renderMulticolor(drawList, origin, pixelScale, snap, backdrop);
    }
    // else: undefined mode combination — show backdrop only

    // Render sprites (all modes except Text have sprites)
    if (!m1) {
        renderSprites(drawList, origin, pixelScale, snap);
    }
}

// --------------------------------------------------------------------------
// Graphics Mode I — 32x24 tiles, 256 patterns, 32 color groups
// --------------------------------------------------------------------------
void TMS9918::renderGraphicsI(ImDrawList* dl, ImVec2 org, float ps,
                               const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase    = (uint16_t)(s.regs[2] & 0x0F) << 10;  // x 0x400
    uint16_t colorBase   = (uint16_t) s.regs[3] << 6;            // x 0x40
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x07) << 11;  // x 0x800

    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 32; col++) {
            uint8_t name = s.vram[(nameBase + row * 32 + col) & 0x3FFF];

            // Color: 1 entry per 8 consecutive patterns
            uint8_t colorByte = s.vram[(colorBase + (name >> 3)) & 0x3FFF];
            uint8_t fgIdx = (colorByte >> 4) & 0x0F;
            uint8_t bgIdx =  colorByte       & 0x0F;
            ImU32 fg = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
            ImU32 bg = (bgIdx == 0) ? backdrop : kPalette[bgIdx];

            uint16_t patAddr = (patternBase + (uint16_t)name * 8) & 0x3FFF;

            for (int line = 0; line < 8; line++) {
                uint8_t pat = s.vram[(patAddr + line) & 0x3FFF];
                float py = org.y + (row * 8 + line) * ps;

                for (int bit = 0; bit < 8; bit++) {
                    ImU32 color = (pat & (0x80 >> bit)) ? fg : bg;
                    if (color != backdrop) {
                        float px = org.x + (col * 8 + bit) * ps;
                        drawPixel(dl, px, py, ps, color);
                    }
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Graphics Mode II — full bitmap, per-row colors
// --------------------------------------------------------------------------
void TMS9918::renderGraphicsII(ImDrawList* dl, ImVec2 org, float ps,
                                const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase = (uint16_t)(s.regs[2] & 0x0F) << 10;

    // In Graphics II, R3 and R4 use AND-masking for 3-section addressing.
    // The 13-bit character offset (0-6143) is AND'd with a mask derived from
    // the register. Bits above the masked field pass through, lower bits pass through.
    //
    // Color table: R3 bit 7 selects base (0x0000 or 0x2000).
    //   R3 bits 6-0 mask bits 12-6 of the offset; bits 5-0 pass through.
    uint16_t colorBase = (uint16_t)(s.regs[3] & 0x80) << 6;   // 0x0000 or 0x2000
    uint16_t colorMask = ((uint16_t)(s.regs[3] & 0x7F) << 6) | 0x003F;

    // Pattern table: R4 bit 2 selects base (0x0000 or 0x2000).
    //   R4 bits 1-0 mask bits 12-11 of the offset; bits 10-0 pass through.
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x04) << 11; // 0x0000 or 0x2000
    uint16_t patternMask = ((uint16_t)(s.regs[4] & 0x03) << 11) | 0x07FF;

    for (int row = 0; row < 24; row++) {
        int section = row / 8; // 0, 1, or 2
        for (int col = 0; col < 32; col++) {
            uint8_t name = s.vram[(nameBase + row * 32 + col) & 0x3FFF];

            // 13-bit offset within the section: (section * 256 + name) * 8
            uint16_t charOffset = (uint16_t)(section * 256 + name) * 8;

            for (int line = 0; line < 8; line++) {
                uint16_t offset = charOffset + line;
                uint16_t patAddr  = patternBase + (offset & patternMask);
                uint16_t colAddr  = colorBase   + (offset & colorMask);

                uint8_t pat       = s.vram[patAddr  & 0x3FFF];
                uint8_t colorByte = s.vram[colAddr  & 0x3FFF];

                uint8_t fgIdx = (colorByte >> 4) & 0x0F;
                uint8_t bgIdx =  colorByte       & 0x0F;
                ImU32 fg = (fgIdx == 0) ? backdrop : kPalette[fgIdx];
                ImU32 bg = (bgIdx == 0) ? backdrop : kPalette[bgIdx];

                float py = org.y + (row * 8 + line) * ps;

                for (int bit = 0; bit < 8; bit++) {
                    ImU32 color = (pat & (0x80 >> bit)) ? fg : bg;
                    if (color != backdrop) {
                        float px = org.x + (col * 8 + bit) * ps;
                        drawPixel(dl, px, py, ps, color);
                    }
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Text Mode — 40x24 characters, 6-pixel wide glyphs
// --------------------------------------------------------------------------
void TMS9918::renderText(ImDrawList* dl, ImVec2 org, float ps,
                          const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase    = (uint16_t)(s.regs[2] & 0x0F) << 10;
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x07) << 11;

    uint8_t fgIdx = (s.regs[7] >> 4) & 0x0F;
    ImU32 fg = (fgIdx == 0) ? backdrop : kPalette[fgIdx];

    // Text mode: 240 pixels wide centered in 256 (8-pixel border on each side)
    float borderX = 8.0f * ps;

    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 40; col++) {
            uint8_t name = s.vram[(nameBase + row * 40 + col) & 0x3FFF];
            uint16_t patAddr = (patternBase + (uint16_t)name * 8) & 0x3FFF;

            for (int line = 0; line < 8; line++) {
                uint8_t pat = s.vram[(patAddr + line) & 0x3FFF];
                float py = org.y + (row * 8 + line) * ps;

                // Only top 6 bits are used in text mode
                for (int bit = 0; bit < 6; bit++) {
                    if (pat & (0x80 >> bit)) {
                        float px = org.x + borderX + (col * 6 + bit) * ps;
                        drawPixel(dl, px, py, ps, fg);
                    }
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Multicolor Mode — 64x48 color blocks
// --------------------------------------------------------------------------
void TMS9918::renderMulticolor(ImDrawList* dl, ImVec2 org, float ps,
                                const Snapshot& s, ImU32 backdrop)
{
    uint16_t nameBase    = (uint16_t)(s.regs[2] & 0x0F) << 10;
    uint16_t patternBase = (uint16_t)(s.regs[4] & 0x07) << 11;

    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 32; col++) {
            uint8_t name = s.vram[(nameBase + row * 32 + col) & 0x3FFF];

            // In multicolor, each pattern has 8 bytes, each byte = 2 color nibbles
            // The row within the pattern selects which pair of 2 lines
            int patRow = (row % 4) * 2;
            uint16_t patAddr = (patternBase + (uint16_t)name * 8 + patRow) & 0x3FFF;

            for (int subRow = 0; subRow < 2; subRow++) {
                uint8_t colorByte = s.vram[(patAddr + subRow) & 0x3FFF];
                uint8_t leftIdx  = (colorByte >> 4) & 0x0F;
                uint8_t rightIdx =  colorByte       & 0x0F;

                float py = org.y + (row * 8 + subRow * 4) * ps;

                // Left 4x4 block
                if (leftIdx != 0) {
                    ImU32 lc = kPalette[leftIdx];
                    for (int dy = 0; dy < 4; dy++) {
                        for (int dx = 0; dx < 4; dx++) {
                            drawPixel(dl, org.x + (col * 8 + dx) * ps,
                                      py + dy * ps, ps, lc);
                        }
                    }
                }
                // Right 4x4 block
                if (rightIdx != 0) {
                    ImU32 rc = kPalette[rightIdx];
                    for (int dy = 0; dy < 4; dy++) {
                        for (int dx = 0; dx < 4; dx++) {
                            drawPixel(dl, org.x + (col * 8 + 4 + dx) * ps,
                                      py + dy * ps, ps, rc);
                        }
                    }
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// Sprites — up to 32, rendered with priority (sprite 0 on top)
// --------------------------------------------------------------------------
void TMS9918::renderSprites(ImDrawList* dl, ImVec2 org, float ps,
                             const Snapshot& s)
{
    uint16_t sprAttrBase    = (uint16_t)(s.regs[5] & 0x7F) << 7;  // x 0x80
    uint16_t sprPatternBase = (uint16_t)(s.regs[6] & 0x07) << 11; // x 0x800

    bool doubleSize = (s.regs[1] & 0x02) != 0; // 16x16 if set
    bool magnified  = (s.regs[1] & 0x01) != 0; // 2x magnification

    int sprPixelSize = doubleSize ? 16 : 8;
    int mag = magnified ? 2 : 1;
    int totalPixels = sprPixelSize * mag;

    // Collect visible sprites (max 32, stop at Y=0xD0)
    struct SpriteInfo {
        int y, x;
        uint8_t name;
        uint8_t color;
    };
    SpriteInfo sprites[32];
    int spriteCount = 0;

    for (int i = 0; i < 32; i++) {
        uint16_t attrAddr = (sprAttrBase + i * 4) & 0x3FFF;
        uint8_t yRaw = s.vram[attrAddr];

        if (yRaw == 0xD0) break; // end-of-sprites marker

        // TMS9918 Y is stored as displayed_Y - 1.  Values > 0xD0 wrap above the
        // screen (allows sprites to scroll off the top edge smoothly).
        int y = (int)yRaw - ((yRaw > 0xD0) ? 256 : 0) + 1;
        int x = s.vram[(attrAddr + 1) & 0x3FFF];
        uint8_t name  = s.vram[(attrAddr + 2) & 0x3FFF];
        uint8_t color = s.vram[(attrAddr + 3) & 0x3FFF];

        // Early clock bit shifts sprite left by 32 pixels
        if (color & 0x80) x -= 32;

        sprites[spriteCount++] = { y, x, name, (uint8_t)(color & 0x0F) };
    }

    // Render sprites in reverse order (sprite 0 has highest priority, drawn last)
    for (int i = spriteCount - 1; i >= 0; i--) {
        const auto& spr = sprites[i];
        if (spr.color == 0) continue; // transparent sprite

        ImU32 sprColor = kPalette[spr.color];

        uint8_t patName = spr.name;
        if (doubleSize) patName &= 0xFC; // 16x16: lower 2 bits ignored

        for (int row = 0; row < sprPixelSize; row++) {
            int screenY = spr.y + row * mag;

            // Determine which pattern byte to read
            uint8_t patByte;
            if (!doubleSize) {
                // 8x8 sprite
                uint16_t addr = (sprPatternBase + (uint16_t)patName * 8 + row) & 0x3FFF;
                patByte = s.vram[addr];

                for (int bit = 0; bit < 8; bit++) {
                    if (!(patByte & (0x80 >> bit))) continue;
                    int screenX = spr.x + bit * mag;

                    for (int my = 0; my < mag; my++) {
                        int sy = screenY + my;
                        if (sy < 0 || sy >= kScreenHeight) continue;
                        for (int mx = 0; mx < mag; mx++) {
                            int sx = screenX + mx;
                            if (sx < 0 || sx >= kScreenWidth) continue;
                            drawPixel(dl, org.x + sx * ps, org.y + sy * ps, ps, sprColor);
                        }
                    }
                }
            } else {
                // 16x16 sprite: 4 quadrants stored as 4 consecutive 8-byte patterns
                // Layout: top-left (name), bottom-left (name+1), top-right (name+2), bottom-right (name+3)
                for (int half = 0; half < 2; half++) {
                    int quadrant;
                    if (row < 8) {
                        quadrant = half * 2;      // top-left or top-right
                    } else {
                        quadrant = half * 2 + 1;  // bottom-left or bottom-right
                    }
                    uint16_t addr = (sprPatternBase + (uint16_t)(patName + quadrant) * 8 + (row % 8)) & 0x3FFF;
                    patByte = s.vram[addr];

                    for (int bit = 0; bit < 8; bit++) {
                        if (!(patByte & (0x80 >> bit))) continue;
                        int screenX = spr.x + (half * 8 + bit) * mag;

                        for (int my = 0; my < mag; my++) {
                            int sy = screenY + my;
                            if (sy < 0 || sy >= kScreenHeight) continue;
                            for (int mx = 0; mx < mag; mx++) {
                                int sx = screenX + mx;
                                if (sx < 0 || sx >= kScreenWidth) continue;
                                drawPixel(dl, org.x + sx * ps, org.y + sy * ps, ps, sprColor);
                            }
                        }
                    }
                }
            }
        }
    }
}
