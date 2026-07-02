// HGR image-conversion smoke test — pins the portable hgrpaint/ importer
// (CAM16-UCS perceptual space + the NTSC analysis-by-synthesis converter) with
// no GL/ImGui dependency.
//
// Covers: CAM16-UCS sanity + perceptual ranking; the module's NTSC decode is
// byte-identical to GraphicsCard (the real renderer); black→empty page;
// white→white; dither tone behaviour on flats and a black→white ramp; and the
// ii-pix upgrade pins — ICM refinement monotonicity, clamped linear-RGB error
// diffusion, and the early-abort/warm-start search being bit-identical to the
// exhaustive candidate scan.

#include "Cam16.h"
#include "HgrConvert.h"
#include "HgrPaintModel.h"
#include "GraphicsCard.h"

// This whole test is assert-based; keep the assertions live even in Release
// builds (the default CMAKE_BUILD_TYPE compiles tests with -DNDEBUG, which
// would silently turn the entire pin into a no-op).
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace hgrpaint;

static float camDist(Cam16Ucs a, Cam16Ucs b) { return std::sqrt(cam16DistSq(a, b)); }

static void renderPage(const uint8_t* page, std::vector<uint32_t>& out)
{
    out.assign(static_cast<size_t>(kHiresWidth) * kHiresHeight, 0);
    for (int y = 0; y < kHiresHeight; ++y) {
        uint8_t bytes[40];
        const int base = hgrByteOffset(0, y);
        for (int b = 0; b < 40; ++b) bytes[b] = page[base + b];
        hgrDecodeScanlineRgb(bytes, &out[static_cast<size_t>(y) * kHiresWidth]);
    }
}

static std::vector<uint8_t> solid(int r, int g, int b)
{
    std::vector<uint8_t> v(static_cast<size_t>(kHiresWidth) * kHiresHeight * 4);
    for (size_t i = 0; i < v.size(); i += 4) { v[i] = r; v[i + 1] = g; v[i + 2] = b; v[i + 3] = 255; }
    return v;
}

int main()
{
    // ── CAM16-UCS sanity ─────────────────────────────────────────────────────
    const auto blk = srgb8ToCam16Ucs(0, 0, 0);
    const auto wht = srgb8ToCam16Ucs(255, 255, 255);
    const auto gry = srgb8ToCam16Ucs(128, 128, 128);
    assert(wht.J > gry.J && gry.J > blk.J);                          // lightness ordering
    assert(std::fabs(blk.a) < 3 && std::fabs(wht.a) < 3 && std::fabs(gry.a) < 3);   // neutral
    assert(std::fabs(blk.b) < 3 && std::fabs(wht.b) < 3 && std::fabs(gry.b) < 3);
    const auto red = srgb8ToCam16Ucs(255, 0, 0);
    const auto org = srgb8ToCam16Ucs(255, 128, 0);
    const auto blu = srgb8ToCam16Ucs(0, 0, 255);
    const auto vio = srgb8ToCam16Ucs(221, 34, 221);
    assert(camDist(red, org) < camDist(red, blu));                  // red nearer orange than blue
    assert(camDist(blu, vio) < camDist(blu, org));                  // blue nearer violet than orange

    // ── hgrpaint NTSC decode == GraphicsCard (the real renderer) ─────────────
    {
        std::vector<uint8_t> mem(0x10000, 0);
        uint32_t s = 12345;
        for (int i = 0; i < 0x2000; ++i) { s = s * 1664525u + 1013904223u; mem[0x2000 + i] = uint8_t(s >> 16); }
        GraphicsCard gfx;
        gfx.invalidate();
        gfx.rasterizeToBuffer(mem.data());
        const uint32_t* ref = gfx.pixels();
        for (int y = 0; y < kHiresHeight; ++y) {
            uint8_t bytes[40];
            const uint16_t base = GraphicsCard::hgrRowAddress(y, false);
            for (int b = 0; b < 40; ++b) bytes[b] = mem[base + b];
            uint32_t line[kHiresWidth];
            hgrDecodeScanlineRgb(bytes, line);
            for (int x = 0; x < kHiresWidth; ++x)
                assert((line[x] & 0xFFFFFFu) == (ref[y * kHiresWidth + x] & 0xFFFFFFu));
        }
    }

    ImportOptions opt;
    opt.stretch = true;   // sources are already 280×192 here
    std::vector<uint8_t> page(kHiresSize);
    std::vector<uint32_t> ren;

    // ── black → empty page; white → white ───────────────────────────────────
    auto img = solid(0, 0, 0);
    imageToHgrPage(img.data(), kHiresWidth, kHiresHeight, opt, page.data());
    for (uint8_t b : page) assert(b == 0);

    img = solid(255, 255, 255);
    imageToHgrPage(img.data(), kHiresWidth, kHiresHeight, opt, page.data());
    renderPage(page.data(), ren);
    assert((ren[96 * kHiresWidth + 140] & 0xFFFFFFu) == 0xFFFFFFu);

    // Pure green is in-gamut → should reproduce closely at the centre.
    img = solid(0, 200, 0);
    imageToHgrPage(img.data(), kHiresWidth, kHiresHeight, opt, page.data());
    renderPage(page.data(), ren);
    {
        const uint32_t p = ren[96 * kHiresWidth + 140];
        const auto t = srgb8ToCam16Ucs(0, 200, 0);
        const auto r = srgb8ToCam16Ucs(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
        assert(camDist(t, r) < 12.0f);
    }

    // ── Floyd-Steinberg tone conservation: a black→white ramp must reproduce as
    //    monotonically increasing brightness ──────────────────────────────────
    {
        std::vector<uint8_t> ramp(static_cast<size_t>(kHiresWidth) * kHiresHeight * 4);
        for (int y = 0; y < kHiresHeight; ++y)
            for (int x = 0; x < kHiresWidth; ++x) {
                const size_t i = (static_cast<size_t>(y) * kHiresWidth + x) * 4;
                const uint8_t g = static_cast<uint8_t>(x * 255 / (kHiresWidth - 1));
                ramp[i] = ramp[i + 1] = ramp[i + 2] = g; ramp[i + 3] = 255;
            }
        opt.dither = true;
        imageToHgrPage(ramp.data(), kHiresWidth, kHiresHeight, opt, page.data());
        renderPage(page.data(), ren);
        int prev = -1;
        for (int band = 0; band < 14; ++band) {
            long sum = 0; int cnt = 0;
            for (int x = band * 20; x < band * 20 + 20; ++x)
                for (int y = 0; y < kHiresHeight; ++y) {
                    const uint32_t p = ren[y * kHiresWidth + x];
                    sum += ((p & 0xFF) * 30 + ((p >> 8) & 0xFF) * 59 + ((p >> 16) & 0xFF) * 11) / 100;
                    ++cnt;
                }
            const int m = static_cast<int>(sum / cnt);
            assert(m + 6 >= prev);          // monotonic (small dithering wobble tolerated)
            prev = m;
        }
        assert(prev > 200);                 // right end is bright
    }

    // ── Flat-field tone reproduction (error-diffusion conservation) ──────────
    // A flat gray whose density is not exactly achievable per 7-pixel byte must
    // still reproduce, page-averaged, at a stable brightness: the diffusion
    // carries the per-byte residual to the neighbours instead of dropping it.
    //
    // Since the gamma-correct import rework the target buffer and the diffusion
    // run in LINEAR light (sRGB decoded before the resample/dither — ii-pix
    // semantics), so the equilibrium density is set by linear-light residual
    // feedback through the CAM16 quantizer (with wants clamped to gamut), NOT by
    // conservation of the gamma-coded values the old expectation (mean ≈ g ± 14)
    // encoded. The old gamma-space diffusion rendered dark flats photometrically
    // ~5× too bright; the linear pipeline halves that (the remaining lift comes
    // from CAM16's perceptual black↔grey threshold plus the want clamp at 0).
    // The exact means are implementation-defined but fully deterministic — pin
    // them as a behavioural table; a conservation regression (e.g. the historic
    // bug that dropped ~6/7 of the horizontal error) shifts them far outside
    // the ±10 window.
    {
        auto meanLuma = [&](const std::vector<uint32_t>& px) {
            long sum = 0;
            for (uint32_t p : px)
                sum += ((p & 0xFF) * 30 + ((p >> 8) & 0xFF) * 59 + ((p >> 16) & 0xFF) * 11) / 100;
            return static_cast<double>(sum) / px.size();
        };
        opt.dither = true;
        opt.chromaWeight = 6.0f;   // neutral target → clean neutral dither
        const int    gs[3]       = {64, 96, 160};
        const double expected[3] = {48.7, 73.1, 110.3};   // measured 2026-07 pins
        for (int i = 0; i < 3; ++i) {
            auto flat = solid(gs[i], gs[i], gs[i]);
            imageToHgrPage(flat.data(), kHiresWidth, kHiresHeight, opt, page.data());
            renderPage(page.data(), ren);
            const double m = meanLuma(ren);
            assert(std::fabs(m - expected[i]) < 10.0);
        }
    }

    // ── Letterbox bars stay pure black (no Floyd-Steinberg bleed) ─────────────
    // A tall/narrow source in fit mode (stretch=false) centres a 100-wide image in
    // 280, so columns [0,90) and [190,280) are letterbox. Diffusion must not leak
    // into them. Active span [90,190) → byte cols 0..11 and 28..39 are fully out.
    {
        const int sW = 100, sH = 192;
        std::vector<uint8_t> img(static_cast<size_t>(sW) * sH * 4);
        for (size_t i = 0; i < img.size(); i += 4) { img[i] = img[i+1] = img[i+2] = 200; img[i+3] = 255; }
        ImportOptions lo;
        lo.stretch = false; lo.dither = true; lo.chromaWeight = 6.0f;
        imageToHgrPage(img.data(), sW, sH, lo, page.data());
        for (int y = 0; y < kHiresHeight; ++y) {
            const int base = hgrByteOffset(0, y);
            for (int b = 0; b <= 11; ++b) assert(page[base + b] == 0);
            for (int b = 28; b < 40; ++b) assert(page[base + b] == 0);
        }
    }

    // ── ii-pix upgrade pins: refinement monotonicity, clamped linear error, and
    //    early-abort/warm-start search exactness ───────────────────────────────
    {
        // Synthetic gradient with both luminance and chroma structure.
        std::vector<uint8_t> grad(static_cast<size_t>(kHiresWidth) * kHiresHeight * 4);
        for (int y = 0; y < kHiresHeight; ++y)
            for (int x = 0; x < kHiresWidth; ++x) {
                const size_t i = (static_cast<size_t>(y) * kHiresWidth + x) * 4;
                grad[i]     = static_cast<uint8_t>(x * 255 / (kHiresWidth - 1));
                grad[i + 1] = static_cast<uint8_t>(y * 255 / (kHiresHeight - 1));
                grad[i + 2] = static_cast<uint8_t>(255 - x * 255 / (kHiresWidth - 1));
                grad[i + 3] = 255;
            }
        for (int kern = 0; kern < 2; ++kern) {
            ImportOptions o;
            o.stretch = true;
            o.kernel = kern ? DitherKernel::JarvisMod : DitherKernel::FloydSteinberg;
            o.diffusion = kern ? 0.7f : 1.0f;   // exercise both diffusion doses
            o.refinePasses = 2;
            ImportStats st;
            std::vector<uint8_t> fast(kHiresSize), exhaustive(kHiresSize);
            imageToHgrPage(grad.data(), kHiresWidth, kHiresHeight, o, fast.data(), &st);

            // (a) Each ICM refinement pass accepts only moves that strictly lower
            //     the frozen-target objective, so the recorded pass costs must be
            //     non-increasing (tiny epsilon for the float→double re-summation).
            assert(st.passCost.size() >= 2);
            for (size_t p = 1; p < st.passCost.size(); ++p)
                assert(st.passCost[p] <= st.passCost[p - 1] * 1.000001f + 1.0f);

            // (b) Linear-RGB diffusion stays clamped: every residual is computed
            //     from a [0,1]-clamped want minus an in-gamut rendered colour, and
            //     no error slot collects more than the kernel-weight sum (=1), so
            //     diffusion 1.0 can't build runaway "worm" error.
            assert(st.maxErrAbs <= 1.0f + 1e-4f);

            // (c) The early-abort + warm-start candidate search (and the pruned
            //     refinement DFS) must be BIT-IDENTICAL to the exhaustive scan:
            //     the abort comparison is strict (ties always finish) and
            //     acceptance is lexicographic on (cost, candidate index), so the
            //     warm seed can never change the argmin or its tie-breaking.
            ImportOptions oex = o;
            oex.exhaustiveSearch = true;
            imageToHgrPage(grad.data(), kHiresWidth, kHiresHeight, oex, exhaustive.data());
            assert(fast == exhaustive);
        }
    }

    std::printf("hgr_convert_smoke: all assertions passed\n");
    return 0;
}
