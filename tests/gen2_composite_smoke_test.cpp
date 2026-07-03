// GEN2 HGR — OpenEmulator composite NTSC demodulator (CPU path).
//
// Pins GraphicsCard::RenderMode::CompositeOECpu, the port of POM2's
// Apple2Display::renderCompositeOeCpu(). The composite path builds the
// 14.318 MHz signal from the same doubled-word HGR bitstream as the MAME LUT
// path, runs the 17-tap FIR NTSC demodulator (kernels + YUV→RGB matrix
// verified verbatim against OpenEmulator libemulation OpenGLCanvas.cpp), and
// pair-averages the 560 sub-pixels down to the shared 280×192 RGBA buffer.
//
// Checks the demod is (a) achromatic + bright on a solid-white HGR field,
// (b) genuinely chromatic on a solid artifact-colour field, (c) black on an
// empty field, (d) distinct from the LUT pipeline, and (e) that the mode
// toggle re-rasterises. Self-contained: GraphicsCard + Gen2VideoScanner only.

#include "GraphicsCard.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

using DS = GraphicsCard::DisplayState;
using RM = GraphicsCard::RenderMode;

constexpr int kW = GraphicsCard::kHiresWidth;   // 280
constexpr int kH = GraphicsCard::kHiresHeight;  // 192

DS hgrState() { DS d; d.textMode = false; d.hiRes = true; return d; }

int chan(uint32_t px, int shift) { return static_cast<int>((px >> shift) & 0xFFu); }
int red  (uint32_t px) { return chan(px, 0);  }
int green(uint32_t px) { return chan(px, 8);  }
int blue (uint32_t px) { return chan(px, 16); }

// Fill HGR page 1 ($2000-$3FFF) with a per-column byte pattern (even column
// bytes = `evenByte`, odd = `oddByte`) so every scanline is the same field,
// render a full HGR frame, and return the pixel buffer. A uniform $55 fill is
// NOT a solid colour — 7 px/byte isn't a multiple of the 4-phase subcarrier,
// so the hue flips violet↔green each byte; the $55/$2A alternation is what
// yields a stable single artifact colour (as the horizontal-split test uses).
std::vector<uint32_t> renderFill(GraphicsCard& card, uint8_t evenByte,
                                 uint8_t oddByte, RM mode)
{
    std::vector<uint8_t> mem(0x10000, 0);
    for (int i = 0; i < 0x2000; ++i) mem[0x2000 + i] = (i & 1) ? oddByte : evenByte;
    card.setRenderMode(mode);
    card.render(mem.data(), hgrState(), hgrState(), {});
    return std::vector<uint32_t>(card.pixels(), card.pixels() + kW * kH);
}

// Average a channel over an interior span of one scanline (avoids the FIR's
// line-edge roll-off so a uniform field reads as its steady-state colour).
struct RGB { int r, g, b; };
RGB avgSpan(const std::vector<uint32_t>& fb, int y, int x0, int x1)
{
    long r = 0, g = 0, b = 0;
    for (int x = x0; x < x1; ++x) {
        const uint32_t px = fb[y * kW + x];
        r += red(px); g += green(px); b += blue(px);
    }
    const int n = x1 - x0;
    return { int(r / n), int(g / n), int(b / n) };
}

} // namespace

int main()
{
    GraphicsCard card;

    // ── API round-trip ────────────────────────────────────────────────────
    CHECK(card.getRenderMode() == RM::MameLut, "default render mode = MameLut");
    card.setRenderMode(RM::CompositeOECpu);
    CHECK(card.getRenderMode() == RM::CompositeOECpu, "setRenderMode sticks");
    card.setRenderMode(RM::MameLut);

    // ── (a) Solid white field ($7F: 7 bits set, no half-dot) ───────────────
    // The composite demod should read near-white and roughly achromatic.
    {
        const auto comp = renderFill(card, 0x7F, 0x7F, RM::CompositeOECpu);
        const RGB c = avgSpan(comp, 100, 70, 210);
        CHECK(c.r > 170 && c.g > 170 && c.b > 170, "white field is bright");
        const int spread = std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
        CHECK(spread < 70, "white field is roughly achromatic");
    }

    // ── (b) Solid artifact-colour field ($55/$2A → violet) ─────────────────
    // The $55/$2A byte alternation is a stable HGR artifact colour; the demod
    // must produce a genuinely chromatic field (channels far apart), and it
    // must differ from the MAME LUT rendering of the same field.
    {
        const auto comp = renderFill(card, 0x55, 0x2A, RM::CompositeOECpu);
        const auto lut  = renderFill(card, 0x55, 0x2A, RM::MameLut);
        const RGB c = avgSpan(comp, 100, 70, 210);
        const int spread = std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
        CHECK(spread > 40, "artifact field is chromatic (channels differ)");

        const RGB l = avgSpan(lut, 100, 70, 210);
        const int diff = std::abs(c.r - l.r) + std::abs(c.g - l.g) + std::abs(c.b - l.b);
        CHECK(diff > 30, "composite pipeline differs from MAME LUT");
    }

    // ── (c) Empty field → black ────────────────────────────────────────────
    {
        const auto comp = renderFill(card, 0x00, 0x00, RM::CompositeOECpu);
        const RGB c = avgSpan(comp, 100, 0, kW);
        CHECK(c.r == 0 && c.g == 0 && c.b == 0, "empty field is black");
    }

    // ── (e) Toggling the mode re-rasterises (invalidate on change) ─────────
    {
        std::vector<uint8_t> mem(0x10000, 0);
        for (int i = 0; i < 0x2000; ++i) mem[0x2000 + i] = 0x55;

        card.setRenderMode(RM::MameLut);
        card.render(mem.data(), hgrState(), hgrState(), {});
        const std::vector<uint32_t> lut(card.pixels(), card.pixels() + kW * kH);

        card.setRenderMode(RM::CompositeOECpu);
        // No RAM changed; only the mode did. The fast-path diff must still
        // repaint because setRenderMode() invalidated the line cache.
        const bool changed = card.render(mem.data(), hgrState(), hgrState(), {});
        CHECK(changed, "mode switch forces a repaint");
        bool anyDiff = false;
        for (int i = 0; i < kW * kH && !anyDiff; ++i)
            anyDiff = (card.pixels()[i] != lut[i]);
        CHECK(anyDiff, "buffer actually changed after mode switch");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gen2_composite_smoke: all checks passed\n");
    return 0;
}
