// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 paint model — see TmsPaintModel.h. Pure VRAM-plotting logic, no
// GL/ImGui/emulator dependency, unit-tested by tms_paint_plot_smoke against the
// real TMS9918 per-line renderers.

#include "TmsPaintModel.h"

#include <vector>

namespace tmspaint {

int width(Mode m)  { return m == Mode::Multicolor ? kMcWidth  : kGfx2Width;  }
int height(Mode m) { return m == Mode::Multicolor ? kMcHeight : kGfx2Height; }

void canonicalRegisters(Mode m, uint8_t regs[8])
{
    // Shared: name table $1800 (R2=$06), SAT $1B00 (R5=$36), sprite patterns
    // $3800 (R6=$07), backdrop = palette 1 / black (R7=$01). 16 KB addressing
    // and display enabled (R1 bit7 + bit6).
    regs[2] = 0x06;
    regs[5] = 0x36;
    regs[6] = 0x07;
    regs[7] = 0x01;
    if (m == Mode::GraphicsII) {
        // M3=1 (bitmap). Colour table $2000 with full $1FFF mask (R3=$FF),
        // pattern table $0000 with full $1FFF mask (R4=$03).
        regs[0] = 0x02;
        regs[1] = 0xC0;
        regs[3] = 0xFF;
        regs[4] = 0x03;
    } else {
        // Multicolor: M2=1 (R1 bit3). Pattern (colour) table $0000 (R4=$00),
        // colour table register unused.
        regs[0] = 0x00;
        regs[1] = 0xC8;
        regs[3] = 0x00;
        regs[4] = 0x00;
    }
}

void writeCanonicalNameTable(uint8_t* vram, Mode m)
{
    if (m == Mode::GraphicsII) {
        // Identity: cell i (0..767) → pattern slot i&0xFF, which combined with
        // the 3-section pattern offset gives every cell a unique 8-byte slot
        // (cf. renderGfxIILineRaw: charOffset = (section*256 + name)*8 + line).
        for (int i = 0; i < 768; ++i)
            vram[kNameBase + i] = static_cast<uint8_t>(i & 0xFF);
    } else {
        // Multicolor has no section offset, so the name must change every 4
        // character rows for each block to map to a distinct pattern nibble:
        // name[row*32+col] = (row/4)*32 + col (cf. renderMulticolorLineRaw:
        // patRow = (row%4)*2 + subRow covers the 8 bytes of one name's slot).
        for (int row = 0; row < 24; ++row)
            for (int col = 0; col < 32; ++col)
                vram[kNameBase + row * 32 + col] =
                    static_cast<uint8_t>((row / 4) * 32 + col);
    }
}

// ── Graphics II addressing ─────────────────────────────────────────────────

int gfx2PatternAddr(int x, int y)
{
    if (x < 0 || x >= kGfx2Width || y < 0 || y >= kGfx2Height) return -1;
    const int col       = x >> 3;            // 0..31
    const int row       = y >> 3;            // 0..23
    const int section   = row >> 3;          // 0..2  (24 rows / 8)
    const int r8        = row & 7;           // row within section
    const int lineInRow = y & 7;
    // patternBase ($0000) + charOffset, with the canonical identity name table
    // name = r8*32 + col → charOffset = section*2048 + r8*256 + col*8 + line.
    return section * 2048 + r8 * 256 + col * 8 + lineInRow;
}

int gfx2ColorAddr(int x, int y)
{
    const int p = gfx2PatternAddr(x, y);
    return (p < 0) ? -1 : (kGfx2ColorBase + p);
}

uint8_t gfx2BitMask(int x) { return static_cast<uint8_t>(0x80u >> (x & 7)); }

// ── Multicolor addressing ──────────────────────────────────────────────────

int mcBlockAddr(int bx, int by)
{
    if (bx < 0 || bx >= kMcWidth || by < 0 || by >= kMcHeight) return -1;
    const int col    = bx >> 1;              // 0..31
    const int row    = by >> 1;              // 0..23 (character row)
    const int g      = row >> 2;             // 0..5  (4-row group)
    const int patRow = (row & 3) * 2 + (by & 1);
    // patternBase ($0000) + name*8 + patRow, name = (row/4)*32 + col →
    // g*256 + col*8 + patRow.
    return g * 256 + col * 8 + patRow;
}

bool mcHighNibble(int bx) { return (bx & 1) == 0; }

// ── Plot / read ────────────────────────────────────────────────────────────

static int popcount8(uint8_t b)
{
    int n = 0;
    while (b) { n += b & 1; b >>= 1; }
    return n;
}

bool plotPixel(uint8_t* vram, Mode m, int x, int y, int color)
{
    color &= 0x0F;
    if (m == Mode::Multicolor) {
        const int addr = mcBlockAddr(x, y);
        if (addr < 0) return false;
        const uint8_t old = vram[addr];
        const uint8_t nb  = mcHighNibble(x)
            ? static_cast<uint8_t>((color << 4) | (old & 0x0F))
            : static_cast<uint8_t>((old & 0xF0) | color);
        if (nb == old) return false;
        vram[addr] = nb;
        return true;
    }

    // Graphics II — honour the 2-colours-per-8×1-cell constraint.
    const int patAddr = gfx2PatternAddr(x, y);
    const int colAddr = gfx2ColorAddr(x, y);
    if (patAddr < 0) return false;
    const uint8_t mask     = gfx2BitMask(x);
    const uint8_t oldPat   = vram[patAddr];
    const uint8_t oldCol   = vram[colAddr];
    const int     fg       = (oldCol >> 4) & 0x0F;
    const int     bg       =  oldCol       & 0x0F;
    uint8_t newPat = oldPat;
    uint8_t newCol = oldCol;
    if (color == fg) {
        newPat |= mask;                                  // pixel → foreground
    } else if (color == bg) {
        newPat &= static_cast<uint8_t>(~mask);           // pixel → background
    } else if (color == 0) {
        // Colour 0 is the transparent/background colour in Graphics II — it must
        // NOT be assigned to a colour SLOT via the clash path below, which would
        // recolour up to 7 OTHER pixels of the cell to 0 (the eraser blanking an
        // entire 8-pixel run). Clear the pattern bit so the pixel falls to this
        // cell's background slot, leaving the colour table untouched. On a fresh
        // cell bg is already 0 so this is truly transparent; in a two-non-zero-
        // colour cell it becomes the background ink — non-destructive, and
        // consistent with the HGR eraser (which likewise just clears the bit).
        newPat &= static_cast<uint8_t>(~mask);
    } else {
        // Colour clash: reassign whichever slot disturbs fewer OTHER pixels of
        // this cell. otherSet pixels currently use fg, 7-otherSet use bg.
        const int otherSet = popcount8(oldPat & static_cast<uint8_t>(~mask));
        if (otherSet <= 7 - otherSet) {
            newCol = static_cast<uint8_t>((color << 4) | bg);   // new fg
            newPat |= mask;
        } else {
            newCol = static_cast<uint8_t>((fg << 4) | color);   // new bg
            newPat &= static_cast<uint8_t>(~mask);
        }
    }
    if (newPat == oldPat && newCol == oldCol) return false;
    vram[patAddr] = newPat;
    vram[colAddr] = newCol;
    return true;
}

int colorAt(const uint8_t* vram, Mode m, int x, int y)
{
    if (m == Mode::Multicolor) {
        const int addr = mcBlockAddr(x, y);
        if (addr < 0) return -1;
        const uint8_t b = vram[addr];
        return mcHighNibble(x) ? ((b >> 4) & 0x0F) : (b & 0x0F);
    }
    const int patAddr = gfx2PatternAddr(x, y);
    if (patAddr < 0) return -1;
    const uint8_t pat = vram[patAddr];
    const uint8_t col = vram[gfx2ColorAddr(x, y)];
    return (pat & gfx2BitMask(x)) ? ((col >> 4) & 0x0F) : (col & 0x0F);
}

int fillRegion(uint8_t* vram, Mode m, int x, int y, int color)
{
    const int W = width(m), H = height(m);
    if (x < 0 || x >= W || y < 0 || y >= H) return 0;
    const int seed = colorAt(vram, m, x, y);
    color &= 0x0F;

    // 4-connected flood over equal colour on the ORIGINAL buffer, then recolour
    // the whole region (so plotPixel's per-cell clash juggling can't reshape the
    // region mid-walk).
    std::vector<uint8_t> seen(static_cast<size_t>(W) * H, 0);
    std::vector<std::pair<int,int>> stack, region;
    stack.emplace_back(x, y);
    seen[static_cast<size_t>(y) * W + x] = 1;
    while (!stack.empty()) {
        const auto p = stack.back(); stack.pop_back();
        region.push_back(p);
        const int nb[4][2] = {{p.first-1,p.second},{p.first+1,p.second},
                              {p.first,p.second-1},{p.first,p.second+1}};
        for (auto& n : nb) {
            const int nx = n[0], ny = n[1];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            const size_t idx = static_cast<size_t>(ny) * W + nx;
            if (seen[idx]) continue;
            if (colorAt(vram, m, nx, ny) != seed) continue;
            seen[idx] = 1;
            stack.emplace_back(nx, ny);
        }
    }
    if (color != seed)
        for (auto& p : region) plotPixel(vram, m, p.first, p.second, color);
    return static_cast<int>(region.size());
}

} // namespace tmspaint
