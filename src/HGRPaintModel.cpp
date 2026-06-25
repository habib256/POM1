// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR paint model — the pure bit-plotting logic behind HGRPaintEditor_ImGui,
// split out so it can be unit-tested (hgr_paint_plot_smoke) with no GL/ImGui
// dependency. Reuses GraphicsCard::hgrRowAddress for the canonical Apple II
// row interleave (no duplicated layout maths).

#include "HGRPaintEditor_ImGui.h"
#include "GraphicsCard.h"

namespace hgrpaint {

int hgrByteOffset(int x, int y)
{
    // hgrRowAddress is $2000-based; subtract the base for a page-relative
    // offset (identical interleave for both pages).
    return (GraphicsCard::hgrRowAddress(y, false) - 0x2000) + x / 7;
}

int hgrBit(int x) { return x % 7; }

int snapColumn(int x, HgrColor c)
{
    if (c == HgrColor::Black || c == HgrColor::White) return x;
    // Violet/Blue live on even columns, Green/Orange on odd.
    const int desiredParity = (c == HgrColor::Violet || c == HgrColor::Blue) ? 0 : 1;
    if ((x & 1) != desiredParity) x += (x > 0) ? -1 : +1;
    return x;
}

int targetOffset(int x, int y, HgrColor c)
{
    if (x < 0 || x > 279 || y < 0 || y > 191) return -1;
    const int sx = snapColumn(x, c);
    if (sx < 0 || sx > 279) return -1;
    return hgrByteOffset(sx, y);
}

int plotPage(uint8_t* page, int x, int y, HgrColor c)
{
    if (x < 0 || x > 279 || y < 0 || y > 191) return -1;
    const int sx = snapColumn(x, c);
    if (sx < 0 || sx > 279) return -1;
    const int off = hgrByteOffset(sx, y);
    const int bit = hgrBit(sx);
    const uint8_t b = page[off];
    uint8_t nb = b;
    switch (c) {
    case HgrColor::Black: nb &= static_cast<uint8_t>(~(1u << bit)); break;
    case HgrColor::White: nb |= static_cast<uint8_t>(1u << bit); break;   // palette untouched
    default:
        // Chromatic: set the byte's shared high bit to the colour's palette,
        // then light the pixel. Flipping the high bit recolours the byte's
        // other pixels — the real HGR/NTSC behaviour.
        if (c == HgrColor::Blue || c == HgrColor::Orange) nb |= 0x80u;
        else                                              nb &= 0x7Fu;
        nb |= static_cast<uint8_t>(1u << bit);
        break;
    }
    if (nb == b) return -1;
    page[off] = nb;
    return off;
}

bool pixelOn(const uint8_t* page, int x, int y)
{
    if (x < 0 || x > 279 || y < 0 || y > 191) return false;
    return (page[hgrByteOffset(x, y)] >> hgrBit(x)) & 1;
}

HgrColor colorAt(const uint8_t* page, int x, int y)
{
    if (x < 0 || x > 279 || y < 0 || y > 191) return HgrColor::Black;
    if (!pixelOn(page, x, y)) return HgrColor::Black;

    // White heuristic: a lit pixel reads white when its two same-byte horizontal
    // neighbours are also lit (the dot pattern fills, so NTSC sees no isolated
    // colour fringe). We restrict the test to within the SAME byte so we mirror
    // plotPage's writer, which only flips the high bit of one byte at a time and
    // never reasons across the byte boundary. This is a documented heuristic:
    // true NTSC white depends on neighbours across bytes too, but matching the
    // per-byte writer keeps colorAt round-tripping plotPage.
    const int bit = hgrBit(x);
    if (bit > 0 && bit < 6) {
        const uint8_t b = page[hgrByteOffset(x, y)];
        if (((b >> (bit - 1)) & 1) && ((b >> (bit + 1)) & 1))
            return HgrColor::White;
    }

    // Chromatic classification: palette (byte high bit) + column parity.
    // Mirrors snapColumn: palette0 even=Violet, palette0 odd=Green,
    // palette1 even=Blue, palette1 odd=Orange.
    const bool palette1 = (page[hgrByteOffset(x, y)] & 0x80u) != 0;
    const bool even = (x & 1) == 0;
    if (palette1) return even ? HgrColor::Blue   : HgrColor::Orange;
    else          return even ? HgrColor::Violet : HgrColor::Green;
}

bool byteHasPaletteSeam(const uint8_t* page, int byteCol, int y)
{
    // A "palette seam" is the boundary at which artifact-colour bleed occurs:
    // two horizontally adjacent bytes that both have lit pixels but disagree on
    // the shared high bit (palette select). At that seam the NTSC renderer mixes
    // the two palettes' colours, which surprises users. We flag the LEFT byte of
    // any such adjacent pair (byteCol .. byteCol+1).
    //
    // Pure + simple by design so the overlay matches a property the test pins.
    if (byteCol < 0 || byteCol > 38 || y < 0 || y > 191) return false;
    const int base = hgrByteOffset(0, y);
    const uint8_t a = page[base + byteCol];
    const uint8_t b = page[base + byteCol + 1];
    const bool aLit = (a & 0x7Fu) != 0;
    const bool bLit = (b & 0x7Fu) != 0;
    if (!aLit || !bLit) return false;
    return (a & 0x80u) != (b & 0x80u);
}

} // namespace hgrpaint
