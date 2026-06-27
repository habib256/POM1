// HGR paint model smoke test — pins the pure bit-plotting logic of the HGR
// Paint editor (hgrpaint:: helpers) with no GL/ImGui dependency.
//
// Covers: byte/bit addressing via the Apple II interleave, column-parity
// snapping per colour, black/white/chromatic plotting, the per-byte shared
// high-bit (palette) behaviour, pixel read-back, and bounds.
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "HgrPaintModel.h"
#include "HgrFont.h"
#include "GraphicsCard.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using hgrpaint::HgrColor;

// Render an 8 KB HGR page through the real GEN2 NTSC pipeline — the renderer the
// editor canvas uses — so fillRegion floods by perceived colour. (POM1 supplies
// this via Pom1HgrPaintHost; the test wires GraphicsCard directly.)
static GraphicsCard g_gfx;
static void renderPage(const uint8_t* page8k, uint32_t* out)
{
    std::vector<uint8_t> mem(0x10000, 0);
    std::copy(page8k, page8k + hgrpaint::kHiresSize, mem.begin() + 0x2000);
    GraphicsCard::DisplayState st;
    st.textMode = false; st.mixedMode = false; st.hiRes = true; st.page2 = false;
    g_gfx.setMonitorMode(GraphicsCard::MonitorMode::Colour);
    g_gfx.invalidate();
    g_gfx.render(mem.data(), st, st, {});
    std::copy(g_gfx.pixels(),
              g_gfx.pixels() + static_cast<size_t>(hgrpaint::kHiresWidth) * hgrpaint::kHiresHeight,
              out);
}

int main()
{
    std::vector<uint8_t> page(hgrpaint::kHiresSize, 0);

    // ── addressing ────────────────────────────────────────────────────────
    // Row 0 starts at $2000 → offset 0; bit = column % 7; byte = column / 7.
    assert(hgrpaint::hgrByteOffset(0, 0) == 0);
    assert(hgrpaint::hgrByteOffset(6, 0) == 0);
    assert(hgrpaint::hgrByteOffset(7, 0) == 1);
    assert(hgrpaint::hgrBit(0) == 0);
    assert(hgrpaint::hgrBit(6) == 6);
    assert(hgrpaint::hgrBit(7) == 0);
    // Page-relative offset must match GraphicsCard's canonical row interleave.
    for (int y = 0; y < 192; ++y) {
        const int expect = (GraphicsCard::hgrRowAddress(y, false) - 0x2000);
        assert(hgrpaint::hgrByteOffset(0, y) == expect);
    }

    // ── column-parity snapping ───────────────────────────────────────────
    // Violet/Blue → even columns, Green/Orange → odd columns.
    assert(hgrpaint::snapColumn(4, HgrColor::Violet) == 4);   // already even
    assert(hgrpaint::snapColumn(5, HgrColor::Violet) == 4);   // odd → even
    assert(hgrpaint::snapColumn(5, HgrColor::Green)  == 5);   // already odd
    assert(hgrpaint::snapColumn(4, HgrColor::Green)  == 3);   // even → odd
    assert(hgrpaint::snapColumn(0, HgrColor::Green)  == 1);   // edge: can't go below 0
    assert(hgrpaint::snapColumn(9, HgrColor::Blue)   == 8);   // odd → even
    // Black/White are parity-agnostic.
    assert(hgrpaint::snapColumn(5, HgrColor::Black) == 5);
    assert(hgrpaint::snapColumn(4, HgrColor::White) == 4);

    // ── white sets a bit, black clears it, palette bit untouched ─────────
    int off = hgrpaint::plotPage(page.data(), 0, 0, HgrColor::White);
    assert(off == 0);
    assert(page[0] == 0x01);                      // bit 0 set, high bit clear
    assert(hgrpaint::pixelOn(page.data(), 0, 0));
    // Re-plotting the same white pixel is a no-op.
    assert(hgrpaint::plotPage(page.data(), 0, 0, HgrColor::White) == -1);
    off = hgrpaint::plotPage(page.data(), 0, 0, HgrColor::Black);
    assert(off == 0);
    assert(page[0] == 0x00);
    assert(!hgrpaint::pixelOn(page.data(), 0, 0));

    // ── chromatic plot sets the pixel bit AND the byte's shared high bit ──
    page.assign(page.size(), 0);
    // Violet at column 2 (even, palette 0): bit 2 set, high bit clear.
    off = hgrpaint::plotPage(page.data(), 2, 0, HgrColor::Violet);
    assert(off == 0);
    assert(page[0] == 0x04);                      // (1<<2), high bit 0
    // Green at column 3 (odd, palette 0) in the same byte: adds bit 3.
    hgrpaint::plotPage(page.data(), 3, 0, HgrColor::Green);
    assert(page[0] == 0x0C);                      // bits 2 and 3, high bit still 0

    // Now paint Blue (palette 1) in the same byte: high bit flips to 1 for the
    // WHOLE byte — the real HGR particularity that recolours its other pixels.
    off = hgrpaint::plotPage(page.data(), 4, 0, HgrColor::Blue);
    assert(off == 0);
    assert((page[0] & 0x80) != 0);                // shared high bit now set
    assert((page[0] & 0x04) != 0);                // earlier pixels still lit
    assert((page[0] & 0x10) != 0);                // blue pixel (col 4, bit 4)

    // ── snapping moves an off-parity chromatic click to a valid column ───
    page.assign(page.size(), 0);
    // Green requested at even column 10 → snaps to column 9 (bit 2 of byte 1).
    off = hgrpaint::plotPage(page.data(), 10, 0, HgrColor::Green);
    assert(off == hgrpaint::hgrByteOffset(9, 0));
    assert(hgrpaint::pixelOn(page.data(), 9, 0));

    // ── bounds ───────────────────────────────────────────────────────────
    assert(hgrpaint::plotPage(page.data(), -1, 0, HgrColor::White) == -1);
    assert(hgrpaint::plotPage(page.data(), 280, 0, HgrColor::White) == -1);
    assert(hgrpaint::plotPage(page.data(), 0, 192, HgrColor::White) == -1);
    assert(hgrpaint::targetOffset(0, 0, HgrColor::White) == 0);
    assert(hgrpaint::targetOffset(279, 191, HgrColor::White) ==
           hgrpaint::hgrByteOffset(279, 191));
    assert(!hgrpaint::pixelOn(page.data(), -5, 0));

    // ── colorAt round-trips plotPage (HGR-03) ────────────────────────────
    page.assign(page.size(), 0);
    // Off pixel reads Black.
    assert(hgrpaint::colorAt(page.data(), 50, 10) == HgrColor::Black);
    // White: plot an isolated lit bit, then ensure its neighbours read it as
    // chromatic, and a fully-filled run reads as White.
    hgrpaint::plotPage(page.data(), 14, 20, HgrColor::Violet);  // col 14 even, byte 2 bit 0
    assert(hgrpaint::colorAt(page.data(), 14, 20) == HgrColor::Violet);
    // Green snaps to an odd column; plotting at 15 (odd, palette 0) → Green.
    hgrpaint::plotPage(page.data(), 15, 20, HgrColor::Green);
    assert(hgrpaint::colorAt(page.data(), 15, 20) == HgrColor::Green);
    // Blue flips the whole byte's high bit to palette 1.
    page.assign(page.size(), 0);
    hgrpaint::plotPage(page.data(), 16, 30, HgrColor::Blue);    // even col, palette1
    assert(hgrpaint::colorAt(page.data(), 16, 30) == HgrColor::Blue);
    hgrpaint::plotPage(page.data(), 17, 30, HgrColor::Orange);  // odd col, palette1
    assert(hgrpaint::colorAt(page.data(), 17, 30) == HgrColor::Orange);
    // White heuristic: three adjacent same-byte bits lit → the middle reads White.
    page.assign(page.size(), 0);
    hgrpaint::plotPage(page.data(), 22, 40, HgrColor::White);   // byte 3 bit 1
    hgrpaint::plotPage(page.data(), 23, 40, HgrColor::White);   // byte 3 bit 2
    hgrpaint::plotPage(page.data(), 24, 40, HgrColor::White);   // byte 3 bit 3
    assert(hgrpaint::colorAt(page.data(), 23, 40) == HgrColor::White);
    // White heuristic crosses the byte boundary: a solid white run spanning bytes 0
    // and 1 must read White on its bit-6 (col 6) and bit-0 (col 7) edge columns too.
    // The old same-byte-only test mis-labelled these as Violet/Green; pixelOn() now
    // consults the neighbour in the adjacent byte.
    page.assign(page.size(), 0);
    for (int x = 0; x <= 14; ++x) hgrpaint::plotPage(page.data(), x, 10, HgrColor::White);
    assert(hgrpaint::colorAt(page.data(), 6, 10) == HgrColor::White);   // bit 6, byte 0 edge
    assert(hgrpaint::colorAt(page.data(), 7, 10) == HgrColor::White);   // bit 0, byte 1 edge
    // Run endpoints have a neighbour off → not white (round-trip of an edge pixel).
    assert(hgrpaint::colorAt(page.data(), 0, 10) != HgrColor::White);
    // Bounds.
    assert(hgrpaint::colorAt(page.data(), -1, 0) == HgrColor::Black);
    assert(hgrpaint::colorAt(page.data(), 0, 192) == HgrColor::Black);

    // ── byteHasPaletteSeam (HGR-07) ──────────────────────────────────────
    page.assign(page.size(), 0);
    const int rowBase = hgrpaint::hgrByteOffset(0, 60);
    // Two adjacent lit bytes, same palette → no seam.
    page[rowBase + 5] = 0x7E;   // lit, high bit clear
    page[rowBase + 6] = 0x7E;   // lit, high bit clear
    assert(!hgrpaint::byteHasPaletteSeam(page.data(), 5, 60));
    // Flip the right byte to palette 1 while both stay lit → seam at byte 5.
    page[rowBase + 6] = 0xFE;   // lit, high bit set
    assert(hgrpaint::byteHasPaletteSeam(page.data(), 5, 60));
    // If one side has no lit pixels, no seam regardless of high bits.
    page[rowBase + 6] = 0x80;   // only high bit set, no lit pixels
    assert(!hgrpaint::byteHasPaletteSeam(page.data(), 5, 60));
    // Out-of-range byte columns / rows are safe.
    assert(!hgrpaint::byteHasPaletteSeam(page.data(), -1, 60));
    assert(!hgrpaint::byteHasPaletteSeam(page.data(), 39, 60));
    assert(!hgrpaint::byteHasPaletteSeam(page.data(), 5, 192));

    // ── setBytePalette (HGR-11): flips only the shared high bit ──────────
    page.assign(page.size(), 0);
    const int pbBase = hgrpaint::hgrByteOffset(0, 70);
    page[pbBase + 4] = 0x2A;                 // some lit pixels, high bit clear
    // Set palette → high bit on, pixels untouched.
    int pbOff = hgrpaint::setBytePalette(page.data(), 4, 70, 1);
    assert(pbOff == pbBase + 4);
    assert(page[pbBase + 4] == 0xAA);        // 0x2A | 0x80, pixel bits intact
    // Idempotent set is a no-op.
    assert(hgrpaint::setBytePalette(page.data(), 4, 70, 1) == -1);
    // Toggle flips it back to clear.
    assert(hgrpaint::setBytePalette(page.data(), 4, 70, 2) == pbBase + 4);
    assert(page[pbBase + 4] == 0x2A);
    // Clear when already clear is a no-op.
    assert(hgrpaint::setBytePalette(page.data(), 4, 70, 0) == -1);
    // Out-of-range byte columns / rows are safe.
    assert(hgrpaint::setBytePalette(page.data(), -1, 70, 1) == -1);
    assert(hgrpaint::setBytePalette(page.data(), 40, 70, 1) == -1);
    assert(hgrpaint::setBytePalette(page.data(), 4, 192, 1) == -1);

    // ── fillRegion: colour-based flood, no leak through dithered colours ─────
    // A solid violet field is the byte pattern $55 (odd columns OFF), so a raw-bit
    // flood of the black background would leak through those off sub-pixels into
    // the shape. fillRegion floods by perceived colour, so it must NOT.
    page.assign(page.size(), 0);
    // Violet block, columns 100..140, rows 50..60.
    for (int yy = 50; yy <= 60; ++yy)
        for (int xx = 100; xx <= 140; ++xx)
            hgrpaint::plotPage(page.data(), xx, yy, HgrColor::Violet);
    // An interior byte (cols 105..111, row 55): violet lights even cols → $2A.
    const int vByte = hgrpaint::hgrByteOffset(105, 55);
    assert(page[vByte] == 0x2A);
    // Fill the black background with White, seeded far from the block.
    hgrpaint::fillRegion(page.data(), 0, 0, HgrColor::White, renderPage);
    // Background far away is now solid white ($7F, palette untouched).
    assert(page[hgrpaint::hgrByteOffset(0, 0)] == 0x7F);
    assert(page[hgrpaint::hgrByteOffset(0, 100)] == 0x7F);
    // The violet block was NOT leaked into: its interior byte is still $2A — a
    // raw-bit flood would have flipped its odd columns on, making it $7F.
    assert(page[vByte] == 0x2A);

    // Recolour the violet block to Green: clear-then-stamp must NOT merge the old
    // violet bits with the new green bits into white ($2A | $55 == $7F).
    hgrpaint::fillRegion(page.data(), 105, 55, HgrColor::Green, renderPage);
    assert(page[vByte] == 0x55);             // green pattern (odd cols), not $7F
    assert((page[vByte] & 0x80) == 0);       // palette 0 (green), high bit clear

    // Filling Black clears a region back to empty.
    hgrpaint::fillRegion(page.data(), 105, 55, HgrColor::Black, renderPage);
    assert(page[vByte] == 0x00);
    // Out-of-range seed is a safe no-op.
    assert(hgrpaint::fillRegion(page.data(), -1, 0, HgrColor::White, renderPage) == 0);
    assert(hgrpaint::fillRegion(page.data(), 0, 192, HgrColor::White, renderPage) == 0);

    // ── bbfont (Text tool): glyph table orientation + chromatic parity stamp ──
    // The font is GENERATED from dev/lib/gen2/bbfont_cp437.inc (bit 0 = leftmost
    // pixel, 7 px/glyph). Pin the orientation so a generator/master drift fails
    // here, and pin the Text tool's parity-gated chromatic stamping convention.
    {
        // Space is blank; a glyph fits 7 columns (values <= 0x7F → bit 7 never set).
        for (int gx = 0; gx < 7; ++gx)
            for (int gy = 0; gy < 8; ++gy) assert(!hgrpaint::bbFontPixel(' ', gx, gy));
        for (int ch = 0; ch < 256; ++ch)
            for (int gy = 0; gy < 8; ++gy) assert((hgrpaint::kBBFontCp437[ch][gy] & 0x80) == 0);
        // 'A' top row is master byte 0x1E = bits 1..4 (bit 0 = left): cols 1-4 lit.
        assert(!hgrpaint::bbFontPixel('A', 0, 0));
        assert(hgrpaint::bbFontPixel('A', 1, 0) && hgrpaint::bbFontPixel('A', 4, 0));
        assert(!hgrpaint::bbFontPixel('A', 5, 0) && !hgrpaint::bbFontPixel('A', 6, 0));

        // White stamp lights every lit glyph pixel; chromatic (Violet) lights only
        // even-parity columns (mirrors stampText), leaving the byte's palette bit
        // clear (palette 0 = Violet).
        page.assign(page.size(), 0);
        int litWhite = 0;
        for (int gy = 0; gy < 8; ++gy)
            for (int gx = 0; gx < 7; ++gx)
                if (hgrpaint::bbFontPixel('A', gx, gy)) {
                    hgrpaint::plotPage(page.data(), gx, 0 + gy, HgrColor::White); ++litWhite;
                }
        int onWhite = 0;
        for (int gy = 0; gy < 8; ++gy)
            for (int gx = 0; gx < 7; ++gx) if (hgrpaint::pixelOn(page.data(), gx, gy)) ++onWhite;
        assert(litWhite > 0 && onWhite == litWhite);

        page.assign(page.size(), 0);
        for (int gy = 0; gy < 8; ++gy)
            for (int gx = 0; gx < 7; ++gx)
                if (hgrpaint::bbFontPixel('A', gx, gy) && (gx & 1) == 0)
                    hgrpaint::plotPage(page.data(), gx, gy, HgrColor::Violet);
        for (int gy = 0; gy < 8; ++gy)
            for (int x = 0; x < 7; ++x)
                if (hgrpaint::pixelOn(page.data(), x, gy)) assert((x & 1) == 0);
        assert((page[hgrpaint::hgrByteOffset(0, 2)] & 0x80) == 0);   // Violet → palette 0
    }

    // ── Apple II lo-res (GR) block model ─────────────────────────────────────
    // 40×48 blocks in the TEXT page; two stacked blocks per byte (low nibble =
    // upper/even row, high nibble = lower/odd row). The page-relative offset must
    // match GraphicsCard's text/lo-res row interleave; a drift fails here.
    {
        for (int by = 0; by < hgrpaint::kGrRows; ++by)
            for (int bx = 0; bx < hgrpaint::kGrCols; ++bx) {
                const int expect = (GraphicsCard::textRowAddress(by / 2, false) - 0x0400) + bx;
                assert(hgrpaint::grBlockOffset(bx, by) == expect);
            }
        std::vector<uint8_t> gp(0x400, 0);
        // Blocks (5,6) even and (5,7) odd share one byte: low nibble C, high nibble 2.
        assert(hgrpaint::plotGrBlock(gp.data(), 5, 6, 0xC) == hgrpaint::grBlockOffset(5, 6));
        hgrpaint::plotGrBlock(gp.data(), 5, 7, 0x2);
        assert(gp[hgrpaint::grBlockOffset(5, 6)] == 0x2C);
        assert(hgrpaint::grBlockColorAt(gp.data(), 5, 6) == 0xC);
        assert(hgrpaint::grBlockColorAt(gp.data(), 5, 7) == 0x2);
        // Re-plotting the same colour is a no-op; the other nibble is preserved.
        assert(hgrpaint::plotGrBlock(gp.data(), 5, 6, 0xC) == -1);
        // Bounds.
        assert(hgrpaint::grBlockOffset(-1, 0) == -1 && hgrpaint::grBlockOffset(40, 0) == -1);
        assert(hgrpaint::grBlockOffset(0, 48) == -1);
        assert(hgrpaint::grBlockColorAt(gp.data(), 0, 48) == -1);
    }

    std::printf("hgr_paint_plot_smoke: all assertions passed\n");
    return 0;
}
