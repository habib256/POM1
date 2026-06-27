// TMS9918 paint model smoke test — pins the pure VRAM-plotting logic of the
// (forthcoming) TMS9918 Paint editor (tmspaint:: helpers) with no GL/ImGui
// dependency, cross-checked against the REAL TMS9918 per-line renderers.
//
// The model derives its Graphics II / Multicolor VRAM addressing from the chip's
// renderGfxIILineRaw / renderMulticolorLineRaw. This test closes that loop: it
// installs the model's canonical registers + name table, plots a colour through
// the model, renders the 16 KB VRAM through TMS9918::renderToBuffer, and asserts
// the pixel the eye sees is exactly kPalette[colour]. It also pins the
// 2-colours-per-8×1-cell "colour clash" rule and colorAt() round-trips.
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "TmsPaintModel.h"
#include "TMS9918.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using tmspaint::Mode;

// Render a 16 KB VRAM + regs through the real chip into a 256×192 RGBA buffer.
static void renderVram(const std::vector<uint8_t>& vram, const uint8_t regs[8],
                       std::vector<uint32_t>& out)
{
    static TMS9918::Snapshot snap;      // ~500 KB — too big for the stack
    std::copy(vram.begin(), vram.end(), snap.vram.begin());
    std::copy(regs, regs + 8, snap.regs.begin());
    snap.statusReg = 0;
    snap.siliconStrictMode = false;
    out.assign(TMS9918::kScreenWidth * TMS9918::kScreenHeight, 0);
    TMS9918::renderToBuffer(out.data(), snap);
}

static uint32_t pixelAt(const std::vector<uint32_t>& fb, int x, int y)
{
    return fb[static_cast<size_t>(y) * TMS9918::kScreenWidth + x];
}

int main()
{
    // ── Graphics II addressing sanity ────────────────────────────────────────
    assert(tmspaint::gfx2PatternAddr(0, 0)   == 0);
    assert(tmspaint::gfx2ColorAddr(0, 0)     == 0x2000);
    assert(tmspaint::gfx2BitMask(0)          == 0x80);
    assert(tmspaint::gfx2BitMask(7)          == 0x01);
    assert(tmspaint::gfx2PatternAddr(8, 0)   == 8);     // next byte column
    assert(tmspaint::gfx2PatternAddr(0, 1)   == 1);     // next line in cell
    assert(tmspaint::gfx2PatternAddr(0, 8)   == 256);   // next char row (r8=1)
    assert(tmspaint::gfx2PatternAddr(0, 64)  == 2048);  // next section
    assert(tmspaint::gfx2PatternAddr(255, 191) == 6143);// last byte (6144-byte map)
    assert(tmspaint::gfx2PatternAddr(-1, 0)  == -1);
    assert(tmspaint::gfx2PatternAddr(256, 0) == -1);
    assert(tmspaint::gfx2PatternAddr(0, 192) == -1);

    uint8_t regs2[8];
    tmspaint::canonicalRegisters(Mode::GraphicsII, regs2);

    // ── Graphics II: plot → render cross-check ───────────────────────────────
    // For a spread of pixels and ink colours, the displayed pixel must equal the
    // palette entry we plotted. Each in a FRESH cell so bg stays 0 (backdrop).
    {
        const int colours[] = {2, 6, 8, 11, 13, 15};
        const struct { int x, y; } pts[] = {
            {0, 0}, {7, 0}, {1, 1}, {37, 5}, {130, 70}, {200, 130},
            {255, 191}, {64, 64}, {128, 96}, {16, 8},
        };
        int ci = 0;
        for (const auto& p : pts) {
            std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
            tmspaint::writeCanonicalNameTable(vram.data(), Mode::GraphicsII);
            const int c = colours[ci++ % 6];
            assert(tmspaint::plotPixel(vram.data(), Mode::GraphicsII, p.x, p.y, c));
            assert(tmspaint::colorAt(vram.data(), Mode::GraphicsII, p.x, p.y) == c);

            std::vector<uint32_t> fb;
            renderVram(vram, regs2, fb);
            assert(pixelAt(fb, p.x, p.y) == TMS9918::kPalette[c]);
        }
    }

    // ── Graphics II colour-clash rule ────────────────────────────────────────
    // (a) Two different inks in one fresh 8×1 cell: the second recolours the
    //     first (a cell holds only fg+bg, bg stays 0, so both land on fg).
    {
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::writeCanonicalNameTable(vram.data(), Mode::GraphicsII);
        tmspaint::plotPixel(vram.data(), Mode::GraphicsII, 0, 0, 6);   // ink 6 at x0
        tmspaint::plotPixel(vram.data(), Mode::GraphicsII, 1, 0, 9);   // ink 9 at x1
        assert(tmspaint::colorAt(vram.data(), Mode::GraphicsII, 0, 0) == 9); // recoloured
        assert(tmspaint::colorAt(vram.data(), Mode::GraphicsII, 1, 0) == 9);
        // Cell uses exactly fg=9, bg=0.
        const uint8_t colByte = vram[tmspaint::gfx2ColorAddr(0, 0)];
        assert(((colByte >> 4) & 0x0F) == 9 && (colByte & 0x0F) == 0);
    }
    // (b) A new colour added when the cell foreground is busy lands on the
    //     background slot, so two inks coexist on the row.
    {
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::writeCanonicalNameTable(vram.data(), Mode::GraphicsII);
        for (int x = 0; x < 7; ++x)
            tmspaint::plotPixel(vram.data(), Mode::GraphicsII, x, 0, 4); // fg=4
        tmspaint::plotPixel(vram.data(), Mode::GraphicsII, 7, 0, 12);    // → bg=12
        const uint8_t colByte = vram[tmspaint::gfx2ColorAddr(0, 0)];
        assert(((colByte >> 4) & 0x0F) == 4 && (colByte & 0x0F) == 12);
        assert(tmspaint::colorAt(vram.data(), Mode::GraphicsII, 0, 0) == 4);
        assert(tmspaint::colorAt(vram.data(), Mode::GraphicsII, 7, 0) == 12);
        // Both inks survive the render.
        std::vector<uint32_t> fb;
        renderVram(vram, regs2, fb);
        assert(pixelAt(fb, 0, 0) == TMS9918::kPalette[4]);
        assert(pixelAt(fb, 7, 0) == TMS9918::kPalette[12]);
    }

    // ── Multicolor addressing sanity ─────────────────────────────────────────
    assert(tmspaint::mcBlockAddr(0, 0)   == 0);
    assert(tmspaint::mcHighNibble(0)     == true);
    assert(tmspaint::mcHighNibble(1)     == false);
    assert(tmspaint::mcBlockAddr(1, 0)   == 0);     // same byte, low nibble
    assert(tmspaint::mcBlockAddr(2, 0)   == 8);     // next column byte
    assert(tmspaint::mcBlockAddr(0, 1)   == 1);     // subRow within the band
    assert(tmspaint::mcBlockAddr(0, 8)   == 256);   // next 4-row group (g=1)
    assert(tmspaint::mcBlockAddr(-1, 0)  == -1);
    assert(tmspaint::mcBlockAddr(64, 0)  == -1);
    assert(tmspaint::mcBlockAddr(0, 48)  == -1);

    uint8_t regsMC[8];
    tmspaint::canonicalRegisters(Mode::Multicolor, regsMC);

    // ── Multicolor: plot → render cross-check, incl. adjacent-nibble pairs ───
    {
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::writeCanonicalNameTable(vram.data(), Mode::Multicolor);
        const struct { int bx, by, c; } blocks[] = {
            {0, 0, 2}, {1, 0, 8}, {63, 47, 15}, {10, 20, 5}, {33, 9, 11},
            {0, 8, 6}, {0, 16, 13}, {32, 24, 14}, {62, 46, 3},
        };
        for (const auto& b : blocks)
            assert(tmspaint::plotPixel(vram.data(), Mode::Multicolor, b.bx, b.by, b.c));

        std::vector<uint32_t> fb;
        renderVram(vram, regsMC, fb);
        for (const auto& b : blocks) {
            assert(tmspaint::colorAt(vram.data(), Mode::Multicolor, b.bx, b.by) == b.c);
            // Block (bx,by) → screen pixel (bx*4 .. +3, by*4 .. +3); sample centre.
            assert(pixelAt(fb, b.bx * 4 + 1, b.by * 4 + 1) == TMS9918::kPalette[b.c]);
        }
    }

    // ── Multicolor uniqueness: a full gradient must read back block-for-block,
    //    proving every block maps to a distinct nibble (name table correct). ──
    {
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::writeCanonicalNameTable(vram.data(), Mode::Multicolor);
        for (int by = 0; by < tmspaint::kMcHeight; ++by)
            for (int bx = 0; bx < tmspaint::kMcWidth; ++bx) {
                const int c = 1 + ((bx + by) % 15);     // ink 1..15
                tmspaint::plotPixel(vram.data(), Mode::Multicolor, bx, by, c);
            }
        for (int by = 0; by < tmspaint::kMcHeight; ++by)
            for (int bx = 0; bx < tmspaint::kMcWidth; ++bx) {
                const int c = 1 + ((bx + by) % 15);
                assert(tmspaint::colorAt(vram.data(), Mode::Multicolor, bx, by) == c);
            }
    }

    // ── fillRegion: a fresh Graphics II canvas floods to one ink everywhere ──
    {
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::writeCanonicalNameTable(vram.data(), Mode::GraphicsII);
        const int n = tmspaint::fillRegion(vram.data(), Mode::GraphicsII, 0, 0, 7);
        assert(n == tmspaint::kGfx2Width * tmspaint::kGfx2Height);
        assert(tmspaint::colorAt(vram.data(), Mode::GraphicsII, 0, 0)     == 7);
        assert(tmspaint::colorAt(vram.data(), Mode::GraphicsII, 255, 191) == 7);
    }

    std::printf("tms_paint_plot_smoke: OK\n");
    return 0;
}
