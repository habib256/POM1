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

// CAM16-UCS arithmetic helpers (error diffusion works in this perceptual space).
inline Cam16Ucs operator+(const Cam16Ucs& a, const Cam16Ucs& b)
{ return {a.J + b.J, a.a + b.a, a.b + b.b}; }
inline Cam16Ucs operator-(const Cam16Ucs& a, const Cam16Ucs& b)
{ return {a.J - b.J, a.a - b.a, a.b - b.b}; }
inline Cam16Ucs scale(const Cam16Ucs& a, float s)
{ return {a.J * s, a.a * s, a.b * s}; }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

void imageToHgrPage(const uint8_t* rgba, int srcW, int srcH,
                    const ImportOptions& opt, uint8_t* outPage)
{
    constexpr int W = kHiresWidth, H = kHiresHeight;
    std::fill(outPage, outPage + kHiresSize, uint8_t{0});
    if (!rgba || srcW <= 0 || srcH <= 0) return;

    // ── 1. Resample to W×H (fit + letterbox, or stretch), in CAM16-UCS ────────
    int ox0 = 0, oy0 = 0, ow = W, oh = H;
    if (!opt.stretch) {
        const double s = std::min(static_cast<double>(W) / srcW,
                                  static_cast<double>(H) / srcH);
        ow = std::max(1, static_cast<int>(std::lround(srcW * s)));
        oh = std::max(1, static_cast<int>(std::lround(srcH * s)));
        ox0 = (W - ow) / 2;
        oy0 = (H - oh) / 2;
    }
    const Cam16Ucs kBlackCam = srgb8ToCam16Ucs(0, 0, 0);
    std::vector<Cam16Ucs> tcam(static_cast<size_t>(W) * H, kBlackCam);

    auto sample = [&](float u, float v, float& r, float& g, float& b) {
        u = clampf(u, 0.0f, srcW - 1.0f);
        v = clampf(v, 0.0f, srcH - 1.0f);
        const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
        const int x1 = std::min(x0 + 1, srcW - 1), y1 = std::min(y0 + 1, srcH - 1);
        const float fx = u - x0, fy = v - y0;
        auto px = [&](int x, int y, int c) {
            return rgba[(static_cast<size_t>(y) * srcW + x) * 4 + c] / 255.0f;
        };
        auto lerp2 = [&](int c) {
            const float a = px(x0, y0, c) * (1 - fx) + px(x1, y0, c) * fx;
            const float bb = px(x0, y1, c) * (1 - fx) + px(x1, y1, c) * fx;
            return a * (1 - fy) + bb * fy;
        };
        r = lerp2(0); g = lerp2(1); b = lerp2(2);
    };

    for (int oy = 0; oy < oh; ++oy) {
        for (int ox = 0; ox < ow; ++ox) {
            const float u = (ox + 0.5f) / ow * srcW - 0.5f;
            const float v = (oy + 0.5f) / oh * srcH - 0.5f;
            float r, g, b;
            sample(u, v, r, g, b);
            if (opt.contrast != 1.0f || opt.brightness != 1.0f || opt.gamma != 1.0f) {
                auto adj = [&](float c) {
                    c = (c - 0.5f) * opt.contrast + 0.5f;        // contrast about mid-grey
                    c = clampf(c * opt.brightness, 0.0f, 1.0f);  // brightness
                    if (opt.gamma != 1.0f) c = std::pow(c, 1.0f / opt.gamma);   // gamma
                    return c;
                };
                r = adj(r); g = adj(g); b = adj(b);
            }
            tcam[static_cast<size_t>(oy0 + oy) * W + (ox0 + ox)] = srgbToCam16Ucs(r, g, b);
        }
    }

    // ── 2. Precompute CAM16-UCS of every rendered pixel colour (avg of two
    //       palette entries — only 16×16 distinct values) ──────────────────────
    Cam16Ucs avgCam[16][16];
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) {
            const uint32_t p = avgRgba(kPalette[i], kPalette[j]);
            avgCam[i][j] = srgb8ToCam16Ucs(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
        }

    // ── 3. Per-scanline analysis-by-synthesis + Floyd-Steinberg ──────────────
    std::vector<Cam16Ucs> errCur(W), errNext(W);
    std::fill(errCur.begin(), errCur.end(), Cam16Ucs{0, 0, 0});

    auto guessByte = [&](int y, int bb) -> uint8_t {
        if (bb >= 40) return 0;
        uint8_t v = 0;
        for (int k = 0; k < 7; ++k) {
            const int x = bb * 7 + k;
            if (tcam[static_cast<size_t>(y) * W + x].J > 45.0f) v |= (1u << k);
        }
        return v;   // palette 0; only feeds the right-edge sliding window
    };

    uint16_t wordRow[40] = {0};   // the committed words of the row being decoded
    auto add = [&](std::vector<Cam16Ucs>& buf, int xx, const Cam16Ucs& d) {
        if (xx >= 0 && xx < W) { buf[xx].J += d.J; buf[xx].a += d.a; buf[xx].b += d.b; }
    };

    for (int y = 0; y < H; ++y) {
        std::fill(errNext.begin(), errNext.end(), Cam16Ucs{0, 0, 0});
        const int rowBase = hgrByteOffset(0, y);
        // Serpentine: even rows L→R, odd rows R→L, so Floyd-Steinberg's error
        // doesn't always smear toward the same corner (kills diagonal artifacts).
        const bool ltr = !opt.serpentine || ((y & 1) == 0);
        const int dir = ltr ? 1 : -1;

        for (int n = 0; n < 40; ++n) {
            const int b = ltr ? n : (39 - n);
            const int px0 = b * 7;
            Cam16Ucs want[7];
            for (int k = 0; k < 7; ++k)
                want[k] = tcam[static_cast<size_t>(y) * W + px0 + k] + errCur[px0 + k];

            // The NTSC half-dot carry + sliding window are physically left→right.
            // The neighbour BEHIND us in the scan is already committed (exact); the
            // one AHEAD is guessed from the target and corrected by diffusion.
            uint16_t wordPrev = (b == 0)  ? 0
                              : ltr        ? wordRow[b - 1]
                                           : buildWord(guessByte(y, b - 1), 0);
            const int carry = (wordPrev >> 13) & 1;

            float bestCost = std::numeric_limits<float>::max();
            int bestByte = 0;
            uint16_t bestWord = 0;
            uint8_t bestIdx[7][2] = {};
            for (int cand = 0; cand < 256; ++cand) {
                const uint16_t wCur = buildWord(static_cast<uint8_t>(cand), carry);
                const uint16_t wNext = (b == 39) ? 0
                                     : !ltr        ? wordRow[b + 1]
                                                   : buildWord(guessByte(y, b + 1), (wCur >> 13) & 1);
                uint8_t idx[7][2];
                decodeBytePixels(wordPrev, wCur, wNext, b, idx);
                float cost = 0.0f;
                for (int k = 0; k < 7; ++k)
                    cost += perceptualCost(avgCam[idx[k][0]][idx[k][1]], want[k], opt.chromaWeight);
                if (cost < bestCost) {
                    bestCost = cost; bestByte = cand; bestWord = wCur;
                    for (int k = 0; k < 7; ++k) { bestIdx[k][0] = idx[k][0]; bestIdx[k][1] = idx[k][1]; }
                }
            }

            outPage[rowBase + b] = static_cast<uint8_t>(bestByte);
            wordRow[b] = bestWord;

            if (opt.dither) {
                // Floyd-Steinberg, mirrored for the scan direction. `diffusion`
                // attenuates how much error is carried forward (lower = less grain).
                for (int k = 0; k < 7; ++k) {
                    const Cam16Ucs res = scale(want[k] - avgCam[bestIdx[k][0]][bestIdx[k][1]],
                                               opt.diffusion);
                    const int x = px0 + k;
                    add(errCur,  x + dir, scale(res, 7.0f / 16.0f));   // ahead, same row
                    add(errNext, x - dir, scale(res, 3.0f / 16.0f));
                    add(errNext, x,       scale(res, 5.0f / 16.0f));
                    add(errNext, x + dir, scale(res, 1.0f / 16.0f));
                }
            }
        }
        std::swap(errCur, errNext);
    }
}

} // namespace hgrpaint
