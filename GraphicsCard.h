#ifndef GRAPHICSCARD_H
#define GRAPHICSCARD_H

#include "imgui.h"
#include <array>
#include <cstdint>

/**
 * GEN2 Color Graphics Card — Uncle Bernie (AppleFritter)
 *
 * Emulates Uncle Bernie's HIRES color graphics card for the Apple 1.
 * The card passively reads CPU RAM at $2000-$3FFF and displays a
 * 280×192 NTSC artifact-color image on a separate video output
 * (rendered as a separate ImGui window in POM1).
 *
 * The Apple II HIRES memory layout and NTSC artifact color encoding
 * are used, as the GEN2 card is Apple II compatible by design.
 *
 * Performance: rasterizeToBuffer() fills a 280×192 RGBA pixel buffer
 * in software. Per-scanline memcmp against a cached copy of the previous
 * frame's 40 framebuffer bytes skips redrawing rows that didn't change.
 * memcmp on 40 bytes is auto-vectorised (NEON/AVX) down to a handful of
 * cycles — much cheaper than the FNV-1a hash it replaced. The caller
 * uploads the buffer to a GL texture (one ImGui::Image draw call per
 * frame) instead of emitting ~100 k AddRectFilled per frame.
 */
class GraphicsCard
{
public:
    using quint8 = uint8_t;

    static constexpr int kHiresWidth = 280;
    static constexpr int kHiresHeight = 192;
    static constexpr uint16_t kHiresBase = 0x2000;
    static constexpr int kHiresSize = 0x2000; // 8 KB

    GraphicsCard();

    // Rasterize the HIRES framebuffer into the internal RGBA pixel buffer.
    // Per-scanline hashing means rows that didn't change skip the inner loop.
    // Returns true if any pixel changed since the previous call (caller can
    // skip the GL upload otherwise; harmless to ignore).
    // memory must point to the full 64 KB address space.
    bool rasterizeToBuffer(const quint8* memory);

    // RGBA pixel buffer, kHiresWidth × kHiresHeight, byte order matches
    // GL_RGBA + GL_UNSIGNED_BYTE on little-endian (same convention as TMS9918).
    const uint32_t* pixels() const { return pixelBuf.data(); }

    // Force a full re-rasterization on the next call (used after toggling the
    // card off/on so a stale buffer doesn't survive).
    void invalidate();

    // Compute the address of a HIRES scanline (Apple II non-linear layout).
    static uint16_t scanlineAddress(int y);

private:
    void rasterizeLine(int y, const quint8* memory);

    // Cached copy of the previous frame's 40 framebuffer bytes per scanline.
    // rasterizeToBuffer() memcmp's the current scanline against this and only
    // re-rasters on mismatch, then refreshes the copy. `invalidateNext` forces
    // one full repaint (used after invalidate()) before the diff kicks in.
    std::array<std::array<uint8_t, 40>, kHiresHeight> lineCopy{};
    bool invalidateNext = true;
    std::array<uint32_t, kHiresWidth * kHiresHeight> pixelBuf{};
};

#endif // GRAPHICSCARD_H
