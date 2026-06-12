#ifndef GRAPHICSCARD_H
#define GRAPHICSCARD_H

#include "Gen2VideoScanner.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

/**
 * GEN2 Color Graphics Card — Uncle Bernie (AppleFritter)
 *
 * Emulates Uncle Bernie's *release* color graphics card for the Apple 1:
 * a full Apple II-compatible video subsystem (TEXT 40×24 B&W, LORES 40×48
 * 16 colours, HIRES 280×192 NTSC artifact colour, MIXED split) displayed
 * on its own video output (a separate ImGui window in POM1). The display
 * mode is driven by the read-only soft switches at $C250-$C257 (see
 * Gen2VideoScanner.h and doc/GEN2_RELEASE_questions.md for the spec).
 *
 * Two rendering paths (back-port of POM2's beam-racing engine):
 *
 *   * Fast path — no soft-switch flip during the frame AND the latch sits
 *     at the classic GRAPHICS+HIRES+PAGE1 state: the original per-scanline
 *     diffed HGR rasteriser runs unchanged (rasterizeToBuffer), so
 *     pre-Phase-2 HGR programs cost exactly what they did before.
 *   * Beam-raced path — render() replays the frame's soft-switch journal:
 *     frameCycleToPos() maps each event's emuCycle to (scanline, byteCol),
 *     forEachBeamSegment() decomposes the frame into vertical bands ×
 *     horizontal column segments with the display state live for each, and
 *     renderInternalSegment() paints them. Vertical splits (page flips at
 *     VBL, text/graphics bands) and Bernie's signature horizontal
 *     mid-scanline splits (TEXT columns alternating with graphics on one
 *     line) both fall out of the same decomposition. HGR segments decode
 *     the whole scanline (the NTSC artifact window needs neighbour-byte
 *     context) and clip the write-back to the segment's columns.
 *
 * HIRES decode follows MAME's `apple2video.cpp` (PR #10773 by benrg) — the
 * gold-standard algorithm calibrated against real Apple II hardware:
 *
 *   1. Bit doubler. Each of the 7 visible HGR bits is duplicated to give a
 *      14-bit word per byte (40 × 14 = 560 sub-pixels per line at the
 *      master 14.32 MHz cadence).
 *   2. Half-dot delay (MSB). When a byte's bit 7 is set, the entire 14-bit
 *      word is shifted left by 1 sub-pixel, with the top bit of the
 *      previous byte's word feeding bit 0 — the 74LS74 flip-flop delay
 *      (~70 ns / 90° chroma phase) the silicon implements.
 *   3. 7-bit sliding window + 4-phase rotation indexing the 128-entry MAME
 *      LUT — the composite_color_mode=1 **medium-color** row (cleaner
 *      mid-tones); the 4-bit result indexes the lo-res palette. 560
 *      sub-pixels are pair-averaged down to 280 framebuffer pixels (the
 *      chroma-bandwidth downsample a real CRT performs optically).
 *
 * TEXT renders B&W (per Bernie's spec sheet) through a built-in 5×7 ASCII
 * font with the Apple II inverse/flashing attributes; LORES paints 7×4
 * blocks from the 16-colour MAME palette. Both honour PAGE2 ($0800) like
 * the Apple II; HIRES PAGE2 reads $4000-$5FFF.
 *
 * Cosmetic toggles (HGR window controls — moniteur, not silicon):
 *  - monitorMode: Colour / Green / Amber / Monochrome — desaturates then
 *    tints the rendered image. Re-uses the Screen_ImGui pattern.
 *  - phosphorPersistence: 0..1 frame-to-frame lerp toward the new pixels.
 *
 * Performance: everything is CPU software rendering into a 280×192 RGBA
 * buffer — no shaders, no GL3+ dependency. The caller uploads the buffer
 * to a GL texture (one ImGui::Image draw per frame).
 */
class GraphicsCard
{
public:
    static constexpr int kHiresWidth = 280;
    static constexpr int kHiresHeight = 192;
    static constexpr uint16_t kHiresBase = 0x2000;
    static constexpr int kHiresSize = 0x2000; // 8 KB

    using DisplayState = Gen2VideoScanner::DisplayState;
    using Event        = Gen2VideoScanner::Event;
    using EventKind    = Gen2VideoScanner::EventKind;

    enum class MonitorMode : uint8_t {
        Colour = 0,
        Green = 1,
        Amber = 2,
        Monochrome = 3,
    };

    GraphicsCard();

    // ── Beam-raced frame render (Phase 3 entry point) ──────────────────
    // `memory` is the full 64 KB address space. `endState` is the soft-
    // switch latch now, `frameStart` + `events` the journal of the last
    // completed video frame (Memory::gen2PublishedVideoEvents). Returns
    // true when the pixel buffer changed (caller may skip the GL upload).
    // `linesPerFrame` = 262 (60 Hz) or 312 (50 Hz vertical jumper).
    bool render(const uint8_t* memory,
                const DisplayState& endState,
                const DisplayState& frameStart,
                const std::vector<Event>& events,
                uint64_t linesPerFrame = Gen2VideoScanner::kLinesPerFrame);

    // Raster position (visible scanline + 40-byte column index) of a CPU
    // cycle within its video frame — the beam-racing replay's mapping from
    // a journaled event to *where on screen* the beam was. byteCol 0 means
    // "before the visible window opened" (HBL, cycles 0-24): the event
    // governs the whole upcoming line. Verbatim POM2 Apple2Display.
    struct RasterPos { int scanline; int byteCol; };
    static RasterPos frameCycleToPos(uint64_t emuCycle,
                                     uint64_t linesPerFrame = Gen2VideoScanner::kLinesPerFrame);

    // Legacy fast path: rasterize HGR page 1 with the per-scanline diff.
    // Kept public for the pre-Phase-2 callers/tests; render() routes here
    // when the journal is empty and the latch shows GRAPHICS+HIRES+PAGE1.
    bool rasterizeToBuffer(const uint8_t* memory);

    // RGBA pixel buffer, kHiresWidth × kHiresHeight, byte order matches
    // GL_RGBA + GL_UNSIGNED_BYTE on little-endian (same convention as TMS9918).
    const uint32_t* pixels() const { return pixelBuf.data(); }

    // Force a full re-rasterization on the next call (used after toggling the
    // card off/on so a stale buffer doesn't survive).
    void invalidate();

    // Address of the first byte of HIRES scanline `y` (Apple II non-linear
    // interleave). Page 1 = $2000, page 2 = $4000.
    static uint16_t hgrRowAddress(int y, bool page2);
    // Address of the first byte of text/lo-res row `y` (Apple II row
    // interleave). Page 1 = $0400, page 2 = $0800.
    static uint16_t textRowAddress(int y, bool page2);
    // Legacy page-1 helper (== hgrRowAddress(y, false)).
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
    // ── Beam-raced internals (port of POM2 Apple2Display) ──────────────
    // Shared beam-race decomposition: sorts `events` into raster order,
    // builds per-scanline column segments [col0, col1), merges vertically-
    // identical scanlines into bands, and invokes paint(state, y0, y1,
    // col0, col1) for each band × column segment.
    static void forEachBeamSegment(
        const DisplayState& frameStart,
        std::vector<Event> events,
        uint64_t linesPerFrame,
        const std::function<void(const DisplayState&,
                                 int y0, int y1, int col0, int col1)>& paint);
    static void applyVideoEvent(DisplayState& state, EventKind kind, bool value);

    // Paint the band [scanY0, scanY1) full-width under `state` (TEXT /
    // LORES / HIRES / MIXED dispatch — POM2 renderInternalBand, 280-wide
    // legacy subset).
    void renderInternalBand(const uint8_t* memory, const DisplayState& state,
                            int scanY0, int scanY1);
    // Column-bounded variant for horizontal mid-scanline splits.
    void renderInternalSegment(const uint8_t* memory, const DisplayState& state,
                               int scanY0, int scanY1, int col0, int col1);
    void renderHiRes(const uint8_t* memory, const DisplayState& state,
                     int firstScanline, int lastScanline, int col0, int col1);
    void renderText (const uint8_t* memory, const DisplayState& state,
                     int firstRow, int lastRow, int col0, int col1,
                     int clipY0, int clipY1);
    void renderLoRes(const uint8_t* memory, const DisplayState& state,
                     int firstRow, int lastRow, int col0, int col1,
                     int clipY0, int clipY1);

    // Decode one HGR scanline at `rowAddr` and write framebuffer pixels
    // [px0, px1) of row y. The whole line is always decoded so the NTSC
    // artifact window keeps its neighbour context across a split.
    void rasterizeHgrLine(int y, const uint8_t* memory, uint16_t rowAddr,
                          int px0, int px1);
    void rasterizeLine(int y, const uint8_t* memory);   // legacy: page 1, full width
    // Apply monitor tint + persistence lerp to a single scanline of pixelBuf.
    void postProcessLine(int y);

    // Cached copy of the previous frame's 40 framebuffer bytes per scanline
    // for the per-line skip diff (legacy fast path only).
    std::array<std::array<uint8_t, 40>, kHiresHeight> lineCopy{};
    bool invalidateNext = true;
    std::array<uint32_t, kHiresWidth * kHiresHeight> pixelBuf{};
    // Snapshot of the previous frame's pixelBuf — only consulted when
    // phosphorPersistence > 0, but kept resident so the lerp stays cheap.
    std::array<uint32_t, kHiresWidth * kHiresHeight> prevPixelBuf{};

    // Frame counter — drives the Apple II FLASH text attribute (~2 Hz:
    // 16-frame half-period, matching MAME's frame_number() & 0x10).
    uint32_t frameCounter = 0;
    static constexpr uint32_t kFlashHalfPeriodFrames = 16;

    // Cosmetic state
    MonitorMode monitorMode = MonitorMode::Colour;
    float       phosphorPersistence = 0.0f;   // 0=snap, 1=hold previous frame
    float       scanlineAlpha       = 0.0f;   // 0=no overlay, 1=fully black rows
};

#endif // GRAPHICSCARD_H
