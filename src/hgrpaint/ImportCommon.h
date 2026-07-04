// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Shared image-import plumbing for the HGR (hgrpaint/) and TMS9918 (tmspaint/)
// ii-pix-style converters — header-only so both portable toolkits can use it
// without new build rules:
//
//   • LinRgb + sRGB↔linear helpers. The converters work on LINEAR-light RGB:
//     the resample average and the error diffusion both happen in linear space
//     (ii-pix rationale: averaging/diffusing gamma-encoded values darkens thin
//     bright detail and lets out-of-gamut error accumulate into "worm" trails;
//     linear light is what the CRT emits and what the eye integrates).
//
//   • resampleToLinearRgb — the bilinear fit/stretch/crop resampler that used
//     to be duplicated (gamma-space) in HgrConvert and TmsConvert, factored
//     once and fixed to sample in linear light. The brightness/contrast/gamma
//     knobs are still applied in gamma-encoded space (encode → adjust → decode)
//     so their user-facing response is unchanged.
//
//   • DitherKernel + KernelSpec — the selectable error-diffusion kernels:
//     Floyd-Steinberg and ii-pix's "jarvis-mod" (Jarvis-Judice-Ninke reshaped
//     to push 4 pixels forward, matching how far an NTSC sliding-window
//     transition can reach — good for HGR).

#ifndef HGRPAINT_IMPORT_COMMON_H
#define HGRPAINT_IMPORT_COMMON_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hgrpaint {

// ─── Linear-light RGB ─────────────────────────────────────────────────────────

struct LinRgb { float r, g, b; };

inline LinRgb operator+(const LinRgb& a, const LinRgb& b)
{ return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline LinRgb operator-(const LinRgb& a, const LinRgb& b)
{ return {a.r - b.r, a.g - b.g, a.b - b.b}; }
inline LinRgb scale(const LinRgb& a, float s) { return {a.r * s, a.g * s, a.b * s}; }

inline float clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline LinRgb clamp01(const LinRgb& c)
{ return {clamp01f(c.r), clamp01f(c.g), clamp01f(c.b)}; }

// sRGB EOTF and its inverse (float channel 0..1).
inline float srgbToLinearF(float c)
{ return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float linearToSrgbF(float c)
{
    c = clamp01f(c);
    return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// 8-bit sRGB → linear, via LUT (the resampler hits this 4× per output pixel).
inline float srgb8ToLinearF(uint8_t v)
{
    static const std::array<float, 256> t = [] {
        std::array<float, 256> a{};
        for (int i = 0; i < 256; ++i) a[i] = srgbToLinearF(i / 255.0f);
        return a;
    }();
    return t[v];
}

// ─── Error-diffusion kernels ──────────────────────────────────────────────────

enum class DitherKernel {
    FloydSteinberg = 0,
    JarvisMod      = 1,   // ii-pix "jarvis-mod": 4-pixel forward reach
};

// A forward kernel split into (a) same-row forward taps at scan-relative
// offsets +1..+fwdReach — these drive the in-candidate sequential walk — and
// (b) up to two rows below, indexed by scan-relative dx = index−2 (−2..+4).
struct KernelSpec {
    int   fwdReach;      // number of same-row forward taps (1 = FS, 4 = jarvis-mod)
    float fwd[4];        // weight at +1..+4
    int   belowRows;     // rows below receiving error (1 = FS, 2 = jarvis-mod)
    float below[2][7];   // [row][dx + 2]
};

inline const KernelSpec& kernelSpec(DitherKernel k)
{
    // Floyd-Steinberg: 7/16 forward; 3,5,1 /16 on the next row.
    static const KernelSpec kFs = {
        1, {7.0f / 16, 0, 0, 0},
        1, {{0, 3.0f / 16, 5.0f / 16, 1.0f / 16, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0}},
    };
    // ii-pix "jarvis-mod" (Jarvis-Judice-Ninke reshaped so all the current-row
    // error goes FORWARD over 4 pixels — the same reach as an NTSC sliding-
    // window transition, so compensation lands where the artifact colours can
    // still respond). Matrix rows, origin at column 2:
    //     0  0  0 15 11  7  3
    //     3  5  7  5  3  1  0
    //     1  3  5  3  1  0  0
    // The weights sum to 73; normalising by the sum conserves error exactly.
    static const KernelSpec kJm = {
        4, {15.0f / 73, 11.0f / 73, 7.0f / 73, 3.0f / 73},
        2, {{3.0f / 73, 5.0f / 73, 7.0f / 73, 5.0f / 73, 3.0f / 73, 1.0f / 73, 0},
            {1.0f / 73, 3.0f / 73, 5.0f / 73, 3.0f / 73, 1.0f / 73, 0, 0}},
    };
    return (k == DitherKernel::JarvisMod) ? kJm : kFs;
}

// ─── Shared resampler (linear-light) ──────────────────────────────────────────
//
// Resample the (cropped) source into a W×H LINEAR-RGB target with optional
// fit+letterbox and the brightness/contrast/gamma knobs. Reports the active
// (non-letterbox) rect [ox0,ox0+ow) × [oy0,oy0+oh). Letterbox pixels stay
// linear black {0,0,0}.
//
// `Opts` is either hgrpaint::ImportOptions or tmspaint::ImportOptions (both
// carry the same stretch/brightness/contrast/gamma/crop fields).
template <typename Opts>
inline void resampleToLinearRgb(const uint8_t* rgba, int srcW, int srcH, int W, int H,
                                const Opts& opt, std::vector<LinRgb>& out,
                                int& ox0, int& oy0, int& ow, int& oh)
{
    out.assign(static_cast<size_t>(W) * H, LinRgb{0, 0, 0});

    // Optional source crop: only [cx0,cx1) × [cy0,cy1) is resampled. A degenerate
    // rect means "whole image"; the crop's aspect ratio drives fit/letterbox.
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

    // Bilinear fetch clamped to the CROP window (not the full source), so an
    // interior crop's edge output columns replicate the crop's own border pixels
    // instead of bleeding ~1px of neighbouring content from outside the rect.
    // The lerp runs on LINEARISED texels: averaging gamma-encoded values (the
    // old behaviour) systematically darkens the mix, so thin bright details
    // (stars, highlights, text) lost energy during the downscale.
    auto sample = [&](float u, float v, float& r, float& g, float& b) {
        u = std::max(static_cast<float>(cx0), std::min(u, cx1 - 1.0f));
        v = std::max(static_cast<float>(cy0), std::min(v, cy1 - 1.0f));
        const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
        const int x1 = std::min(x0 + 1, cx1 - 1), y1 = std::min(y0 + 1, cy1 - 1);
        const float fx = u - x0, fy = v - y0;
        auto px = [&](int x, int y, int c) {
            return srgb8ToLinearF(rgba[(static_cast<size_t>(y) * srcW + x) * 4 + c]);
        };
        auto lerp2 = [&](int c) {
            const float a = px(x0, y0, c) * (1 - fx) + px(x1, y0, c) * fx;
            const float bb = px(x0, y1, c) * (1 - fx) + px(x1, y1, c) * fx;
            return a * (1 - fy) + bb * fy;
        };
        r = lerp2(0); g = lerp2(1); b = lerp2(2);
    };

    const bool adjust = (opt.contrast != 1.0f || opt.brightness != 1.0f ||
                         opt.gamma != 1.0f);
    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox) {
            const float u = (ox + 0.5f) / ow * cropW - 0.5f + cx0;
            const float v = (oy + 0.5f) / oh * cropH - 0.5f + cy0;
            float r, g, b;
            sample(u, v, r, g, b);
            if (adjust) {
                // The knobs keep their historical gamma-space response (they were
                // tuned against gamma-encoded values), so encode → adjust → decode.
                auto adj = [&](float lin) {
                    float c = linearToSrgbF(lin);
                    c = (c - 0.5f) * opt.contrast + 0.5f;        // contrast about mid-grey
                    c = clamp01f(c * opt.brightness);            // brightness
                    if (opt.gamma != 1.0f) c = std::pow(c, 1.0f / opt.gamma);   // gamma
                    return srgbToLinearF(c);
                };
                r = adj(r); g = adj(g); b = adj(b);
            }
            out[static_cast<size_t>(oy0 + oy) * W + (ox0 + ox)] = {r, g, b};
        }
}

} // namespace hgrpaint

#endif // HGRPAINT_IMPORT_COMMON_H
