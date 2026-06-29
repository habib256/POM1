// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HgrSpriteBank — the emulator-agnostic core behind the HGR Paint editor's
// "Sprite Bank" mode: carve named rectangles out of an HGR page and BAKE them
// into Buzzard-Bait-style 7-phase PRE-SHIFTED banks for the GEN2 HGR runtime
// (gen2_hgr_sprite, dev/lib/gen2c). Pure (no GL / ImGui / emulator), so it
// unit-tests standalone (hgr_sprite_bank_smoke) and ports verbatim to POM2.
//
// The byte layout produced here is the SAME ABI gen2_sprite_t expects
// (dev/lib/gen2c/gen2.h) and the same one tools/build_preshift_sprites.py emits:
//   stride = ceil((w + 6) / 7) bytes per row, uniform across the 7 phases;
//   7 phase blocks, each h*stride bytes, row-major, 7px/byte (bit 0 = leftmost,
//   bit 7 = 0); phase p at offset p*(h*stride); source pixel (row,col) of phase
//   p lands at packed bit (col+p): byte (col+p)/7, bit (col+p)%7.

#ifndef HGRPAINT_SPRITE_BANK_H
#define HGRPAINT_SPRITE_BANK_H

#include <cstdint>
#include <string>
#include <vector>

namespace hgrpaint {

// A sprite region selected on the HGR canvas, in logical pixels (x in 0..279,
// y in 0..191). Names a rectangle the editor lifts out of the page.
struct SpriteRegion {
    std::string name;
    int x = 0, y = 0, w = 1, h = 1;
};

// One baked sprite: a 7-phase pre-shifted bank ready for gen2_hgr_sprite().
struct BakedSprite {
    std::string name;
    int w = 0, h = 0, stride = 0;   // stride = ceil((w+6)/7)
    std::vector<uint8_t> bank;      // 7 * h * stride bytes
};

// stride (bytes per row per phase) for a sprite w pixels wide.
int spriteStride(int w);

// Read region `r` of an 8 KB HGR page as a 1bpp shape mask: mask[row][col] is 1
// when logical pixel (r.x+col, r.y+row) is lit (mono on/off — sprites are shape
// masks; the runtime/NTSC handles colour). Off-page pixels read as 0.
std::vector<std::vector<uint8_t>> extractMask(const uint8_t* page8k,
                                              const SpriteRegion& r);

// Bake a shape mask (mask[row][col] in {0,1}) into a 7-phase pre-shifted bank.
BakedSprite bakeMask(const std::string& name,
                     const std::vector<std::vector<uint8_t>>& mask);

// Convenience: extractMask + bakeMask straight from a page region.
BakedSprite bakeRegion(const uint8_t* page8k, const SpriteRegion& r);

// Emit a C header (gen2_sprite_t + data) for the baked sprites — drop-in for a
// gen2c project that links GEN2C_PRESHIFT_SRCS. `title` seeds the include guard.
std::string emitC(const std::vector<BakedSprite>& sprites, const std::string& title);

// Emit just the C body (data arrays + gen2_sprite_t, no include guard / #include)
// — for pasting straight into a DevBench buffer that already #includes "gen2.h".
// DevBench compiles a single buffer, so a sibling header can't be reached; this
// inlines the data instead.
std::string emitInline(const std::vector<BakedSprite>& sprites);

// Emit ca65 assembly (returned) plus, via `incOut`, the matching .inc of
// per-sprite W/H/STRIDE/PHASES constants — for a pure-asm project.
std::string emitAsm(const std::vector<BakedSprite>& sprites, const std::string& title,
                    std::string& incOut);

// Emit a tools/build_preshift_sprites.py-compatible ASCII-art sheet (the raw,
// re-bakeable 1bpp source) for the regions of a page. Round-trips: the editor
// can re-import it or the Python tool can re-bake it.
std::string emitSheet(const uint8_t* page8k, const std::vector<SpriteRegion>& regions);

} // namespace hgrpaint

#endif // HGRPAINT_SPRITE_BANK_H
