// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Image → HGR conversion — see HgrConvert.h. The NTSC artifact pipeline below is
// a copy of GraphicsCard's MAME-based decode (pinned equal in the test), kept
// here so the portable hgrpaint/ toolkit can render candidate byte patterns
// during dithering without reaching into the emulator.

#include "HgrConvert.h"

#include "Cam16.h"
#include "HgrPaintModel.h"   // kHires*, hgrByteOffset

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hgrpaint {
namespace {

// ─── MAME palette + artifact LUT (verbatim from GraphicsCard.cpp) ─────────────
struct Rgb { uint8_t r, g, b; };
constexpr Rgb kPalette[16] = {
    {0x00,0x00,0x00},{0xa7,0x0b,0x40},{0x40,0x1c,0xf7},{0xe6,0x28,0xff},
    {0x00,0x74,0x40},{0x80,0x80,0x80},{0x19,0x90,0xff},{0xbf,0x9c,0xff},
    {0x40,0x63,0x00},{0xe6,0x6f,0x00},{0x80,0x80,0x80},{0xff,0x8b,0xbf},
    {0x19,0xd7,0x00},{0xbf,0xe3,0x08},{0x58,0xf4,0xbf},{0xff,0xff,0xff},
};

constexpr std::array<uint16_t, 128> makeBitDoubler()
{
    std::array<uint16_t, 128> t{};
    for (unsigned i = 1; i < 128; ++i)
        t[i] = static_cast<uint16_t>(t[i >> 1] * 4 + (i & 1) * 3);
    return t;
}
constexpr std::array<uint16_t, 128> kBitDoubler = makeBitDoubler();

constexpr uint8_t kArtifactColorLut[128] = {
    0x00,0x00,0x00,0x00,0x88,0x00,0xcc,0x00,0x11,0x11,0x55,0x11,0x99,0x99,0xdd,0xff,
    0x22,0x22,0x66,0x66,0xaa,0xaa,0xee,0xee,0x33,0x33,0x33,0x33,0xbb,0xbb,0xff,0xff,
    0x00,0x00,0x44,0x44,0xcc,0xcc,0xcc,0xcc,0x55,0x55,0x55,0x55,0x99,0x99,0xdd,0xff,
    0x66,0x22,0x66,0x66,0xee,0xaa,0xee,0xee,0x77,0x77,0x77,0x77,0xff,0xff,0xff,0xff,
    0x00,0x00,0x00,0x00,0x88,0x88,0x88,0x88,0x11,0x11,0x55,0x11,0x99,0x99,0xdd,0x99,
    0x00,0x22,0x66,0x66,0xaa,0xaa,0xaa,0xaa,0x33,0x33,0x33,0x33,0xbb,0xbb,0xff,0xff,
    0x00,0x00,0x44,0x44,0xcc,0xcc,0xcc,0xcc,0x11,0x11,0x55,0x55,0x99,0x99,0xdd,0xdd,
    0x00,0x22,0x66,0x66,0xee,0xaa,0xee,0xee,0xff,0x33,0xff,0x77,0xff,0xff,0xff,0xff,
};

inline unsigned rotl4b(unsigned n, unsigned count)
{
    return (n >> ((-static_cast<int>(count)) & 3)) & 0x0fu;
}

// Bit-doubled + half-dot word for one byte, given the previous word's carry bit.
inline uint16_t buildWord(uint8_t b, int carry)
{
    uint16_t w = kBitDoubler[b & 0x7Fu];
    if (b & 0x80u) w = static_cast<uint16_t>(((w << 1) | carry) & 0x3FFFu);
    return w;
}

// Decode byte `byteCol`'s 14 sub-pixels into 7 pixels, each a pair of 4-bit
// palette indices (sub-pixel 2k, 2k+1). Needs the neighbour words for the NTSC
// sliding window (3-bit left context from wordPrev's top, right context from
// wordNext's bottom).
inline void decodeBytePixels(uint16_t wordPrev, uint16_t wordCur, uint16_t wordNext,
                             int byteCol, uint8_t idx[7][2])
{
    uint32_t w = (static_cast<uint32_t>(wordPrev >> 11) & 0x7u)
               | (static_cast<uint32_t>(wordCur) << 3)
               | (static_cast<uint32_t>(wordNext) << 17);
    for (int k = 0; k < 7; ++k) {
        for (int s = 0; s < 2; ++s) {
            const int absX = byteCol * 14 + (2 * k + s);
            const uint8_t lut = kArtifactColorLut[w & 0x7Fu];
            idx[k][s] = static_cast<uint8_t>(rotl4b(lut, static_cast<unsigned>(absX)));
            w >>= 1;
        }
    }
}

inline uint32_t avgRgba(Rgb a, Rgb b)
{
    const uint32_t r  = (static_cast<uint32_t>(a.r) + b.r) >> 1;
    const uint32_t g  = (static_cast<uint32_t>(a.g) + b.g) >> 1;
    const uint32_t bl = (static_cast<uint32_t>(a.b) + b.b) >> 1;
    return (uint32_t(0xFFu) << 24) | (bl << 16) | (g << 8) | r;
}

// CAM16-UCS arithmetic helpers (the in-candidate scoring walk works in this
// perceptual space; the committed error diffusion works in linear RGB).
inline Cam16Ucs operator+(const Cam16Ucs& a, const Cam16Ucs& b)
{ return {a.J + b.J, a.a + b.a, a.b + b.b}; }
inline Cam16Ucs operator-(const Cam16Ucs& a, const Cam16Ucs& b)
{ return {a.J - b.J, a.a - b.a, a.b - b.b}; }
inline Cam16Ucs scale(const Cam16Ucs& a, float s)
{ return {a.J * s, a.a * s, a.b * s}; }

// Perceptual cost with the chroma axes weighted by `cw`. CAM16-UCS alone makes a
// same-lightness colour "nearest" to a neutral mid-grey (e.g. bright magenta vs
// 50% grey), so flat greys dither into magenta/green confetti instead of clean
// black/white. Weighting chroma error makes inventing colour expensive, so
// neutral targets dither black/white while real colours still match well. The
// editor exposes `cw` as the "colour noise" slider.
inline float perceptualCost(const Cam16Ucs& rendered, const Cam16Ucs& want, float cw)
{
    const float dJ = rendered.J - want.J;
    const float da = rendered.a - want.a;
    const float db = rendered.b - want.b;
    return dJ * dJ + cw * (da * da + db * db);
}

// ─── Branch-and-bound candidate search ────────────────────────────────────────
// Searches the 256 byte candidates as a DFS over the 7 pattern bits (LSB→MSB,
// palette bit outermost). Pixel k of a byte reads only pattern bits (k−2)..(k+2)
// plus the palette bit (and, for the last two pixels, the right word), so fixing
// bits in LSB order lets whole bit-suffixes SHARE their prefix pixels' cost and
// in-candidate walk state: pixel 0 is evaluated at most 16 times instead of 256,
// pixel 1 at most 32, … — and with the strict bound most subtrees die at depth
// 3-4 after a warm seed. Pruning uses the same strict comparison as the flat
// early abort and acceptance is lexicographic on (cost, candidate index), so the
// result is bit-identical to the exhaustive scan (pinned in the smoke test).
//
// `LeafExtraFn(cand, wCur, budget) -> float` adds a caller cost on top of the
// byte's own 7 pixels (the refinement passes score the two neighbour bytes
// there); the dither pass passes a constant 0.
template <typename LeafExtraFn>
struct ByteSearcher {
    explicit ByteSearcher(LeafExtraFn fn) : leafExtra(fn) {}

    const Cam16Ucs (*avgCam)[16] = nullptr;   // 16×16 rendered-colour table (CAM16)
    const LinRgb (*avgLin)[16] = nullptr;     // …and in linear RGB (walk residuals)
    const Cam16Ucs* wantCam = nullptr;        // 7 effective targets (CAM16)
    const LinRgb* wantLin = nullptr;          // …and in linear RGB
    const Cam16Jac* const* jacs = nullptr;    // 7 local CAM16 Jacobians at wantLin
    const KernelSpec* ker = nullptr;
    float cw = 0.0f, walkScale = 0.0f;
    uint32_t leftCtx = 0;                     // (wordPrev >> 11) & 7
    unsigned absX0 = 0;                       // byteCol * 14
    uint16_t wNextByCarry[2] = {0, 0};        // right word for carry-out 0 / 1
    int carryIn = 0;
    bool prune = true;                        // false = exhaustive reference
    bool strictOnly = false;                  // refinement: keep the seed on ties
    LeafExtraFn leafExtra;

    float bestCost = std::numeric_limits<float>::max();
    int bestByte = -1;
    uint16_t bestWord = 0;

    // Seed the bound (and the fallback winner) with a known candidate's cost.
    // The seed value MUST be computed with the same arithmetic order as a DFS
    // leaf (own walked cost + leafExtra) so ties resolve identically.
    void seed(float cost, int cand, uint16_t word)
    { bestCost = cost; bestByte = cand; bestWord = word; }

    void run()
    {
        for (int palette = 0; palette <= 1; ++palette)
            dfs(0, palette ? static_cast<uint32_t>(carryIn) : 0u, 0.0f, Fw{},
                0, palette);
    }

private:
    struct Fw { LinRgb f[4] = {}; };   // forward walk residuals for k+1..k+4

    // Score pixel k. The in-candidate walk stays in LINEAR RGB — the same space
    // as the committed diffusion (walking in CAM16 would systematically over-
    // credit compensation of dark pixels and break tone conservation); the small
    // walked offset is mapped into CAM16 for scoring via the local Jacobian:
    // cam(want + d) ≈ cam(want) + J·d. Identical operations, in the same order,
    // to walkedCost in imageToHgrPage — the warm/incumbent seeds rely on the
    // floats matching.
    void scorePixel(int k, uint32_t wCur, uint16_t wNext, float& cost, Fw& fw) const
    {
        const uint32_t win = (leftCtx | (wCur << 3)
                              | (static_cast<uint32_t>(wNext) << 17)) >> (2 * k);
        const unsigned absX = absX0 + 2 * k;
        const uint8_t i0 = rotl4b(kArtifactColorLut[win & 0x7Fu], absX);
        const uint8_t i1 = rotl4b(kArtifactColorLut[(win >> 1) & 0x7Fu], absX + 1);
        // Zero-offset fast path (always taken at pixel 0, and everywhere when
        // dither is off): the walked want IS the byte-start want — bit-exact.
        const bool walked = fw.f[0].r != 0.0f || fw.f[0].g != 0.0f || fw.f[0].b != 0.0f;
        LinRgb wlLin;
        Cam16Ucs wl;
        if (walked) {
            wlLin = clamp01(wantLin[k] + fw.f[0]);
            const LinRgb d = wlLin - wantLin[k];
            const float* M = jacs[k]->m;
            wl = {wantCam[k].J + M[0] * d.r + M[1] * d.g + M[2] * d.b,
                  wantCam[k].a + M[3] * d.r + M[4] * d.g + M[5] * d.b,
                  wantCam[k].b + M[6] * d.r + M[7] * d.g + M[8] * d.b};
        } else {
            wlLin = wantLin[k];   // already clamped at byte start
            wl = wantCam[k];
        }
        cost += perceptualCost(avgCam[i0][i1], wl, cw);
        if (walkScale != 0.0f && k < 6) {
            const LinRgb res = scale(wlLin - avgLin[i0][i1], walkScale);
            fw.f[0] = fw.f[1] + scale(res, ker->fwd[0]);
            if (ker->fwdReach > 1) {              // FS keeps f[1..3] at zero
                fw.f[1] = fw.f[2] + scale(res, ker->fwd[1]);
                fw.f[2] = fw.f[3] + scale(res, ker->fwd[2]);
                fw.f[3] = scale(res, ker->fwd[3]);
            }
        }
    }

    void dfs(int depth, uint32_t wCur, float cost, Fw fw, int patBits, int palette)
    {
        if (depth == 7) {
            // Bits all fixed (pixels 0..4 scored on the way down); the carry-out
            // (bit 13) picks the right word, unlocking pixels 5 and 6.
            const uint16_t wNext = wNextByCarry[(wCur >> 13) & 1];
            for (int k = 5; k <= 6; ++k) {
                scorePixel(k, wCur, wNext, cost, fw);
                if (prune && cost > bestCost) return;
            }
            const int cand = patBits | (palette << 7);
            const float budget = prune ? bestCost - cost
                                       : std::numeric_limits<float>::max();
            const float total = cost + leafExtra(cand, static_cast<uint16_t>(wCur),
                                                 budget);
            if (total < bestCost ||
                (!strictOnly && total == bestCost &&
                 (bestByte < 0 || cand < bestByte))) {
                bestCost = total;
                bestByte = cand;
                bestWord = static_cast<uint16_t>(wCur);
            }
            return;
        }
        for (int bit = 0; bit <= 1; ++bit) {
            uint32_t w2 = wCur;
            // Pattern bit `depth` doubles into word bits 2d,2d+1 (palette clear)
            // or 2d+1,2d+2 with bit 14 dropped (palette set) — same as buildWord.
            if (bit) w2 |= (0x3u << (2 * depth + palette)) & 0x3FFFu;
            float c2 = cost;
            Fw fw2 = fw;
            const int k = depth - 2;   // pixel unlocked by fixing bit `depth`
            if (k >= 0) {
                scorePixel(k, w2, 0, c2, fw2);   // k ≤ 4 never reads the right word
                if (prune && c2 > bestCost) continue;
            }
            dfs(depth + 1, w2, c2, fw2, patBits | (bit << depth), palette);
        }
    }
};

} // namespace

void hgrDecodeScanlineRgb(const uint8_t bytes[40], uint32_t out[280])
{
    uint16_t words[40];
    int carry = 0;
    for (int b = 0; b < 40; ++b) {
        words[b] = buildWord(bytes[b], carry);
        carry = (words[b] >> 13) & 1;
    }
    for (int b = 0; b < 40; ++b) {
        const uint16_t wp = (b > 0)      ? words[b - 1] : 0;
        const uint16_t wn = (b < 39)     ? words[b + 1] : 0;
        uint8_t idx[7][2];
        decodeBytePixels(wp, words[b], wn, b, idx);
        for (int k = 0; k < 7; ++k)
            out[b * 7 + k] = avgRgba(kPalette[idx[k][0]], kPalette[idx[k][1]]);
    }
}

void imageToGrPage(const uint8_t* rgba, int srcW, int srcH,
                   const ImportOptions& opt, uint8_t* outPage)
{
    // GR (Apple II lo-res) has none of HGR's NTSC coupling: it is a flat grid of
    // kGrCols×kGrRows blocks, each a single one-of-16 palette index (kPalette here
    // is bit-identical to GraphicsCard::kApple2Palette). So the importer is the
    // simple case — resample to block resolution, quantise each block to the
    // nearest palette colour in CAM16-UCS, and Floyd-Steinberg / jarvis-mod
    // diffuse the residual in linear RGB (same machinery + knobs as the TMS
    // Multicolor path). Output is the 1 KB text-page layout via plotGrBlock.
    constexpr int W = kGrCols, H = kGrRows;
    std::fill(outPage, outPage + static_cast<size_t>(0x400), uint8_t{0});
    if (!rgba || srcW <= 0 || srcH <= 0) return;

    // Per-palette CAM16 (scoring) + linear RGB (diffusion) — all 16 colours are
    // candidates (index 0 = black is a legal block, unlike TMS's transparent 0).
    Cam16Ucs cam[16];
    LinRgb   lin[16];
    for (int i = 0; i < 16; ++i) {
        cam[i] = srgb8ToCam16Ucs(kPalette[i].r, kPalette[i].g, kPalette[i].b);
        lin[i] = {srgb8ToLinearF(kPalette[i].r), srgb8ToLinearF(kPalette[i].g),
                  srgb8ToLinearF(kPalette[i].b)};
    }

    std::vector<LinRgb> tlin;
    int ox0, oy0, ow, oh;
    resampleToLinearRgb(rgba, srcW, srcH, W, H, opt, tlin, ox0, oy0, ow, oh);

    const KernelSpec& ker = kernelSpec(opt.kernel);
    const float cw = opt.chromaWeight;
    std::vector<LinRgb> errRow[3];
    for (auto& e : errRow) e.assign(W, LinRgb{0, 0, 0});
    const int colLo = ox0, colHi = ox0 + ow;
    auto addErr = [&](std::vector<LinRgb>& buf, int xx, const LinRgb& d) {
        if (xx >= colLo && xx < colHi) buf[xx] = buf[xx] + d;
    };
    auto rotateErrRows = [&] {
        std::swap(errRow[0], errRow[1]);
        std::swap(errRow[1], errRow[2]);
        std::fill(errRow[2].begin(), errRow[2].end(), LinRgb{0, 0, 0});
    };

    for (int by = 0; by < H; ++by) {
        if (by < oy0 || by >= oy0 + oh) { rotateErrRows(); continue; }
        for (int bx = 0; bx < W; ++bx) {
            if (bx < colLo || bx >= colHi) continue;   // letterbox stays black
            const LinRgb want = clamp01(tlin[static_cast<size_t>(by) * W + bx]
                                        + errRow[0][bx]);
            const Cam16Ucs wantCam = linearSrgbToCam16Ucs(want.r, want.g, want.b);
            int best = 0;
            float bestCost = std::numeric_limits<float>::max();
            for (int i = 0; i < 16; ++i) {
                const float c = perceptualCost(cam[i], wantCam, cw);
                if (c < bestCost) { bestCost = c; best = i; }
            }
            if (opt.dither) {
                const LinRgb res = scale(want - lin[best], opt.diffusion);
                for (int d = 1; d <= ker.fwdReach; ++d)
                    addErr(errRow[0], bx + d, scale(res, ker.fwd[d - 1]));
                for (int r = 1; r <= ker.belowRows; ++r)
                    for (int dx = -2; dx <= 4; ++dx) {
                        const float wgt = ker.below[r - 1][dx + 2];
                        if (wgt != 0.0f) addErr(errRow[r], bx + dx, scale(res, wgt));
                    }
            }
            plotGrBlock(outPage, bx, by, best);
        }
        rotateErrRows();
    }
}

void imageToHgrPage(const uint8_t* rgba, int srcW, int srcH,
                    const ImportOptions& opt, uint8_t* outPage, ImportStats* stats)
{
    constexpr int W = kHiresWidth, H = kHiresHeight;
    std::fill(outPage, outPage + kHiresSize, uint8_t{0});
    if (stats) *stats = ImportStats{};
    if (!rgba || srcW <= 0 || srcH <= 0) return;

    // ── 1. Resample to W×H in LINEAR RGB (shared resampler, ImportCommon.h) ───
    // ii-pix rationale: the bilinear average and the error diffusion both happen
    // in linear light; averaging gamma-encoded values darkened thin bright detail.
    std::vector<LinRgb> tlin;
    int ox0 = 0, oy0 = 0, ow = W, oh = H;
    resampleToLinearRgb(rgba, srcW, srcH, W, H, opt, tlin, ox0, oy0, ow, oh);

    // ── 2. Precompute, for every rendered pixel colour (avg of two palette
    //       entries — only 16×16 distinct values), its CAM16-UCS (scoring) and
    //       its linear RGB (error diffusion) ────────────────────────────────────
    Cam16Ucs avgCam[16][16];
    LinRgb   avgLin[16][16];
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) {
            const uint32_t p = avgRgba(kPalette[i], kPalette[j]);
            avgCam[i][j] = srgb8ToCam16Ucs(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
            avgLin[i][j] = {srgb8ToLinearF(static_cast<uint8_t>(p & 0xFF)),
                            srgb8ToLinearF(static_cast<uint8_t>((p >> 8) & 0xFF)),
                            srgb8ToLinearF(static_cast<uint8_t>((p >> 16) & 0xFF))};
        }

    const KernelSpec& ker = kernelSpec(opt.kernel);
    const float cw = opt.chromaWeight;
    // The in-candidate walk mirrors the committed diffusion, so it scales by the
    // same knobs (dither off ⇒ no walk).
    const float walkScale = opt.dither ? opt.diffusion : 0.0f;

    // ── 3. Dither pass: per-byte analysis-by-synthesis, LTR only ──────────────
    // (Serpentine is deliberately NOT honoured here: the NTSC half-dot carry +
    // sliding window are physically left→right, so an RTL row would score every
    // candidate against a fabricated left word. The refinement passes below fix
    // the directional-smear problem serpentine used to paper over.)
    //
    // Error is carried in LINEAR RGB and clamped at the point of use — the
    // effective target is want = clamp(target + err, 0, 1) — so out-of-gamut
    // demands can't snowball into "worm" trails and diffusion = 1.0 is safe
    // (ii-pix semantics). The clamped wants are converted to CAM16 once per byte
    // (not per candidate) for perceptual scoring, and frozen for refinement.
    std::vector<LinRgb> errRow[3];   // this row / next / next+1 (jarvis-mod needs y+2)
    for (auto& e : errRow) e.assign(W, LinRgb{0, 0, 0});
    // Frozen effective targets for the refinement passes: CAM16 + linear RGB per
    // pixel, plus the local CAM16 Jacobian at each want (nodes of a shared static
    // lattice — the DFS and the flat walkedCost must fetch the SAME object so
    // their floats match).
    const Cam16Ucs camBlack = srgb8ToCam16Ucs(0, 0, 0);
    std::vector<Cam16Ucs> wantCamPage(static_cast<size_t>(W) * H, camBlack);
    std::vector<LinRgb> wantLinPage(static_cast<size_t>(W) * H, LinRgb{0, 0, 0});
    std::vector<const Cam16Jac*> jacPage(static_cast<size_t>(W) * H,
                                         &cam16JacobianAt(0, 0, 0));

    // Active (non-letterbox) span: [colActiveLo,colActiveHi) × [oy0,oy0+oh). In
    // stretch mode this is the whole frame. Confining diffusion to it stops the
    // quantization residual from bleeding into the black letterbox bars (speckle).
    const int colActiveLo = ox0, colActiveHi = ox0 + ow;
    const int rowActiveLo = oy0, rowActiveHi = oy0 + oh;
    auto addErr = [&](std::vector<LinRgb>& buf, int xx, const LinRgb& d) {
        if (xx < colActiveLo || xx >= colActiveHi) return;
        buf[xx] = buf[xx] + d;
        if (stats)
            stats->maxErrAbs = std::max({stats->maxErrAbs, std::fabs(buf[xx].r),
                                         std::fabs(buf[xx].g), std::fabs(buf[xx].b)});
    };

    // Right-hand context guess for the dither pass only (refinement replaces it
    // with the ACTUAL committed word). Linear luma 0.15 ≈ the old CAM16 J > 45
    // mid-lightness threshold.
    auto guessByte = [&](int y, int bb) -> uint8_t {
        if (bb >= 40) return 0;
        uint8_t v = 0;
        for (int k = 0; k < 7; ++k) {
            const LinRgb& t = tlin[static_cast<size_t>(y) * W + bb * 7 + k];
            if (0.2126f * t.r + 0.7152f * t.g + 0.0722f * t.b > 0.15f) v |= (1u << k);
        }
        return v;   // palette 0; only feeds the right-edge sliding window
    };

    // Walked perceptual cost of one byte's 7 pixels, given its neighbour words.
    // ii-pix apply_one_line semantics: pixel k's residual is diffused into the
    // following pixels of the SAME candidate before they are scored, so a
    // candidate is credited for compensating its own early error. The walk stays
    // in LINEAR RGB (the space of the committed diffusion — walking in CAM16
    // would over-credit dark-pixel compensation and break tone conservation);
    // the walked offset is mapped into CAM16 for scoring via the local Jacobian.
    // The cost accumulates pixel by pixel and bails out as soon as the partial
    // sum EXCEEDS `bound` (strictly — a tying candidate must finish, so
    // tie-breaking stays identical to the exhaustive search). The NTSC decode is
    // fused into the loop (same maths as decodeBytePixels) so the abort skips
    // the decode work of the remaining pixels too. Must stay float-identical to
    // ByteSearcher::scorePixel (the warm/incumbent seeds rely on it).
    const float kInf = std::numeric_limits<float>::max();
    auto walkedCost = [&](uint16_t wp, uint16_t wc, uint16_t wn, int bcol,
                          const Cam16Ucs* wantC, const LinRgb* wantL,
                          const Cam16Jac* const* jacs, float bound) {
        uint32_t w = (static_cast<uint32_t>(wp >> 11) & 0x7u)
                   | (static_cast<uint32_t>(wc) << 3)
                   | (static_cast<uint32_t>(wn) << 17);
        unsigned absX = static_cast<unsigned>(bcol) * 14u;
        LinRgb fw[4] = {};   // forward residuals pending for pixels k+1..k+4
        float cost = 0.0f;
        for (int k = 0; k < 7; ++k) {
            const uint8_t i0 = rotl4b(kArtifactColorLut[w & 0x7Fu], absX);
            w >>= 1; ++absX;
            const uint8_t i1 = rotl4b(kArtifactColorLut[w & 0x7Fu], absX);
            w >>= 1; ++absX;
            // Zero-offset fast path — must mirror ByteSearcher::scorePixel.
            const bool walked = fw[0].r != 0.0f || fw[0].g != 0.0f || fw[0].b != 0.0f;
            LinRgb wlLin;
            Cam16Ucs wl;
            if (walked) {
                wlLin = clamp01(wantL[k] + fw[0]);
                const LinRgb d = wlLin - wantL[k];
                const float* M = jacs[k]->m;
                wl = {wantC[k].J + M[0] * d.r + M[1] * d.g + M[2] * d.b,
                      wantC[k].a + M[3] * d.r + M[4] * d.g + M[5] * d.b,
                      wantC[k].b + M[6] * d.r + M[7] * d.g + M[8] * d.b};
            } else {
                wlLin = wantL[k];
                wl = wantC[k];
            }
            cost += perceptualCost(avgCam[i0][i1], wl, cw);
            if (cost > bound) return cost;          // early abort: can't win or tie
            if (walkScale != 0.0f && k < 6) {
                const LinRgb res = scale(wlLin - avgLin[i0][i1], walkScale);
                fw[0] = fw[1] + scale(res, ker.fwd[0]);
                if (ker.fwdReach > 1) {             // FS keeps fw[1..3] at zero
                    fw[1] = fw[2] + scale(res, ker.fwd[1]);
                    fw[2] = fw[3] + scale(res, ker.fwd[2]);
                    fw[3] = scale(res, ker.fwd[3]);
                }
            }
        }
        return cost;
    };

    auto rotateErrRows = [&] {
        std::swap(errRow[0], errRow[1]);
        std::swap(errRow[1], errRow[2]);
        std::fill(errRow[2].begin(), errRow[2].end(), LinRgb{0, 0, 0});
    };

    uint16_t wordRow[40] = {0};       // the committed words of the row being decoded
    uint8_t  prevRowByte[40] = {0};   // warm-start seeds (previous row's choices)

    for (int y = 0; y < H; ++y) {
        const int rowBase = hgrByteOffset(0, y);
        // Letterbox row: leave the page bytes 0 and carry no error into it, so the
        // top/bottom bars stay pure black. The rotate makes the next row start clean.
        if (y < rowActiveLo || y >= rowActiveHi) { rotateErrRows(); continue; }

        for (int b = 0; b < 40; ++b) {
            const int px0 = b * 7;
            // Skip letterbox columns entirely: leave the byte 0 (pure black) and
            // its word 0, so neighbours see black context and no error is spent.
            if (px0 + 7 <= colActiveLo || px0 >= colActiveHi) {
                outPage[rowBase + b] = 0; wordRow[b] = 0; prevRowByte[b] = 0;
                continue;
            }
            // Effective per-pixel targets: clamp(target + err) in linear RGB,
            // converted to CAM16 once per byte (NOT per candidate), plus the
            // local Jacobian used to score walked offsets.
            LinRgb   wantLin[7];
            Cam16Ucs wantCam[7];
            const Cam16Jac* jac[7];
            for (int k = 0; k < 7; ++k) {
                const size_t px = static_cast<size_t>(y) * W + px0 + k;
                wantLin[k] = clamp01(tlin[px] + errRow[0][px0 + k]);
                wantCam[k] = linearSrgbToCam16Ucs(wantLin[k].r, wantLin[k].g, wantLin[k].b);
                jac[k] = &cam16JacobianAt(wantLin[k].r, wantLin[k].g, wantLin[k].b);
                wantCamPage[px] = wantCam[k];
                wantLinPage[px] = wantLin[k];
                jacPage[px] = jac[k];
            }

            // The NTSC half-dot carry + sliding window are physically left→right:
            // the left neighbour is committed (exact); the right one is guessed
            // here and corrected by the refinement passes below.
            const uint16_t wordPrev = (b == 0) ? 0 : wordRow[b - 1];
            const int carry = (wordPrev >> 13) & 1;
            // guessByte(y,b+1) depends only on (y,b+1), not on the candidate — hoist
            // it out of the 256-iteration loop; only buildWord's carry varies, so
            // precompute both carry variants of the right word too.
            const uint8_t aheadByte = (b < 39) ? guessByte(y, b + 1) : 0;
            const uint16_t wNextC[2] = {
                (b == 39) ? uint16_t{0} : buildWord(aheadByte, 0),
                (b == 39) ? uint16_t{0} : buildWord(aheadByte, 1),
            };

            auto noExtra = [](int, uint16_t, float) { return 0.0f; };
            ByteSearcher<decltype(noExtra)> search(noExtra);
            search.avgCam = avgCam;
            search.avgLin = avgLin;
            search.wantCam = wantCam;
            search.wantLin = wantLin;
            search.jacs = jac;
            search.ker = &ker;
            search.cw = cw;
            search.walkScale = walkScale;
            search.leftCtx = (static_cast<uint32_t>(wordPrev) >> 11) & 0x7u;
            search.absX0 = static_cast<unsigned>(b) * 14u;
            search.wNextByCarry[0] = wNextC[0];
            search.wNextByCarry[1] = wNextC[1];
            search.carryIn = carry;
            search.prune = !opt.exhaustiveSearch;
            // Warm start: the byte chosen at this column on the PREVIOUS row is
            // usually near-optimal (images are vertically coherent), so seeding
            // its cost lets the DFS prune most subtrees at depth 3-4. The seed
            // never changes tie-breaking: acceptance is (cost, index)
            // lexicographic, so a lower-index candidate tying the seed still wins
            // — pinned bit-identical to the exhaustive search in the smoke test.
            if (!opt.exhaustiveSearch) {
                const uint8_t warm = prevRowByte[b];
                const uint16_t wWarm = buildWord(warm, carry);
                search.seed(walkedCost(wordPrev, wWarm, wNextC[(wWarm >> 13) & 1],
                                       b, wantCam, wantLin, jac, kInf) + 0.0f,
                            warm, wWarm);
            }
            search.run();
            const int bestByte = search.bestByte;
            const uint16_t bestWord = search.bestWord;

            outPage[rowBase + b] = static_cast<uint8_t>(bestByte);
            wordRow[b] = bestWord;
            prevRowByte[b] = static_cast<uint8_t>(bestByte);
            // Recover the winner's per-pixel palette indices once (instead of
            // hauling them out of every candidate evaluation).
            uint8_t bestIdx[7][2];
            decodeBytePixels(wordPrev, bestWord, wNextC[(bestWord >> 13) & 1], b, bestIdx);

            if (opt.dither) {
                // COMMIT-time diffusion: redo the winning candidate's sequential
                // walk in LINEAR RGB, emitting each pixel's residual to its
                // positionally correct targets — forward taps that stay inside
                // the byte feed the walk itself; taps crossing the byte boundary
                // land on errRow[0] at px0+k+d (read by the next byte's wants);
                // the below-row taps go to errRow[1..2]. This replaces the old
                // uniform byteCarry/7 smear, which averaged the whole byte's
                // forward error over the next byte's 7 pixels and blurred edges.
                LinRgb wl[7];
                for (int k = 0; k < 7; ++k) wl[k] = wantLin[k];
                for (int k = 0; k < 7; ++k) {
                    wl[k] = clamp01(wl[k]);   // walked wants stay in gamut too
                    const LinRgb res = scale(wl[k] - avgLin[bestIdx[k][0]][bestIdx[k][1]],
                                             opt.diffusion);
                    for (int d = 1; d <= ker.fwdReach; ++d) {
                        const LinRgb t = scale(res, ker.fwd[d - 1]);
                        if (k + d < 7) wl[k + d] = wl[k + d] + t;
                        else           addErr(errRow[0], px0 + k + d, t);
                    }
                    for (int r = 1; r <= ker.belowRows; ++r)
                        for (int dx = -2; dx <= 4; ++dx) {
                            const float wgt = ker.below[r - 1][dx + 2];
                            if (wgt != 0.0f)
                                addErr(errRow[r], px0 + k + dx, scale(res, wgt));
                        }
                }
            }
        }
        rotateErrRows();
    }

    // ── 4. Cross-byte ICM refinement (replaces trusting the guessByte context) ─
    // Each byte is re-chosen against the ACTUAL committed neighbours: the exact
    // left word/carry AND the committed right byte's word, rebuilt per candidate
    // because the candidate's half-dot carry feeds the right byte's palette-bit
    // shift. A move is accepted only if it strictly lowers the summed walked cost
    // of the 3 bytes whose pixels it can touch (the sliding window reaches ≤2 px
    // into a neighbour; the carry only flips the right neighbour's own bit 0 and
    // never propagates further), so the global frozen-target objective decreases
    // monotonically (pinned in the smoke test). Targets are the dither pass's
    // frozen effective wants, so refinement sharpens colour fringes at byte seams
    // without undoing the dither density. A pass changing <0.5% of bytes ends it.
    auto wordAt = [](const uint16_t* words, int b) -> uint16_t {
        return (b >= 0 && b < 40) ? words[b] : 0;
    };
    auto buildRowWords = [&](const uint8_t* rowBytes, uint16_t words[40]) {
        int carry = 0;
        for (int b = 0; b < 40; ++b) {
            words[b] = buildWord(rowBytes[b], carry);
            carry = (words[b] >> 13) & 1;
        }
    };
    // Global frozen-target objective (for the monotonicity pin / telemetry).
    auto totalCost = [&]() {
        double sum = 0.0;
        for (int y = rowActiveLo; y < rowActiveHi; ++y) {
            const uint8_t* rowBytes = outPage + hgrByteOffset(0, y);
            uint16_t words[40];
            buildRowWords(rowBytes, words);
            const size_t row = static_cast<size_t>(y) * W;
            for (int b = 0; b < 40; ++b)
                sum += walkedCost(wordAt(words, b - 1), words[b], wordAt(words, b + 1),
                                  b, &wantCamPage[row + b * 7], &wantLinPage[row + b * 7],
                                  &jacPage[row + b * 7], kInf);
        }
        return static_cast<float>(sum);
    };
    if (stats) stats->passCost.push_back(totalCost());

    int refinableBytesPerRow = 0;
    for (int b = 0; b < 40; ++b)
        if (!(b * 7 + 7 <= colActiveLo || b * 7 >= colActiveHi)) ++refinableBytesPerRow;
    const int refinableBytes =
        std::max(1, (rowActiveHi - rowActiveLo) * refinableBytesPerRow);

    // Dirty tracking across passes: a move at byte b can only change the move
    // objective of bytes within ±2 columns of it on the same row (the sliding
    // window reads ≤2 px / 3 context bits into a neighbour and the half-dot
    // carry only flips the right neighbour's own bit 0, whose context influence
    // stops there); a byte whose context and frozen targets are unchanged since
    // its last search would just re-pick its incumbent, so skipping it is
    // EXACT — later passes only re-visit the neighbourhoods of actual changes.
    std::vector<uint8_t> dirty(static_cast<size_t>(H) * 40, 1);
    std::vector<uint8_t> dirtyNext(static_cast<size_t>(H) * 40, 0);

    for (int pass = 0; pass < opt.refinePasses; ++pass) {
        int changed = 0;
        for (int y = rowActiveLo; y < rowActiveHi; ++y) {
            uint8_t* rowBytes = outPage + hgrByteOffset(0, y);
            uint16_t words[40];
            buildRowWords(rowBytes, words);
            for (int b = 0; b < 40; ++b) {
                const int px0 = b * 7;
                if (px0 + 7 <= colActiveLo || px0 >= colActiveHi) continue;   // letterbox
                if (!dirty[static_cast<size_t>(y) * 40 + b]) continue;        // unaffected
                const int incumbent = rowBytes[b];
                const uint16_t wPrev = wordAt(words, b - 1);
                const int carryIn = (wPrev >> 13) & 1;
                const uint16_t wNextRC[2] = {
                    (b < 39) ? buildWord(rowBytes[b + 1], 0) : uint16_t{0},
                    (b < 39) ? buildWord(rowBytes[b + 1], 1) : uint16_t{0},
                };
                const Cam16Ucs* wantRow = &wantCamPage[static_cast<size_t>(y) * W];
                const LinRgb* wantLRow = &wantLinPage[static_cast<size_t>(y) * W];
                const Cam16Jac* const* jacRow = &jacPage[static_cast<size_t>(y) * W];

                // The neighbour terms of the move objective depend on the
                // candidate only through a couple of pattern bits: byte b−1's
                // last two pixels read wCur bits 0..2 (⇐ palette, pat0, pat1) and
                // byte b+1's decode reads wCur bits 11..13 + the carry-out
                // (⇐ palette, pat5, pat6). Each term therefore takes at most 8
                // values — precompute them per byte, shift both tables by their
                // minimum (a constant offset can't change the argmin) so the DFS
                // bound stays tight, and the leaf cost collapses to two lookups.
                float nb0[8] = {}, nb1[8] = {};   // b−1 / b+1 terms by group key
                if (b > 0)
                    for (int g = 0; g < 8; ++g) {
                        const uint8_t rep = static_cast<uint8_t>(((g & 4) << 5) | (g & 3));
                        nb0[g] = walkedCost(wordAt(words, b - 2), wPrev,
                                            buildWord(rep, carryIn), b - 1,
                                            wantRow + (b - 1) * 7,
                                            wantLRow + (b - 1) * 7,
                                            jacRow + (b - 1) * 7, kInf);
                    }
                if (b < 39)
                    for (int g = 0; g < 8; ++g) {
                        const uint8_t rep = static_cast<uint8_t>(((g & 4) << 5) | ((g & 3) << 5));
                        const uint16_t wRep = buildWord(rep, carryIn);
                        nb1[g] = walkedCost(wRep, wNextRC[(wRep >> 13) & 1],
                                            wordAt(words, b + 2), b + 1,
                                            wantRow + (b + 1) * 7,
                                            wantLRow + (b + 1) * 7,
                                            jacRow + (b + 1) * 7, kInf);
                    }
                const float min0 = *std::min_element(nb0, nb0 + 8);
                const float min1 = *std::min_element(nb1, nb1 + 8);
                for (float& v : nb0) v -= min0;
                for (float& v : nb1) v -= min1;
                auto neighbourCost = [&](int cand, uint16_t, float) {
                    const int pal = (cand >> 5) & 4;   // palette bit → key bit 2
                    return nb0[pal | (cand & 3)] + nb1[pal | ((cand >> 5) & 3)];
                };
                ByteSearcher<decltype(neighbourCost)> search(neighbourCost);
                search.avgCam = avgCam;
                search.avgLin = avgLin;
                search.wantCam = wantRow + b * 7;
                search.wantLin = wantLRow + b * 7;
                search.jacs = jacRow + b * 7;
                search.ker = &ker;
                search.cw = cw;
                search.walkScale = walkScale;
                search.leftCtx = (static_cast<uint32_t>(wPrev) >> 11) & 0x7u;
                search.absX0 = static_cast<unsigned>(b) * 14u;
                search.wNextByCarry[0] = wNextRC[0];
                search.wNextByCarry[1] = wNextRC[1];
                search.carryIn = carryIn;
                search.prune = !opt.exhaustiveSearch;
                search.strictOnly = true;   // ties keep the incumbent → monotone
                // The incumbent seeds the bound; only strict improvements are
                // accepted, which is what makes the pass monotone.
                const uint16_t wInc = buildWord(static_cast<uint8_t>(incumbent), carryIn);
                const float ownInc = walkedCost(wPrev, wInc, wNextRC[(wInc >> 13) & 1],
                                                b, wantRow + b * 7, wantLRow + b * 7,
                                                jacRow + b * 7, kInf);
                search.seed(ownInc + neighbourCost(incumbent, wInc, kInf),
                            incumbent, wInc);
                search.run();
                if (search.bestByte != incumbent) {
                    rowBytes[b] = static_cast<uint8_t>(search.bestByte);
                    words[b] = buildWord(rowBytes[b], carryIn);
                    if (b < 39)
                        words[b + 1] = buildWord(rowBytes[b + 1], (words[b] >> 13) & 1);
                    ++changed;
                    for (int d = -2; d <= 2; ++d)
                        if (b + d >= 0 && b + d < 40)
                            dirtyNext[static_cast<size_t>(y) * 40 + b + d] = 1;
                }
            }
        }
        if (stats) {
            stats->passChanged.push_back(changed);
            stats->passCost.push_back(totalCost());
        }
        if (changed * 200 < refinableBytes) break;   // <0.5% changed → converged
        std::swap(dirty, dirtyNext);
        std::fill(dirtyNext.begin(), dirtyNext.end(), uint8_t{0});
    }
}

} // namespace hgrpaint
