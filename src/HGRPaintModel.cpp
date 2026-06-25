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

} // namespace hgrpaint
