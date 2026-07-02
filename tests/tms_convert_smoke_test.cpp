// TMS9918 image-import smoke test — pins the Cam16 metric + the TmsConvert
// image→VRAM pipeline (tmspaint::imageToTmsVram) with no GL/ImGui dependency,
// cross-checked against the real TMS9918 renderer.
//
// Covers: CAM16-UCS sanity, solid-palette-colour round-trips (a solid image of a
// palette colour must render back as that colour), the 2-colours-per-8×1-cell
// Graphics II constraint (every cell renders at most two distinct colours), the
// Multicolor nearest-colour path, and letterbox bars staying backdrop.
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "TmsConvert.h"
#include "TmsPaintModel.h"
#include "Cam16.h"
#include "TMS9918.h"

// This whole test is assert-based; keep the assertions live even in Release
// builds (the default CMAKE_BUILD_TYPE compiles tests with -DNDEBUG, which
// would silently turn the entire pin into a no-op).
#undef NDEBUG
#include <cassert>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

using tmspaint::Mode;
using hgrpaint::srgb8ToCam16Ucs;
using hgrpaint::cam16DistSq;

static void renderVram(const std::vector<uint8_t>& vram, const uint8_t regs[8],
                       std::vector<uint32_t>& out)
{
    static TMS9918::Snapshot snap;
    std::copy(vram.begin(), vram.end(), snap.vram.begin());
    std::copy(regs, regs + 8, snap.regs.begin());
    snap.statusReg = 0;
    snap.siliconStrictMode = false;
    out.assign(TMS9918::kScreenWidth * TMS9918::kScreenHeight, 0);
    TMS9918::renderToBuffer(out.data(), snap);
}
static uint32_t pixelAt(const std::vector<uint32_t>& fb, int x, int y)
{ return fb[static_cast<size_t>(y) * TMS9918::kScreenWidth + x]; }

// Solid w×h RGBA image of one colour.
static std::vector<uint8_t> solidImage(int w, int h, int r, int g, int b)
{
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.size(); i += 4) { img[i]=r; img[i+1]=g; img[i+2]=b; img[i+3]=255; }
    return img;
}

// Pull an 8-bit RGB out of a TMS9918 palette entry (IM_COL32 = 0xAABBGGRR).
static void palRgb(int idx, int& r, int& g, int& b)
{
    const uint32_t c = TMS9918::kPalette[idx];
    r = c & 0xFF; g = (c >> 8) & 0xFF; b = (c >> 16) & 0xFF;
}

int main()
{
    // ── CAM16-UCS sanity ─────────────────────────────────────────────────────
    {
        const auto white = srgb8ToCam16Ucs(255, 255, 255);
        const auto grey  = srgb8ToCam16Ucs(128, 128, 128);
        const auto black = srgb8ToCam16Ucs(0, 0, 0);
        assert(white.J > grey.J && grey.J > black.J);          // lightness order
        assert(std::fabs(grey.a) < 3.0f && std::fabs(grey.b) < 3.0f);  // neutral chroma
        const auto red    = srgb8ToCam16Ucs(220, 40, 40);
        const auto orange = srgb8ToCam16Ucs(230, 130, 20);
        const auto blue   = srgb8ToCam16Ucs(40, 60, 220);
        assert(cam16DistSq(red, orange) < cam16DistSq(red, blue));
    }

    uint8_t regs2[8];   tmspaint::canonicalRegisters(Mode::GraphicsII, regs2);
    uint8_t regsMC[8];  tmspaint::canonicalRegisters(Mode::Multicolor, regsMC);
    tmspaint::ImportOptions opt;   // defaults: fit + dither

    // ── Graphics II: solid palette colour round-trips ────────────────────────
    for (int ink : {1, 2, 6, 8, 11, 13, 14, 15}) {
        int r, g, b; palRgb(ink, r, g, b);
        const auto img = solidImage(256, 192, r, g, b);
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::imageToTmsVram(img.data(), 256, 192, Mode::GraphicsII, opt, vram.data());
        std::vector<uint32_t> fb; renderVram(vram, regs2, fb);
        // A solid image must render uniformly as that palette colour.
        assert(pixelAt(fb, 128, 96)  == TMS9918::kPalette[ink]);
        assert(pixelAt(fb, 8, 8)     == TMS9918::kPalette[ink]);
        assert(pixelAt(fb, 248, 184) == TMS9918::kPalette[ink]);
    }

    // ── 2-colours-per-8×1-cell constraint (via the real renderer) ────────────
    // A horizontal RGB gradient: every 8-pixel cell of every row must render with
    // at most two distinct colours — the Graphics II hardware limit, which the
    // converter must respect by construction.
    {
        std::vector<uint8_t> img(256 * 192 * 4);
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x) {
                const size_t i = (static_cast<size_t>(y) * 256 + x) * 4;
                img[i]   = static_cast<uint8_t>(x);             // R ramp
                img[i+1] = static_cast<uint8_t>(255 - x);       // G ramp
                img[i+2] = static_cast<uint8_t>(y);             // B ramp
                img[i+3] = 255;
            }
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::imageToTmsVram(img.data(), 256, 192, Mode::GraphicsII, opt, vram.data());
        std::vector<uint32_t> fb; renderVram(vram, regs2, fb);
        for (int y = 0; y < 192; ++y)
            for (int cell = 0; cell < 32; ++cell) {
                std::set<uint32_t> colors;
                for (int k = 0; k < 8; ++k) colors.insert(pixelAt(fb, cell * 8 + k, y));
                assert(colors.size() <= 2);
            }
    }

    // ── Multicolor: solid palette colour round-trips ─────────────────────────
    for (int ink : {2, 5, 9, 12, 15}) {
        int r, g, b; palRgb(ink, r, g, b);
        const auto img = solidImage(64, 48, r, g, b);
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::imageToTmsVram(img.data(), 64, 48, Mode::Multicolor, opt, vram.data());
        std::vector<uint32_t> fb; renderVram(vram, regsMC, fb);
        assert(pixelAt(fb, 4 * 4 + 1, 4 * 4 + 1) == TMS9918::kPalette[ink]);   // a centre block
        assert(pixelAt(fb, 30 * 4 + 1, 24 * 4 + 1) == TMS9918::kPalette[ink]);
    }

    // ── Black → backdrop, white → white (Graphics II) ────────────────────────
    {
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        const auto blk = solidImage(256, 192, 0, 0, 0);
        tmspaint::imageToTmsVram(blk.data(), 256, 192, Mode::GraphicsII, opt, vram.data());
        std::vector<uint32_t> fb; renderVram(vram, regs2, fb);
        assert(pixelAt(fb, 128, 96) == TMS9918::kPalette[1]);   // black/backdrop

        const auto wht = solidImage(256, 192, 255, 255, 255);
        tmspaint::imageToTmsVram(wht.data(), 256, 192, Mode::GraphicsII, opt, vram.data());
        renderVram(vram, regs2, fb);
        assert(pixelAt(fb, 128, 96) == TMS9918::kPalette[15]);  // white
    }

    // ── Letterbox bars stay backdrop (Graphics II, fit) ──────────────────────
    {
        // A tall, narrow white source: fit scales to height → active span is the
        // centred 64-px column [96,160); the side bars must render backdrop.
        const auto img = solidImage(64, 192, 255, 255, 255);
        std::vector<uint8_t> vram(tmspaint::kVramSize, 0);
        tmspaint::ImportOptions fit; fit.stretch = false;
        tmspaint::imageToTmsVram(img.data(), 64, 192, Mode::GraphicsII, fit, vram.data());
        std::vector<uint32_t> fb; renderVram(vram, regs2, fb);
        assert(pixelAt(fb, 8, 96)   == TMS9918::kPalette[1]);    // left bar = backdrop
        assert(pixelAt(fb, 248, 96) == TMS9918::kPalette[1]);    // right bar = backdrop
        assert(pixelAt(fb, 128, 96) == TMS9918::kPalette[15]);   // centre = white image
    }

    // ── Early-abort + warm-start pair search == exhaustive scan (bit-exact) ──
    // The 120-pair Graphics II search accumulates its walked cost pixel by pixel
    // and aborts strictly above the best-so-far, seeded by the previous row's
    // pair; ties are always evaluated to completion and resolve by pair index,
    // so the result must be bit-identical to the exhaustive scan.
    {
        std::vector<uint8_t> img(256 * 192 * 4);
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x) {
                const size_t i = (static_cast<size_t>(y) * 256 + x) * 4;
                img[i]   = static_cast<uint8_t>(x);
                img[i+1] = static_cast<uint8_t>(255 - x);
                img[i+2] = static_cast<uint8_t>(y);
                img[i+3] = 255;
            }
        for (int kern = 0; kern < 2; ++kern) {
            tmspaint::ImportOptions fastOpt;
            fastOpt.kernel = kern ? hgrpaint::DitherKernel::JarvisMod
                                  : hgrpaint::DitherKernel::FloydSteinberg;
            tmspaint::ImportOptions exOpt = fastOpt;
            exOpt.exhaustiveSearch = true;
            std::vector<uint8_t> va(tmspaint::kVramSize, 0), vb(tmspaint::kVramSize, 0);
            tmspaint::imageToTmsVram(img.data(), 256, 192, Mode::GraphicsII, fastOpt, va.data());
            tmspaint::imageToTmsVram(img.data(), 256, 192, Mode::GraphicsII, exOpt, vb.data());
            assert(va == vb);
        }
    }

    std::printf("tms_convert_smoke: OK\n");
    return 0;
}
