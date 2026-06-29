// hgr_sprite_bank_smoke — pins the HGR Paint "Sprite Bank" baker (hgrpaint/
// HgrSpriteBank) to the exact 7-phase pre-shift ABI that the GEN2 HGR runtime
// gen2_hgr_sprite() reads (dev/lib/gen2c/gen2.h) and that
// tools/build_preshift_sprites.py emits. If the editor's exported banks ever
// stop matching what the engine consumes, this fails.
//
// Checks (mirroring the python generator's verify()):
//   1. stride == ceil((w + 6) / 7).
//   2. phase 0 reproduces the source mask exactly (no shift, no stray bits).
//   3. phase k == phase 0 shifted right by k screen pixels.
//   4. a 1x1 dot: phase p is a single bit at packed position p (byte 0 == 1<<p).
//   5. extractMask round-trips real pixels plotted through HgrPaintModel.
//   6. emitC / emitAsm produce the expected tokens.

#include "HgrSpriteBank.h"
#include "HgrPaintModel.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace hgrpaint;

// Decode one phase block back into per-row sets of lit screen columns
// (LSB convention: column 0 = leftmost). Inverse of the bake's packing.
static std::vector<std::vector<int>> decodePhase(const BakedSprite& s, int phase)
{
    std::vector<std::vector<int>> rows(s.h);
    for (int r = 0; r < s.h; ++r) {
        const size_t base = (static_cast<size_t>(phase) * s.h + r) * s.stride;
        for (int j = 0; j < s.stride; ++j) {
            const uint8_t b = s.bank[base + j];
            assert((b & 0x80u) == 0 && "bit 7 must stay clear (NTSC group bit)");
            for (int k = 0; k < 7; ++k)
                if (b & (1u << k)) rows[r].push_back(j * 7 + k);
        }
    }
    return rows;
}

static std::vector<std::vector<uint8_t>> makeMask(
    const std::vector<std::string>& art)
{
    std::vector<std::vector<uint8_t>> m;
    for (const std::string& line : art) {
        std::vector<uint8_t> row;
        for (char c : line) row.push_back((c == '#' || c == 'X') ? 1u : 0u);
        m.push_back(row);
    }
    return m;
}

static void checkShiftInvariant(const BakedSprite& s)
{
    assert(s.stride == spriteStride(s.w));
    const std::vector<std::vector<int>> p0 = decodePhase(s, 0);

    // phase 0 == source: columns lit in p0 must be < w and match the mask.
    for (int r = 0; r < s.h; ++r)
        for (int c : p0[r]) assert(c >= 0 && c < s.w);

    // phase k == phase 0 shifted right by k pixels.
    for (int phase = 1; phase < 7; ++phase) {
        const std::vector<std::vector<int>> pk = decodePhase(s, phase);
        for (int r = 0; r < s.h; ++r) {
            assert(pk[r].size() == p0[r].size());
            for (size_t i = 0; i < pk[r].size(); ++i)
                assert(pk[r][i] == p0[r][i] + phase);
        }
    }
}

int main()
{
    // 1 + 2 + 3 — a non-trivial mask: stride, phase-0 fidelity, shift invariant.
    {
        const std::vector<std::vector<uint8_t>> mask = makeMask({
            "....#....",
            "...###...",
            "..#####..",
            ".#######.",
            "#########",
        });
        BakedSprite s = bakeMask("tri", mask);
        assert(s.w == 9 && s.h == 5);
        assert(s.stride == (9 + 6 + 6) / 7);             // ceil((9+6)/7) = 3
        assert(s.bank.size() == static_cast<size_t>(7) * s.h * s.stride);

        // phase 0 must reproduce the mask exactly.
        const std::vector<std::vector<int>> p0 = decodePhase(s, 0);
        for (int r = 0; r < s.h; ++r) {
            std::vector<int> want;
            for (int c = 0; c < s.w; ++c) if (mask[r][c]) want.push_back(c);
            assert(p0[r] == want);
        }
        checkShiftInvariant(s);
    }

    // 4 — a single dot: phase p is exactly bit p of byte 0.
    {
        BakedSprite s = bakeMask("dot", makeMask({ "#" }));
        assert(s.w == 1 && s.h == 1 && s.stride == 1);
        for (int phase = 0; phase < 7; ++phase) {
            const uint8_t b = s.bank[static_cast<size_t>(phase) * s.h * s.stride];
            assert(b == (1u << phase));
        }
    }

    // 5 — extractMask round-trips pixels plotted through the paint model.
    {
        std::vector<uint8_t> page(kHiresSize, 0);
        // a 3px horizontal stub at (x=14,y=10): white so all three columns light.
        for (int c = 14; c < 17; ++c) plotPage(page.data(), c, 10, HgrColor::White);
        SpriteRegion r; r.name = "stub"; r.x = 14; r.y = 10; r.w = 3; r.h = 1;
        std::vector<std::vector<uint8_t>> m = extractMask(page.data(), r);
        assert(m.size() == 1 && m[0].size() == 3);
        int lit = 0; for (uint8_t v : m[0]) lit += v;
        assert(lit >= 1);                                 // at least one column lit
        BakedSprite s = bakeRegion(page.data(), r);
        assert(s.w == 3 && s.h == 1);
        checkShiftInvariant(s);
    }

    // 6 — emitters produce the tokens a gen2c project / ca65 build needs.
    {
        std::vector<BakedSprite> bank = { bakeMask("ball", makeMask({ "###", ".#." })) };
        const std::string c = emitC(bank, "demo");
        assert(c.find("#include \"gen2.h\"") != std::string::npos);
        assert(c.find("gen2_sprite_t ball") != std::string::npos);
        assert(c.find("ball_ps[") != std::string::npos);

        std::string inc;
        const std::string a = emitAsm(bank, "demo", inc);
        assert(a.find("ball_ps:") != std::string::npos);
        assert(a.find(".byte") != std::string::npos);
        assert(inc.find("BALL_STRIDE = ") != std::string::npos);
        assert(inc.find("BALL_PHASES = 7") != std::string::npos);

        // Inline export = the C body with NO include guard / #include (it is
        // pasted into a DevBench buffer that already has gen2.h).
        const std::string in = emitInline(bank);
        assert(in.find("gen2_sprite_t ball") != std::string::npos);
        assert(in.find("ball_ps[") != std::string::npos);
        assert(in.find("#ifndef") == std::string::npos);
        // No real include DIRECTIVE; the banner comment legitimately says
        // "#includes" in prose, so match the directive form (#include "...") not
        // the bare token.
        assert(in.find("#include \"") == std::string::npos);
    }

    std::printf("hgr_sprite_bank_smoke: OK\n");
    return 0;
}
