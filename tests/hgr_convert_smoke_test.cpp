// HGR image-conversion smoke test — pins the portable hgrpaint/ importer
// (CAM16-UCS perceptual space + the NTSC analysis-by-synthesis converter) with
// no GL/ImGui dependency.
//
// Covers: CAM16-UCS sanity + perceptual ranking; the module's NTSC decode is
// byte-identical to GraphicsCard (the real renderer); black→empty page;
// white→white; and Floyd-Steinberg tone conservation on a black→white ramp.

#include "Cam16.h"
#include "HgrConvert.h"
#include "HgrPaintModel.h"
#include "GraphicsCard.h"

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

    std::printf("hgr_convert_smoke: all assertions passed\n");
    return 0;
}
