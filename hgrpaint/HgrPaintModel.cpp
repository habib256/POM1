// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR paint model — the emulator-agnostic bit-plotting logic behind
// HgrPaintEditor, split out so it can be unit-tested (hgr_paint_plot_smoke) with
// no GL/ImGui/emulator dependency. Carries its own copy of the Apple II row
// interleave (a hardware fact, identical on any host).

#include "HgrPaintModel.h"

#include <utility>
#include <vector>

namespace hgrpaint {

uint16_t hgrRowAddress(int y)
{
    // Apple II HIRES non-linear memory layout: 192 lines split into 3 groups of
    // 64, each interleaved in blocks of 8 (Woz reused the row counter for DRAM
    // refresh). Page 1 base ($2000); the model works page-relative.
    const int group    = y / 64;          // 0, 1, 2
    const int subGroup = (y % 64) / 8;     // 0-7
    const int line     = y % 8;            // 0-7
    return static_cast<uint16_t>(kHiresBase)
         + static_cast<uint16_t>(group)    * 0x28
         + static_cast<uint16_t>(subGroup) * 0x80
         + static_cast<uint16_t>(line)     * 0x400;
}

int hgrByteOffset(int x, int y)
{
    // hgrRowAddress is $2000-based; subtract the base for a page-relative
    // offset (identical interleave for both pages).
    return (hgrRowAddress(y) - kHiresBase) + x / 7;
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

int setBytePalette(uint8_t* page, int byteCol, int y, int msb)
{
    // Flip only the shared high bit (palette select) of one byte — recolours its
    // 7 pixels (orange<->green, blue<->violet) WITHOUT lighting/clearing any
    // pixel. msb: 0 = clear, 1 = set, anything else = toggle. Returns the changed
    // page offset, or -1 if out of range / no change. The HGR-unique "transparent"
    // palette-shift fadden exposes as HI_BIT_CLEAR/HI_BIT_SET patterns.
    if (byteCol < 0 || byteCol > 39 || y < 0 || y > 191) return -1;
    const int off = hgrByteOffset(0, y) + byteCol;
    const uint8_t b = page[off];
    uint8_t nb = b;
    if (msb == 0)      nb &= 0x7Fu;
    else if (msb == 1) nb |= 0x80u;
    else               nb ^= 0x80u;
    if (nb == b) return -1;
    page[off] = nb;
    return off;
}

int fillRegion(uint8_t* page, int x, int y, HgrColor c, const RenderPageFn& render)
{
    if (x < 0 || x > 279 || y < 0 || y > 191 || !render) return 0;

    // Render the page through the host's real NTSC pipeline so connectivity
    // matches the displayed image (see header).
    constexpr int W = kHiresWidth, H = kHiresHeight;
    std::vector<uint32_t> col(static_cast<size_t>(W) * H, 0);
    render(page, col.data());
    auto rgb = [&](int px, int py) { return col[static_cast<size_t>(py) * W + px] & 0x00FFFFFFu; };
    const uint32_t seed = rgb(x, y);

    // 4-connected flood over equal perceived colour.
    std::vector<uint8_t> seen(static_cast<size_t>(W) * H, 0);
    std::vector<std::pair<int,int>> stack, region;
    stack.emplace_back(x, y);
    seen[static_cast<size_t>(y) * W + x] = 1;
    while (!stack.empty()) {
        const std::pair<int,int> p = stack.back(); stack.pop_back();
        const int px = p.first, py = p.second;
        region.emplace_back(px, py);
        const int nb[4][2] = {{px - 1, py}, {px + 1, py}, {px, py - 1}, {px, py + 1}};
        for (auto& n : nb) {
            const int nx = n[0], ny = n[1];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            const size_t idx = static_cast<size_t>(ny) * W + nx;
            if (seen[idx]) continue;
            if (rgb(nx, ny) != seed) continue;
            seen[idx] = 1;
            stack.emplace_back(nx, ny);
        }
    }

    // Recolour: clear the whole region first (so leftover bits of the old colour
    // can't OR with the new colour's bits into white), then stamp c's pattern.
    // Chromatic colours only live on one column parity, so we light just the
    // matching-parity pixels — no snap-bleed past the region edge.
    for (auto& p : region) plotPage(page, p.first, p.second, HgrColor::Black);
    if (c != HgrColor::Black) {
        const int parity = (c == HgrColor::Violet || c == HgrColor::Blue)  ? 0
                         : (c == HgrColor::Green  || c == HgrColor::Orange) ? 1
                         : -1;   // White: parity-agnostic, light every pixel
        for (auto& p : region) {
            if (parity < 0)                  plotPage(page, p.first, p.second, c);   // White
            else if ((p.first & 1) == parity) plotPage(page, p.first, p.second, c);
        }
    }
    return static_cast<int>(region.size());
}

} // namespace hgrpaint
