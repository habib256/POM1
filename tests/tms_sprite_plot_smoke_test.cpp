// TMS9918 sprite model smoke test — pins the pure sprite-VRAM addressing of the
// TMS9918 Sprite editor (tmssprite:: helpers) with no GL/ImGui dependency,
// cross-checked against the REAL TMS9918 sprite renderer (renderSpritesLineRaw,
// reached via TMS9918::renderToBuffer).
//
// The model derives its sprite pattern / SAT addressing from the chip's
// renderSpritesLineRaw. This test closes that loop: it installs the model's
// canonical Graphics-I backdrop + registers, draws sprite pixels through the
// model, places the sprite via the SAT, renders the 16 KB VRAM through
// TMS9918::renderToBuffer, and asserts the pixels the eye sees match the sprite
// colour (set bits) or the backdrop (clear bits) — for both 8×8 and 16×16
// (all four TMS quadrants).
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "TmsSpriteModel.h"
#include "TMS9918.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using tmssprite::Size;

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

// Place a single sprite as SAT entry 0 at screen (sx, sy) and terminate the list.
static void placeSprite(std::vector<uint8_t>& vram, int sx, int sy, int pat, int colour)
{
    tmssprite::writeSatEntry(vram.data(), 0,
                             static_cast<uint8_t>((sy - 1) & 0xFF),  // SAT Y = line-1
                             static_cast<uint8_t>(sx & 0xFF),
                             static_cast<uint8_t>(pat),
                             static_cast<uint8_t>(colour & 0x0F));
    tmssprite::terminateSat(vram.data(), 1);
}

int main()
{
    // ── Addressing sanity ────────────────────────────────────────────────────
    assert(tmssprite::dim(Size::S8)  == 8);
    assert(tmssprite::dim(Size::S16) == 16);
    assert(tmssprite::slotsPerSprite(Size::S8)  == 1);
    assert(tmssprite::slotsPerSprite(Size::S16) == 4);
    assert(tmssprite::bitMask(0) == 0x80);
    assert(tmssprite::bitMask(7) == 0x01);

    // 8×8: pattern slot n starts at base + n*8.
    assert(tmssprite::patternByteAddr(5, Size::S8, 0, 0) == 0x3800 + 5 * 8);
    assert(tmssprite::patternByteAddr(5, Size::S8, 7, 7) == 0x3800 + 5 * 8 + 7);
    assert(tmssprite::patternByteAddr(0, Size::S8, -1, 0) == -1);
    assert(tmssprite::patternByteAddr(0, Size::S8, 8, 0)  == -1);

    // 16×16 quadrant order (top-left, bottom-left, top-right, bottom-right).
    assert(tmssprite::patternByteAddr(8, Size::S16, 0,  0)  == 0x3800 + (8 + 0) * 8 + 0);
    assert(tmssprite::patternByteAddr(8, Size::S16, 15, 0)  == 0x3800 + (8 + 2) * 8 + 0);
    assert(tmssprite::patternByteAddr(8, Size::S16, 0,  15) == 0x3800 + (8 + 1) * 8 + 7);
    assert(tmssprite::patternByteAddr(8, Size::S16, 15, 15) == 0x3800 + (8 + 3) * 8 + 7);
    // Pattern number is masked to a multiple of 4 in 16×16.
    assert(tmssprite::patternByteAddr(10, Size::S16, 0, 0) ==
           tmssprite::patternByteAddr(8,  Size::S16, 0, 0));

    // setPixel / getPixel round-trip.
    {
        std::vector<uint8_t> vram(tmssprite::kVramSize, 0);
        assert(tmssprite::setPixel(vram.data(), 5, Size::S8, 3, 4, true));
        assert(tmssprite::getPixel(vram.data(), 5, Size::S8, 3, 4));
        assert(!tmssprite::getPixel(vram.data(), 5, Size::S8, 3, 5));
        assert(!tmssprite::setPixel(vram.data(), 5, Size::S8, 3, 4, true));  // no change
        assert(tmssprite::setPixel(vram.data(), 5, Size::S8, 3, 4, false));
        assert(!tmssprite::getPixel(vram.data(), 5, Size::S8, 3, 4));
    }

    // ── 8×8: draw → place → render cross-check ───────────────────────────────
    {
        std::vector<uint8_t> vram(tmssprite::kVramSize, 0);
        uint8_t regs[8];
        tmssprite::canonicalRegisters(regs, Size::S8, false, /*backdrop*/4);
        tmssprite::writeCanonicalBackdrop(vram.data(), 4);

        const int pat = 5, colour = 15, sx = 80, sy = 40;
        for (int i = 0; i < 8; ++i)                        // main diagonal
            assert(tmssprite::setPixel(vram.data(), pat, Size::S8, i, i, true));
        placeSprite(vram, sx, sy, pat, colour);

        std::vector<uint32_t> fb;
        renderVram(vram, regs, fb);
        for (int i = 0; i < 8; ++i) {
            assert(pixelAt(fb, sx + i, sy + i) == TMS9918::kPalette[colour]);   // set bit
            if (i != 7)
                assert(pixelAt(fb, sx + i, sy + 7) == TMS9918::kPalette[4]);    // clear → backdrop
        }
        // A pixel well outside the sprite is pure backdrop.
        assert(pixelAt(fb, 0, 0) == TMS9918::kPalette[4]);
    }

    // ── 16×16: one pixel in each quadrant, cross-checked at its screen spot ───
    {
        std::vector<uint8_t> vram(tmssprite::kVramSize, 0);
        uint8_t regs[8];
        tmssprite::canonicalRegisters(regs, Size::S16, false, /*backdrop*/1);
        tmssprite::writeCanonicalBackdrop(vram.data(), 1);

        const int pat = 8, colour = 2, sx = 100, sy = 50;
        const struct { int x, y; } corners[] = {{0,0}, {15,0}, {0,15}, {15,15}};
        for (const auto& c : corners)
            assert(tmssprite::setPixel(vram.data(), pat, Size::S16, c.x, c.y, true));
        placeSprite(vram, sx, sy, pat, colour);

        std::vector<uint32_t> fb;
        renderVram(vram, regs, fb);
        for (const auto& c : corners)
            assert(pixelAt(fb, sx + c.x, sy + c.y) == TMS9918::kPalette[colour]);
        // Sprite centre (unset) shows the backdrop (index 1 → black per renderer).
        const ImU32 bd = TMS9918::kPalette[1];
        assert(pixelAt(fb, sx + 7, sy + 7) == bd);
    }

    // ── Early-Clock bit shifts the sprite 32 px left ─────────────────────────
    {
        std::vector<uint8_t> vram(tmssprite::kVramSize, 0);
        uint8_t regs[8];
        tmssprite::canonicalRegisters(regs, Size::S8, false, 4);
        tmssprite::writeCanonicalBackdrop(vram.data(), 4);
        const int pat = 1, colour = 15, sx = 100, sy = 60;
        for (int i = 0; i < 8; ++i)
            assert(tmssprite::setPixel(vram.data(), pat, Size::S8, i, 0, true));  // top row
        // Early-Clock set: X shifts left by 32.
        tmssprite::writeSatEntry(vram.data(), 0, static_cast<uint8_t>(sy - 1),
                                 static_cast<uint8_t>(sx), static_cast<uint8_t>(pat),
                                 static_cast<uint8_t>(colour | 0x80));
        tmssprite::terminateSat(vram.data(), 1);
        std::vector<uint32_t> fb;
        renderVram(vram, regs, fb);
        assert(pixelAt(fb, sx - 32, sy) == TMS9918::kPalette[colour]);
    }

    std::printf("tms_sprite_plot_smoke: OK\n");
    return 0;
}
