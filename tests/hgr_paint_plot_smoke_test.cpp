// HGR paint model smoke test — pins the pure bit-plotting logic of the HGR
// Paint editor (hgrpaint:: helpers) with no GL/ImGui dependency.
//
// Covers: byte/bit addressing via the Apple II interleave, column-parity
// snapping per colour, black/white/chromatic plotting, the per-byte shared
// high-bit (palette) behaviour, pixel read-back, and bounds.
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "HGRPaintEditor_ImGui.h"
#include "GraphicsCard.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using hgrpaint::HgrColor;

int main()
{
    std::vector<uint8_t> page(GraphicsCard::kHiresSize, 0);

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

    std::printf("hgr_paint_plot_smoke: all assertions passed\n");
    return 0;
}
