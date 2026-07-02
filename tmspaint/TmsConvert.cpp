// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Image → TMS9918 conversion — see TmsConvert.h. Scores candidate palette colours
// in CAM16-UCS (shared hgrpaint::Cam16) and error-diffuses in LINEAR RGB; no NTSC
// synthesis (the TMS palette is 15 fixed RGB colours). ii-pix-style upgrades:
//   • resample + diffusion in linear light (gamma-space averaging darkened thin
//     bright detail; gamma-space diffusion let out-of-gamut error snowball),
//   • clamped effective targets: want = clamp(target + err) — diffusion 1.0 safe,
//   • in-candidate sequential walk (apply_one_line): pixel k's residual feeds
//     pixel k+1.. of the SAME candidate pair before they are scored, and the
//     bit-assignment reuses the identical walk so scoring and commit agree,
//   • early abort + warm start (previous row's pair) in the 120-pair search,
//     pinned bit-identical to the exhaustive scan in tms_convert_smoke,
//   • selectable kernel (Floyd-Steinberg / jarvis-mod).

#include "TmsConvert.h"

#include "Cam16.h"           // hgrpaint::Cam16 (shared, pure)
#include "ImportCommon.h"    // shared linear-RGB resampler + kernels
#include "TmsPaintModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace tmspaint {
namespace {

using hgrpaint::Cam16Ucs;
using hgrpaint::DitherKernel;
using hgrpaint::KernelSpec;
using hgrpaint::LinRgb;
using hgrpaint::clamp01;
using hgrpaint::kernelSpec;
using hgrpaint::linearSrgbToCam16Ucs;
using hgrpaint::resampleToLinearRgb;
using hgrpaint::srgb8ToCam16Ucs;
using hgrpaint::srgb8ToLinearF;

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

// Perceptual cost with weighted chroma axes (see HgrConvert: the "colour noise"
// knob makes inventing chroma expensive so neutral targets dither clean B/W).
inline float perceptualCost(const Cam16Ucs& got, const Cam16Ucs& want, float cw)
{
    const float dJ = got.J - want.J, da = got.a - want.a, db = got.b - want.b;
    return dJ * dJ + cw * (da * da + db * db);
}

// ── Graphics II: 2-colours-per-8×1-cell ──────────────────────────────────────
void convertGfxII(const std::vector<LinRgb>& tlin, int ox0, int ow,
                  const ImportOptions& opt, const Cam16Ucs cam[16],
                  const LinRgb lin[16], uint8_t* outVram)
{
    constexpr int W = kGfx2Width, H = kGfx2Height;
    const KernelSpec& ker = kernelSpec(opt.kernel);
    const float cw = opt.chromaWeight;
    const float walkScale = opt.dither ? opt.diffusion : 0.0f;
    const float kInf = std::numeric_limits<float>::max();

    // Linear-RGB error rows (jarvis-mod reaches two rows down).
    std::vector<LinRgb> errRow[3];
    for (auto& e : errRow) e.assign(W, LinRgb{0, 0, 0});
    const int colActiveLo = ox0, colActiveHi = ox0 + ow;
    auto addErr = [&](std::vector<LinRgb>& buf, int xx, const LinRgb& d) {
        if (xx >= colActiveLo && xx < colActiveHi) buf[xx] = buf[xx] + d;
    };
    auto rotateErrRows = [&] {
        std::swap(errRow[0], errRow[1]);
        std::swap(errRow[1], errRow[2]);
        std::fill(errRow[2].begin(), errRow[2].end(), LinRgb{0, 0, 0});
    };

    // Warm-start seeds: the pair chosen for this cell on the previous row.
    uint8_t prevFg[32], prevBg[32];
    std::fill(prevFg, prevFg + 32, uint8_t{1});
    std::fill(prevBg, prevBg + 32, uint8_t{1});

    for (int y = 0; y < H; ++y) {
        const bool ltr = !opt.serpentine || ((y & 1) == 0);
        const int dir = ltr ? 1 : -1;
        for (int n = 0; n < 32; ++n) {
            const int cellCol = ltr ? n : (31 - n);
            const int px0 = cellCol * 8;
            if (px0 + 8 <= colActiveLo || px0 >= colActiveHi) continue;   // letterbox

            // Effective per-pixel targets: clamp(target + err) in linear RGB,
            // converted to CAM16 once per cell (NOT per candidate pair), plus
            // the local CAM16 Jacobian used to score walked offsets.
            LinRgb   wantLin[8];
            Cam16Ucs wantCam[8];
            const hgrpaint::Cam16Jac* jac[8];
            for (int k = 0; k < 8; ++k) {
                wantLin[k] = clamp01(tlin[static_cast<size_t>(y) * W + px0 + k]
                                     + errRow[0][px0 + k]);
                wantCam[k] = linearSrgbToCam16Ucs(wantLin[k].r, wantLin[k].g,
                                                  wantLin[k].b);
                jac[k] = &hgrpaint::cam16JacobianAt(wantLin[k].r, wantLin[k].g,
                                                    wantLin[k].b);
            }

            // Pick the (fg,bg) pair minimising the walked nearest-of-two cost.
            // ii-pix apply_one_line semantics: pixel k's residual is diffused
            // into the following pixels of the SAME pair before they are scored
            // (scan order), so a pair is credited for compensating its own early
            // error. The walk stays in LINEAR RGB — the same space as the
            // committed diffusion (walking in CAM16 would over-credit dark-pixel
            // compensation and break tone conservation); the walked offset is
            // mapped into CAM16 for scoring via the local Jacobian:
            // cam(want + d) ≈ cam(want) + J·d. The cost accumulates pixel by
            // pixel and bails out as soon as it strictly exceeds the best (ties
            // must finish so tie-breaking stays identical to the exhaustive scan
            // — pinned in the smoke test).
            float bestCost = kInf;
            int bestFg = 1, bestBg = 1, bestPair = -1;
            uint8_t bestPat = 0;
            auto consider = [&](int fi, int bi) {
                const int pairIdx = fi * 15 + bi;
                if (pairIdx == bestPair) return;   // warm seed: don't score twice
                const int fg = kInk[fi], bg = kInk[bi];
                const float bound = opt.exhaustiveSearch ? kInf : bestCost;
                LinRgb fw[4] = {};   // rolling forward residuals (scan order)
                float cost = 0.0f;
                uint8_t pat = 0;
                for (int j = 0; j < 8; ++j) {
                    const int k = ltr ? j : 7 - j;
                    // Zero-offset fast path (always at the first pixel, and
                    // everywhere when dither is off) — bit-exact shortcut.
                    const bool walked = fw[0].r != 0.0f || fw[0].g != 0.0f
                                        || fw[0].b != 0.0f;
                    LinRgb wlLin;
                    Cam16Ucs wl;
                    if (walked) {
                        wlLin = clamp01(wantLin[k] + fw[0]);
                        const LinRgb d = wlLin - wantLin[k];
                        const float* M = jac[k]->m;
                        wl = {wantCam[k].J + M[0] * d.r + M[1] * d.g + M[2] * d.b,
                              wantCam[k].a + M[3] * d.r + M[4] * d.g + M[5] * d.b,
                              wantCam[k].b + M[6] * d.r + M[7] * d.g + M[8] * d.b};
                    } else {
                        wlLin = wantLin[k];   // already clamped at cell start
                        wl = wantCam[k];
                    }
                    const float cf = perceptualCost(cam[fg], wl, cw);
                    const float cb = perceptualCost(cam[bg], wl, cw);
                    const bool useFg = (cf <= cb);
                    cost += useFg ? cf : cb;
                    if (cost > bound) return;      // early abort: can't win or tie
                    if (useFg) pat |= static_cast<uint8_t>(0x80u >> k);
                    if (walkScale != 0.0f && j < 7) {
                        const LinRgb res = scale(wlLin - lin[useFg ? fg : bg], walkScale);
                        fw[0] = fw[1] + scale(res, ker.fwd[0]);
                        if (ker.fwdReach > 1) {    // FS keeps fw[1..3] at zero
                            fw[1] = fw[2] + scale(res, ker.fwd[1]);
                            fw[2] = fw[3] + scale(res, ker.fwd[2]);
                            fw[3] = scale(res, ker.fwd[3]);
                        }
                    }
                }
                // Accept strictly better costs; on an exact tie keep the LOWEST
                // pair index — exactly what the plain (fi,bi) scan produces, so
                // the warm start can never change tie-breaking.
                if (cost < bestCost ||
                    (cost == bestCost && (bestPair < 0 || pairIdx < bestPair))) {
                    bestCost = cost; bestFg = fg; bestBg = bg;
                    bestPair = pairIdx; bestPat = pat;
                }
            };
            // Warm start: images are vertically coherent, so the previous row's
            // pair for this cell seeds a tight abort bound (kInk[i] == i+1).
            if (!opt.exhaustiveSearch)
                consider(prevFg[cellCol] - 1, prevBg[cellCol] - 1);
            for (int fi = 0; fi < 15; ++fi)
                for (int bi = fi; bi < 15; ++bi)
                    consider(fi, bi);

            // COMMIT: the bit pattern was fixed during scoring (bestPat), so the
            // bit choice and the scoring walk agree by construction. Redo the
            // walk in LINEAR RGB to emit the residuals: in-cell forward taps feed
            // the walk itself; taps past the cell land on errRow[0] (read by the
            // next cell's wants); below-row taps go to errRow[1..2].
            outVram[tmspaint::gfx2PatternAddr(px0, y)] = bestPat;
            outVram[tmspaint::gfx2ColorAddr(px0, y)] =
                static_cast<uint8_t>((bestFg << 4) | bestBg);
            prevFg[cellCol] = static_cast<uint8_t>(bestFg);
            prevBg[cellCol] = static_cast<uint8_t>(bestBg);

            if (opt.dither) {
                LinRgb wl[8];   // in scan order
                for (int j = 0; j < 8; ++j) wl[j] = wantLin[ltr ? j : 7 - j];
                for (int j = 0; j < 8; ++j) {
                    const int k = ltr ? j : 7 - j;
                    const int x = px0 + k;
                    wl[j] = clamp01(wl[j]);   // walked wants stay in gamut too
                    const int chosen = (bestPat & (0x80u >> k)) ? bestFg : bestBg;
                    const LinRgb res = scale(wl[j] - lin[chosen], opt.diffusion);
                    for (int d = 1; d <= ker.fwdReach; ++d) {
                        const LinRgb t = scale(res, ker.fwd[d - 1]);
                        if (j + d < 8) wl[j + d] = wl[j + d] + t;
                        else           addErr(errRow[0], x + dir * d, t);
                    }
                    for (int r = 1; r <= ker.belowRows; ++r)
                        for (int dx = -2; dx <= 4; ++dx) {
                            const float wgt = ker.below[r - 1][dx + 2];
                            if (wgt != 0.0f)
                                addErr(errRow[r], x + dir * dx, scale(res, wgt));
                        }
                }
            }
        }
        rotateErrRows();
    }
}

// ── Multicolor: nearest colour per 4×4 block ─────────────────────────────────
void convertMulticolor(const std::vector<LinRgb>& tlin, int ox0, int oy0, int ow, int oh,
                       const ImportOptions& opt, const Cam16Ucs cam[16],
                       const LinRgb lin[16], uint8_t* outVram)
{
    constexpr int W = kMcWidth, H = kMcHeight;
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
        const bool ltr = !opt.serpentine || ((by & 1) == 0);
        const int dir = ltr ? 1 : -1;
        for (int n = 0; n < W; ++n) {
            const int bx = ltr ? n : (W - 1 - n);
            if (bx < colLo || bx >= colHi) continue;   // letterbox
            // Clamped effective target in linear RGB, scored in CAM16.
            const LinRgb want = clamp01(tlin[static_cast<size_t>(by) * W + bx]
                                        + errRow[0][bx]);
            const Cam16Ucs wantCam = linearSrgbToCam16Ucs(want.r, want.g, want.b);
            int best = 1;
            float bestCost = std::numeric_limits<float>::max();
            for (int i = 0; i < 15; ++i) {   // 15 candidates: no abort needed
                const float c = perceptualCost(cam[kInk[i]], wantCam, cw);
                if (c < bestCost) { bestCost = c; best = kInk[i]; }
            }
            if (opt.dither) {
                const LinRgb res = scale(want - lin[best], opt.diffusion);
                for (int d = 1; d <= ker.fwdReach; ++d)
                    addErr(errRow[0], bx + dir * d, scale(res, ker.fwd[d - 1]));
                for (int r = 1; r <= ker.belowRows; ++r)
                    for (int dx = -2; dx <= 4; ++dx) {
                        const float wgt = ker.below[r - 1][dx + 2];
                        if (wgt != 0.0f)
                            addErr(errRow[r], bx + dir * dx, scale(res, wgt));
                    }
            }
            const int addr = tmspaint::mcBlockAddr(bx, by);
            if (tmspaint::mcHighNibble(bx))
                outVram[addr] = static_cast<uint8_t>((best << 4) | (outVram[addr] & 0x0F));
            else
                outVram[addr] = static_cast<uint8_t>((outVram[addr] & 0xF0) | best);
        }
        rotateErrRows();
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
    LinRgb   lin[16];
    for (int i = 0; i < 16; ++i) {
        cam[i] = srgb8ToCam16Ucs(kTmsRgb[i].r, kTmsRgb[i].g, kTmsRgb[i].b);
        lin[i] = {srgb8ToLinearF(kTmsRgb[i].r), srgb8ToLinearF(kTmsRgb[i].g),
                  srgb8ToLinearF(kTmsRgb[i].b)};
    }

    // Shared linear-light resampler (ImportCommon.h): the bilinear average and
    // the diffusion below both run on linear RGB (gamma-correct).
    std::vector<LinRgb> tlin;
    int ox0, oy0, ow, oh;
    if (mode == Mode::Multicolor) {
        resampleToLinearRgb(rgba, srcW, srcH, kMcWidth, kMcHeight, opt, tlin,
                            ox0, oy0, ow, oh);
        convertMulticolor(tlin, ox0, oy0, ow, oh, opt, cam, lin, outVram);
    } else {
        resampleToLinearRgb(rgba, srcW, srcH, kGfx2Width, kGfx2Height, opt, tlin,
                            ox0, oy0, ow, oh);
        convertGfxII(tlin, ox0, ow, opt, cam, lin, outVram);
    }
}

} // namespace tmspaint
