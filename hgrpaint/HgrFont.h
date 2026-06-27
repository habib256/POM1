// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Beautiful Boot 8x8 bitmap font (full CP437) for the HGR Paint editor's Text
// tool. The glyph table lives in HgrFont.cpp, which is GENERATED from the single
// master dev/lib/gen2/bbfont_cp437.inc by tools/build_shared_font.py (emitter
// "cpp") -- the same source the 6502 GEN2/TMS9918 fonts are built from, so the
// editor's text matches what the emulated programs draw. Part of the portable
// hgrpaint/ toolkit (no GL/ImGui/emulator dependency).
//
// Encoding (identical to the master): each glyph is 8 rows top->bottom; in each
// row byte bit 0 = LEFTMOST pixel, 7 columns used (values <= 0x7F). To stamp a
// glyph, plot the lit pixels at (x+gx, y+gy) for gx in 0..6, gy in 0..7.

#ifndef HGRPAINT_FONT_H
#define HGRPAINT_FONT_H

#include <cstdint>

namespace hgrpaint {

// 256 glyphs x 8 rows; bit 0 = leftmost pixel (see file header).
extern const uint8_t kBBFontCp437[256][8];

// True if the glyph for byte `ch` lights the pixel at column gx (0..6, left->right)
// row gy (0..7, top->bottom). Out-of-range coordinates read as off.
inline bool bbFontPixel(unsigned char ch, int gx, int gy)
{
    if (gx < 0 || gx > 6 || gy < 0 || gy > 7) return false;
    return (kBBFontCp437[ch][gy] >> gx) & 1u;
}

// Glyph cell advance: 7 px wide + 1 px inter-character gap.
constexpr int kBBFontGlyphW = 7;
constexpr int kBBFontGlyphH = 8;
constexpr int kBBFontAdvance = 8;

} // namespace hgrpaint

#endif // HGRPAINT_FONT_H
