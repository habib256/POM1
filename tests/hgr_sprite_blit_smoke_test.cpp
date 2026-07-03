// GEN2 HGR sprite blit smoke test — pins the pure byte-placement logic of the
// HGR Sprite editor (hgrsprite::extract / stamp) against hgrpaint's Apple II
// row-interleave addressing. No GL/ImGui/emulator dependency.
//
// The Apple II has no hardware sprites — a sprite is a rectangle of HIRES bytes a
// program blits onto the page. This test draws pixels through the (already-pinned)
// hgrpaint model, extracts the rectangle, stamps it into a fresh page at a shifted
// location, and asserts every lit pixel reappears at the shifted coordinates —
// proving extract/stamp honour the non-linear interleave and clip off-page.
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "HgrSpriteBlit.h"
#include "HgrPaintModel.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using hgrpaint::HgrColor;

int main()
{
    const int W = 3, H = 12;                 // 3 bytes (21 px) × 12 rows

    // ── Draw a shape into a source page at byte-column 0, row 0 ──────────────
    std::vector<uint8_t> src(hgrpaint::kHiresSize, 0);
    // A diagonal of white pixels + a violet run, all inside the W×H box.
    for (int i = 0; i < H; ++i)
        hgrpaint::plotPage(src.data(), i, i, HgrColor::White);   // (x=i,y=i)
    for (int x = 0; x < 14; x += 2)
        hgrpaint::plotPage(src.data(), x, 3, HgrColor::Violet);  // row 3 violet dither

    // ── Extract the sprite, then stamp it into a fresh page shifted right/down ─
    std::vector<uint8_t> sprite(static_cast<size_t>(W) * H, 0);
    hgrsprite::extract(src.data(), 0, 0, W, H, sprite.data());

    const int dstCol = 12, dstRow = 40;
    std::vector<uint8_t> dst(hgrpaint::kHiresSize, 0);
    hgrsprite::stamp(sprite.data(), W, H, dstCol, dstRow,
                     [&](int off, uint8_t v) { dst[off] = v; });

    // Every source byte of the box must reappear at the shifted byte column/row.
    for (int r = 0; r < H; ++r)
        for (int b = 0; b < W; ++b) {
            const int soff = hgrpaint::hgrByteOffset(b * 7, r);
            const int doff = hgrpaint::hgrByteOffset((dstCol + b) * 7, dstRow + r);
            assert(soff >= 0 && doff >= 0);
            assert(dst[doff] == src[soff]);
        }

    // Pixel-level check: the white diagonal reappears shifted by (dstCol*7,dstRow).
    for (int i = 0; i < H; ++i) {
        assert(hgrpaint::pixelOn(src.data(), i, i));
        assert(hgrpaint::pixelOn(dst.data(), dstCol * 7 + i, dstRow + i));
    }

    // ── Round-trip: extract from dst at the shifted spot equals the sprite ────
    std::vector<uint8_t> back(static_cast<size_t>(W) * H, 0);
    hgrsprite::extract(dst.data(), dstCol, dstRow, W, H, back.data());
    assert(back == sprite);

    // ── Off-page clipping: stamping past the right/bottom edge must not crash
    //    and must skip the out-of-range cells (only in-range ones are poked). ──
    {
        int pokes = 0;
        std::vector<uint8_t> edge(hgrpaint::kHiresSize, 0);
        hgrsprite::stamp(sprite.data(), W, H, hgrsprite::kByteCols - 1, hgrsprite::kRows - 2,
                         [&](int off, uint8_t v) { assert(off >= 0 && off < hgrpaint::kHiresSize);
                                                   edge[off] = v; ++pokes; });
        // Only byte column 39 (1 of 3) × rows 190-191 (2 of 12) land on-page.
        assert(pokes == 1 * 2);
    }

    // ── Colour-aware ×2 magnify (reliable per-super-pixel artifact colour) ────
    // Each source cell → a 2-aligned colour clock, so the authored hue survives
    // the doubling regardless of column parity. Stamp the doubled sprite and read
    // the displayed colour back through the pinned hgrpaint decoder.
    {
        const int cwB = 1, cH = 2, cwpx = cwB * 7;      // 7×2 colour cells
        std::vector<HgrColor> cells(static_cast<size_t>(cwpx) * cH, HgrColor::Black);
        auto setc = [&](int x, int y, HgrColor c) { cells[y * cwpx + x] = c; };
        setc(0, 0, HgrColor::Violet);
        setc(2, 0, HgrColor::Green);
        setc(4, 0, HgrColor::Blue);
        setc(6, 0, HgrColor::Orange);
        setc(0, 1, HgrColor::White);                    // contiguous white pair →
        setc(1, 1, HgrColor::White);                    // interior reads pure white

        std::vector<uint8_t> dbl(static_cast<size_t>(cwB * 2) * (cH * 2), 0);
        hgrsprite::magnifyColor2x(cells.data(), cwB, cH, dbl.data());

        std::vector<uint8_t> pg(hgrpaint::kHiresSize, 0);
        hgrsprite::stamp(dbl.data(), cwB * 2, cH * 2, 0, 0,
                         [&](int off, uint8_t v) { pg[off] = v; });

        // Each chromatic super-pixel reproduces its exact hue (2-aligned clock).
        assert(hgrpaint::colorAt(pg.data(), 0,  0) == HgrColor::Violet);
        assert(hgrpaint::colorAt(pg.data(), 5,  0) == HgrColor::Green);
        assert(hgrpaint::colorAt(pg.data(), 8,  0) == HgrColor::Blue);
        assert(hgrpaint::colorAt(pg.data(), 13, 0) == HgrColor::Orange);
        assert(hgrpaint::colorAt(pg.data(), 1,  2) == HgrColor::White);
        assert(hgrpaint::colorAt(pg.data(), 2,  2) == HgrColor::White);
        assert(hgrpaint::colorAt(pg.data(), 3,  0) == HgrColor::Black);   // untouched cell
    }

    std::printf("hgr_sprite_blit_smoke: OK\n");
    return 0;
}
