#ifndef GRAPHICSCARD_H
#define GRAPHICSCARD_H

#include <array>
#include <cstdint>

/**
 * GEN2 Color Graphics Card — Uncle Bernie (AppleFritter)
 *
 * Emulates Uncle Bernie's HIRES color graphics card for the Apple 1.
 * The card passively reads CPU RAM at $2000-$3FFF and displays a
 * 280×192 image on a separate video output (rendered as a separate
 * ImGui window in POM1).
 *
 * The Apple II HIRES memory layout and NTSC artifact color encoding
 * are used, since the GEN2 card is Apple II compatible by design.
 *
 * Rendering follows MAME's `apple2video.cpp` (PR #10773 by benrg) — the
 * gold-standard algorithm calibrated against real Apple II hardware.
 * Three building blocks:
 *
 *   1. Bit doubler. Each of the 7 visible HGR bits is duplicated to
 *      give a 14-bit word per byte (40 × 14 = 560 sub-pixels per line
 *      at the master 14.32 MHz cadence).
 *   2. Half-dot delay (MSB). When a byte's bit 7 is set, the entire
 *      14-bit word is shifted left by 1 sub-pixel, with the top bit
 *      of the previous byte's word feeding bit 0 — the 74LS74 flip-flop
 *      delay (~70 ns / 90° chroma phase) the silicon implements.
 *      MSB-toggle byte-boundary fringing falls out for free.
 *   3. 7-bit sliding window + 4-phase rotation. A 7-bit window walks
 *      the doubled stream with 3 bits of left context. Each sub-pixel
 *      indexes the 128-entry MAME LUT; rotl4b extracts the 4-bit
 *      palette index for the current absolute sub-pixel x mod 4.
 *      The 4-bit result indexes the same MAME 16-colour table the
 *      lo-res mode would use on a //e — the artefact colour drops out
 *      naturally.
 *
 * Output is 560 sub-pixels per scanline averaged into 280 framebuffer
 * pixels (the chroma-bandwidth-limited downsample real CRTs perform
 * optically).
 *
 * Cosmetic toggles (HGR window controls — moniteur, not silicon):
 *  - monitorMode: Colour / Green / Amber / Monochrome — desaturates
 *    then tints the rendered image to mimic the monitor at the other
 *    end of the composite cable. Re-uses the Screen_ImGui pattern.
 *  - phosphorPersistence: 0..1. Frame-to-frame lerp toward the new
 *    rasterised pixels — non-zero values force a full upload each
 *    frame (the per-line diff is bypassed).
 *
 * Performance: rasterizeToBuffer() fills a 280×192 RGBA pixel buffer in
 * software and is 100% CPU — no shaders, no GL3+ dependency. Per-scanline
 * memcmp against a cached copy of the previous frame's 40 framebuffer
 * bytes skips redrawing rows that didn't change (memcmp on 40 bytes
 * auto-vectorises to a couple of SIMD loads). The caller uploads the
 * buffer to a GL texture (one ImGui::Image draw call per frame) instead
 * of emitting ~100 k AddRectFilled per frame.
 */
class GraphicsCard
{
public:
    static constexpr int kHiresWidth = 280;
    static constexpr int kHiresHeight = 192;
    static constexpr uint16_t kHiresBase = 0x2000;
    static constexpr int kHiresSize = 0x2000; // 8 KB

    enum class MonitorMode : uint8_t {
        Colour = 0,
        Green = 1,
        Amber = 2,
        Monochrome = 3,
    };

    GraphicsCard();

    // Rasterize the HIRES framebuffer into the internal RGBA pixel buffer.
    // Per-scanline diff means rows that didn't change skip the inner loop.
    // Returns true if any pixel changed since the previous call (caller can
    // skip the GL upload otherwise; harmless to ignore).
    // memory must point to the full 64 KB address space.
    bool rasterizeToBuffer(const uint8_t* memory);

    // RGBA pixel buffer, kHiresWidth × kHiresHeight, byte order matches
    // GL_RGBA + GL_UNSIGNED_BYTE on little-endian (same convention as TMS9918).
    const uint32_t* pixels() const { return pixelBuf.data(); }

    // Force a full re-rasterization on the next call (used after toggling the
    // card off/on so a stale buffer doesn't survive).
    void invalidate();

    // Compute the address of a HIRES scanline (Apple II non-linear layout).
    static uint16_t scanlineAddress(int y);

    // -------- Cosmetic monitor toggles (HGR window) --------
    void setMonitorMode(MonitorMode m) {
        if (m != monitorMode) { monitorMode = m; invalidate(); }
    }
    MonitorMode getMonitorMode() const { return monitorMode; }
    void setPhosphorPersistence(float p) { phosphorPersistence = p; }
    float getPhosphorPersistence() const { return phosphorPersistence; }
    void setScanlineAlpha(float a) { scanlineAlpha = a; }
    float getScanlineAlpha() const { return scanlineAlpha; }

private:
    void rasterizeLine(int y, const uint8_t* memory);
    // Apply monitor tint + persistence lerp to a single scanline of pixelBuf.
    void postProcessLine(int y);

    // Cached copy of the previous frame's 40 framebuffer bytes per scanline
    // for the per-line skip diff.
    std::array<std::array<uint8_t, 40>, kHiresHeight> lineCopy{};
    bool invalidateNext = true;
    std::array<uint32_t, kHiresWidth * kHiresHeight> pixelBuf{};
    // Snapshot of the previous frame's pixelBuf — only allocated when
    // phosphorPersistence > 0, but kept resident so the lerp stays cheap.
    std::array<uint32_t, kHiresWidth * kHiresHeight> prevPixelBuf{};

    // Cosmetic state
    MonitorMode monitorMode = MonitorMode::Colour;
    float       phosphorPersistence = 0.0f;   // 0=snap, 1=hold previous frame
    float       scanlineAlpha       = 0.0f;   // 0=no overlay, 1=fully black rows
};

#endif // GRAPHICSCARD_H
