// gen2_hgr_inflate_x2 vs HgrSpriteBlit::magnifyColor2x — the ×2-colour mirror pin.
//
// The GEN2-C runtime's mono-×1 → ×2-colour inflate (dev/lib/gen2c/gen2_hgr_x2.c,
// compiled here on the host as plain C) must produce byte-identical output to the
// POM1 HGR Sprite editor's HgrSpriteBlit::magnifyColor2x, so a sprite exported ×2
// from the editor and one inflated at runtime from its mono ×1 master match. Same
// discipline as hgr_convert_smoke: two independent implementations of one transform
// cross-checked on the host. No GL/ImGui/emulator/cc65 dependency — pure integer
// maths both sides. The GEN2_X2_* order == HgrColor order, so casting an HgrColor
// to the runtime's colour arg also pins that numeric mapping.
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "HgrSpriteBlit.h"    // hgrsprite::magnifyColor2x
#include "HgrPaintModel.h"    // hgrpaint::HgrColor (Black=0,White=1,Violet=2,Green=3,Blue=4,Orange=5)

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" void gen2_hgr_inflate_x2(const unsigned char* mono, unsigned char wbytes,
                                    unsigned char h, unsigned char color,
                                    unsigned char* out);

using hgrpaint::HgrColor;

// Build a flat wpx×hRows colour grid from a mono sprite (lit pixels take `col`,
// the rest Black) — exactly how HgrSpriteEditor::buildSpriteBytes feeds
// magnifyColor2x at ×2 — then assert the editor and runtime inflates agree.
static void oneCase(int wBytes, int hRows, HgrColor col, uint32_t seed)
{
    const int wpx = wBytes * 7;
    std::vector<uint8_t> mono(static_cast<size_t>(wBytes) * hRows);
    uint32_t s = seed;                                   // deterministic LCG fill
    for (auto& b : mono) { s = s * 1664525u + 1013904223u; b = static_cast<uint8_t>(s >> 16); }

    std::vector<HgrColor> cells(static_cast<size_t>(wpx) * hRows, HgrColor::Black);
    for (int sy = 0; sy < hRows; ++sy)
        for (int sx = 0; sx < wpx; ++sx)
            if (mono[static_cast<size_t>(sy) * wBytes + sx / 7] & (1u << (sx % 7)))
                cells[static_cast<size_t>(sy) * wpx + sx] = col;

    const size_t n = static_cast<size_t>(wBytes * 2) * (hRows * 2);
    std::vector<uint8_t> expected(n, 0), actual(n, 0);
    hgrsprite::magnifyColor2x(cells.data(), wBytes, hRows, expected.data());
    gen2_hgr_inflate_x2(mono.data(), static_cast<unsigned char>(wBytes),
                        static_cast<unsigned char>(hRows),
                        static_cast<unsigned char>(col), actual.data());
    assert(expected == actual);
}

int main()
{
    const HgrColor cols[6] = { HgrColor::Black, HgrColor::White, HgrColor::Violet,
                               HgrColor::Green, HgrColor::Blue,  HgrColor::Orange };
    const int geo[][2] = { {3, 16}, {2, 8}, {1, 5}, {6, 32}, {4, 10} };
    for (auto& g : geo)
        for (HgrColor c : cols)
            for (uint32_t seed = 1; seed <= 8; ++seed)
                oneCase(g[0], g[1], c, seed * 2654435761u + g[0] * 131u + g[1]);

    std::printf("hgr_inflate_x2_smoke: OK (%zu geometries x 6 colours x 8 seeds)\n",
                sizeof(geo) / sizeof(geo[0]));
    return 0;
}
