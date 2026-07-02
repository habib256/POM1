// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 sprite model — see TmsSpriteModel.h. Pure VRAM logic, no
// GL/ImGui/emulator dependency, pinned by tms_sprite_plot_smoke against the real
// TMS9918::renderSpritesLineRaw addressing.

#include "TmsSpriteModel.h"

namespace tmssprite {

int dim(Size s)            { return s == Size::S16 ? 16 : 8; }
int slotsPerSprite(Size s) { return s == Size::S16 ? 4  : 1; }

uint8_t bitMask(int x) { return static_cast<uint8_t>(0x80u >> (x & 7)); }

int patternByteAddr(int patNum, Size s, int x, int y)
{
    const int d = dim(s);
    if (x < 0 || x >= d || y < 0 || y >= d) return -1;
    if (s == Size::S16) {
        patNum &= 0xFC;                                   // chip masks to /4
        const int quadrant = (x >= 8 ? 2 : 0) + (y >= 8 ? 1 : 0);
        return kSpritePatternBase + (patNum + quadrant) * 8 + (y & 7);
    }
    patNum &= 0xFF;
    return kSpritePatternBase + patNum * 8 + (y & 7);
}

bool getPixel(const uint8_t* vram, int patNum, Size s, int x, int y)
{
    const int addr = patternByteAddr(patNum, s, x, y);
    if (addr < 0) return false;
    return (vram[addr] & bitMask(x)) != 0;
}

bool setPixel(uint8_t* vram, int patNum, Size s, int x, int y, bool on)
{
    const int addr = patternByteAddr(patNum, s, x, y);
    if (addr < 0) return false;
    const uint8_t old  = vram[addr];
    const uint8_t mask = bitMask(x);
    const uint8_t nb   = on ? static_cast<uint8_t>(old | mask)
                            : static_cast<uint8_t>(old & ~mask);
    if (nb == old) return false;
    vram[addr] = nb;
    return true;
}

void writeSatEntry(uint8_t* vram, int idx, uint8_t y, uint8_t x,
                   uint8_t pat, uint8_t colourEc)
{
    if (idx < 0 || idx >= kMaxSprites) return;
    const int base = kSpriteAttrBase + idx * 4;
    vram[base + 0] = y;
    vram[base + 1] = x;
    vram[base + 2] = pat;
    vram[base + 3] = colourEc;
}

void terminateSat(uint8_t* vram, int idx)
{
    if (idx < 0 || idx >= kMaxSprites) return;
    vram[kSpriteAttrBase + idx * 4] = static_cast<uint8_t>(kSatTerminator);
}

void canonicalRegisters(uint8_t regs[8], Size s, bool magnified, int backdrop)
{
    // Graphics I, display + 16 KB enabled, sprite engine live (no M1/M2/M3).
    regs[0] = 0x00;
    regs[1] = 0xC0;                                   // 16K + display enable
    if (s == Size::S16) regs[1] |= 0x02;             // sprite size 16×16
    if (magnified)      regs[1] |= 0x01;             // sprite magnification ×2
    regs[2] = 0x06;                                   // name table   $1800
    regs[3] = 0x80;                                   // colour table $2000 (<<6)
    regs[4] = 0x00;                                   // pattern gen  $0000 (<<11)
    regs[5] = 0x36;                                   // SAT          $1B00 (<<7)
    regs[6] = 0x07;                                   // sprite pats  $3800 (<<11)
    regs[7] = static_cast<uint8_t>(backdrop & 0x0F);  // backdrop palette index
}

void writeCanonicalBackdrop(uint8_t* vram, int backdrop)
{
    const uint8_t bd = static_cast<uint8_t>(backdrop & 0x0F);
    // Name table → all reference pattern slot 0 (768 bytes).
    for (int i = 0; i < 768; ++i) vram[kNameBase + i] = 0x00;
    // Pattern slot 0 → all-background (8 zero bytes).
    for (int i = 0; i < 8; ++i) vram[kGfx1PatternBase + i] = 0x00;
    // Graphics-I colour table: 32 bytes, one per 8 patterns; (fg<<4)|bg. With an
    // all-zero pattern every playfield pixel takes bg, so make both nibbles the
    // backdrop index → a flat field.
    for (int i = 0; i < 32; ++i)
        vram[kGfx1ColorBase + i] = static_cast<uint8_t>((bd << 4) | bd);
}

} // namespace tmssprite
