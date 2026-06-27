// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Image → TMS9918 conversion — see TmsConvert.h. Scores candidate palette colours
// in CAM16-UCS (shared hgrpaint::Cam16) and Floyd-Steinberg-dithers; no NTSC
// synthesis (the TMS palette is 15 fixed RGB colours).

#include "TmsConvert.h"

#include "Cam16.h"           // hgrpaint::Cam16 (shared, pure)
#include "TmsPaintModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace tmspaint {
namespace {

using hgrpaint::Cam16Ucs;
using hgrpaint::srgbToCam16Ucs;
using hgrpaint::srgb8ToCam16Ucs;

// The 16 TMS9918 palette colours (index 0 = transparent → treated as black for
// the distance metric). Byte-identical to TMS9918::kPalette / the editor swatch.
struct Rgb { uint8_t r, g, b; };
const Rgb kTmsRgb[16] = {
    {  0,  0,  0}, {  0,  0,  0}, { 33,200, 66}, { 94,220,120},
    { 84, 85,237}, {125,118,252}, {212, 82, 77}, { 66,235,245},
    {252, 85, 84}, {255,121,120}, {212,193, 84}, {230,206,128},
    { 33,176, 59}, {201, 91,186}, {204,204,204}, {255,255,255},
};
// Candidate inks: every opaque colour 1..15 (0 is just black again).
const int kInk[15] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

inline Cam16Ucs operator+(const Cam16Ucs& a, const Cam16Ucs& b)
{ return {a.J + b.J, a.a + b.a, a.b + b.b}; }
inline Cam16Ucs operator-(const Cam16Ucs& a, const Cam16Ucs& b)
{ return {a.J - b.J, a.a - b.a, a.b - b.b}; }
inline Cam16Ucs scale(const Cam16Ucs& a, float s) { return {a.J * s, a.a * s, a.b * s}; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Perceptual cost with weighted chroma axes (see HgrConvert: the "colour noise"
// knob makes inventing chroma expensive so neutral targets dither clean B/W).
inline float perceptualCost(const Cam16Ucs& got, const Cam16Ucs& want, float cw)
{
    const float dJ = got.J - want.J, da = got.a - want.a, db = got.b - want.b;
    return dJ * dJ + cw * (da * da + db * db);
}

// Resample the (cropped) source into a W×H CAM16-UCS target buffer with optional
// fit+letterbox, brightness/contrast/gamma. Reports the active (non-letterbox)
// rect [ox0,ox0+ow) × [oy0,oy0+oh).
void resampleToCam(const uint8_t* rgba, int srcW, int srcH, int W, int H,
                   const ImportOptions& opt, std::vector<Cam16Ucs>& tcam,
                   int& ox0, int& oy0, int& ow, int& oh)
{
    const Cam16Ucs kBlackCam = srgb8ToCam16Ucs(0, 0, 0);
    tcam.assign(static_cast<size_t>(W) * H, kBlackCam);

    int cx0 = opt.cropX0, cy0 = opt.cropY0, cx1 = opt.cropX1, cy1 = opt.cropY1;
    if (cx1 <= cx0 || cy1 <= cy0) { cx0 = 0; cy0 = 0; cx1 = srcW; cy1 = srcH; }
    cx0 = std::max(0, std::min(cx0, srcW - 1));
    cy0 = std::max(0, std::min(cy0, srcH - 1));
    cx1 = std::max(cx0 + 1, std::min(cx1, srcW));
    cy1 = std::max(cy0 + 1, std::min(cy1, srcH));
    const int cropW = cx1 - cx0, cropH = cy1 - cy0;

    ox0 = 0; oy0 = 0; ow = W; oh = H;
    if (!opt.stretch) {
        const double s = std::min(static_cast<double>(W) / cropW,
                                  static_cast<double>(H) / cropH);
        ow = std::max(1, static_cast<int>(std::lround(cropW * s)));
        oh = std::max(1, static_cast<int>(std::lround(cropH * s)));
        ox0 = (W - ow) / 2;
        oy0 = (H - oh) / 2;
    }

    auto sample = [&](float u, float v, float& r, float& g, float& b) {
        u = clampf(u, static_cast<float>(cx0), cx1 - 1.0f);
        v = clampf(v, static_cast<float>(cy0), cy1 - 1.0f);
        const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
        const int x1 = std::min(x0 + 1, cx1 - 1), y1 = std::min(y0 + 1, cy1 - 1);
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

    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox) {
            const float u = (ox + 0.5f) / ow * cropW - 0.5f + cx0;
            const float v = (oy + 0.5f) / oh * cropH - 0.5f + cy0;
            float r, g, b;
            sample(u, v, r, g, b);
            if (opt.contrast != 1.0f || opt.brightness != 1.0f || opt.gamma != 1.0f) {
                auto adj = [&](float c) {
                    c = (c - 0.5f) * opt.contrast + 0.5f;
                    c = clampf(c * opt.brightness, 0.0f, 1.0f);
                    if (opt.gamma != 1.0f) c = std::pow(c, 1.0f / opt.gamma);
                    return c;
                };
                r = adj(r); g = adj(g); b = adj(b);
            }
            tcam[static_cast<size_t>(oy0 + oy) * W + (ox0 + ox)] = srgbToCam16Ucs(r, g, b);
        }
}

// ── Graphics II: 2-colours-per-8×1-cell, Floyd-Steinberg ─────────────────────
void convertGfxII(const std::vector<Cam16Ucs>& tcam, int ox0, int ow,
                  const ImportOptions& opt, const Cam16Ucs cam[16], uint8_t* outVram)
{
    constexpr int W = kGfx2Width, H = kGfx2Height;
    const float cw = opt.chromaWeight;
    std::vector<Cam16Ucs> errCur(W, {0,0,0}), errNext(W, {0,0,0});
    const int colActiveLo = ox0, colActiveHi = ox0 + ow;
    auto add = [&](std::vector<Cam16Ucs>& buf, int xx, const Cam16Ucs& d) {
        if (xx >= colActiveLo && xx < colActiveHi) { buf[xx].J += d.J; buf[xx].a += d.a; buf[xx].b += d.b; }
    };

    for (int y = 0; y < H; ++y) {
        std::fill(errNext.begin(), errNext.end(), Cam16Ucs{0,0,0});
        const bool ltr = !opt.serpentine || ((y & 1) == 0);
        const int dir = ltr ? 1 : -1;
        for (int n = 0; n < 32; ++n) {
            const int cellCol = ltr ? n : (31 - n);
            const int px0 = cellCol * 8;
            if (px0 + 8 <= colActiveLo || px0 >= colActiveHi) continue;   // letterbox

            Cam16Ucs want[8];
            for (int k = 0; k < 8; ++k)
                want[k] = tcam[static_cast<size_t>(y) * W + px0 + k] + errCur[px0 + k];

            // Pick the (fg,bg) pair minimising the summed nearest-of-two cost.
            float bestCost = std::numeric_limits<float>::max();
            int bestFg = 1, bestBg = 1;
            for (int fi = 0; fi < 15; ++fi) {
                const int fg = kInk[fi];
                for (int bi = fi; bi < 15; ++bi) {
                    const int bg = kInk[bi];
                    float cost = 0.0f;
                    for (int k = 0; k < 8; ++k) {
                        const float cf = perceptualCost(cam[fg], want[k], cw);
                        const float cb = perceptualCost(cam[bg], want[k], cw);
                        cost += std::min(cf, cb);
                    }
                    if (cost < bestCost) { bestCost = cost; bestFg = fg; bestBg = bg; }
                }
            }

            uint8_t pat = 0;
            for (int k = 0; k < 8; ++k) {
                const float cf = perceptualCost(cam[bestFg], want[k], cw);
                const float cb = perceptualCost(cam[bestBg], want[k], cw);
                const bool useFg = (cf <= cb);
                const int chosen = useFg ? bestFg : bestBg;
                if (useFg) pat |= static_cast<uint8_t>(0x80u >> k);
                if (opt.dither) {
                    const Cam16Ucs res = scale(want[k] - cam[chosen], opt.diffusion);
                    const int x = px0 + k;
                    add(errNext, x - dir, scale(res, 3.0f / 16.0f));
                    add(errNext, x,       scale(res, 5.0f / 16.0f));
                    add(errNext, x + dir, scale(res, 1.0f / 16.0f));
                    add(errCur,  x + dir, scale(res, 7.0f / 16.0f));   // ahead, same row
                }
            }
            const int patAddr = tmspaint::gfx2PatternAddr(px0, y);
            const int colAddr = tmspaint::gfx2ColorAddr(px0, y);
            outVram[patAddr] = pat;
            outVram[colAddr] = static_cast<uint8_t>((bestFg << 4) | bestBg);
        }
        std::swap(errCur, errNext);
    }
}

// ── Multicolor: nearest colour per 4×4 block, Floyd-Steinberg ────────────────
void convertMulticolor(const std::vector<Cam16Ucs>& tcam, int ox0, int oy0, int ow, int oh,
                       const ImportOptions& opt, const Cam16Ucs cam[16], uint8_t* outVram)
{
    constexpr int W = kMcWidth, H = kMcHeight;
    const float cw = opt.chromaWeight;
    std::vector<Cam16Ucs> errCur(W, {0,0,0}), errNext(W, {0,0,0});
    const int colLo = ox0, colHi = ox0 + ow;
    auto add = [&](std::vector<Cam16Ucs>& buf, int xx, const Cam16Ucs& d) {
        if (xx >= colLo && xx < colHi) { buf[xx].J += d.J; buf[xx].a += d.a; buf[xx].b += d.b; }
    };

    for (int by = 0; by < H; ++by) {
        std::fill(errNext.begin(), errNext.end(), Cam16Ucs{0,0,0});
        if (by < oy0 || by >= oy0 + oh) { std::swap(errCur, errNext); continue; }
        const bool ltr = !opt.serpentine || ((by & 1) == 0);
        const int dir = ltr ? 1 : -1;
        for (int n = 0; n < W; ++n) {
            const int bx = ltr ? n : (W - 1 - n);
            if (bx < colLo || bx >= colHi) continue;   // letterbox
            const Cam16Ucs want = tcam[static_cast<size_t>(by) * W + bx] + errCur[bx];
            int best = 1; float bestCost = std::numeric_limits<float>::max();
            for (int i = 0; i < 15; ++i) {
                const float c = perceptualCost(cam[kInk[i]], want, cw);
                if (c < bestCost) { bestCost = c; best = kInk[i]; }
            }
            if (opt.dither) {
                const Cam16Ucs res = scale(want - cam[best], opt.diffusion);
                add(errCur, bx + dir, scale(res, 7.0f / 16.0f));
                add(errNext, bx - dir, scale(res, 3.0f / 16.0f));
                add(errNext, bx,       scale(res, 5.0f / 16.0f));
                add(errNext, bx + dir, scale(res, 1.0f / 16.0f));
            }
            const int addr = tmspaint::mcBlockAddr(bx, by);
            if (tmspaint::mcHighNibble(bx))
                outVram[addr] = static_cast<uint8_t>((best << 4) | (outVram[addr] & 0x0F));
            else
                outVram[addr] = static_cast<uint8_t>((outVram[addr] & 0xF0) | best);
        }
        std::swap(errCur, errNext);
    }
}

} // namespace

void imageToTmsVram(const uint8_t* rgba, int srcW, int srcH,
                    Mode mode, const ImportOptions& opt, uint8_t* outVram)
{
    std::fill(outVram, outVram + kVramSize, uint8_t{0});
    tmspaint::writeCanonicalNameTable(outVram, mode);
    if (!rgba || srcW <= 0 || srcH <= 0) return;

    Cam16Ucs cam[16];
    for (int i = 0; i < 16; ++i)
        cam[i] = srgb8ToCam16Ucs(kTmsRgb[i].r, kTmsRgb[i].g, kTmsRgb[i].b);

    std::vector<Cam16Ucs> tcam;
    int ox0, oy0, ow, oh;
    if (mode == Mode::Multicolor) {
        resampleToCam(rgba, srcW, srcH, kMcWidth, kMcHeight, opt, tcam, ox0, oy0, ow, oh);
        convertMulticolor(tcam, ox0, oy0, ow, oh, opt, cam, outVram);
    } else {
        resampleToCam(rgba, srcW, srcH, kGfx2Width, kGfx2Height, opt, tcam, ox0, oy0, ow, oh);
        convertGfxII(tcam, ox0, ow, opt, cam, outVram);
    }
}

} // namespace tmspaint
