// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR Paint portable module — the seam between the emulator-agnostic editor
// (HgrPaintEditor) and a host emulator (POM1 / POM2). The host owns the video
// RAM, the NTSC renderer and the file I/O; HgrPaintEditor owns the canvas,
// tools, undo and clipboard. Mirrors bench/IBenchHost.
//
// To port the HGR Paint editor to a new emulator: copy hgrpaint/ verbatim and
// implement one IHgrPaintHost (poke a byte, render an 8 KB page to RGBA, and the
// three file ops). No HgrPaintEditor change needed.

#ifndef HGRPAINT_IHGRPAINT_HOST_H
#define HGRPAINT_IHGRPAINT_HOST_H

#include <cstdint>
#include <string>

namespace hgrpaint {

class IHgrPaintHost {
public:
    virtual ~IHgrPaintHost() = default;

    // Poke one byte into the host's video RAM so strokes appear on the live
    // screen in real time. `addr` is an absolute CPU address (page base + offset).
    virtual void pokeByte(uint16_t addr, uint8_t value) = 0;

    // Render an 8 KB HGR page (page-relative bytes, $2000-layout) into a
    // kHiresWidth×kHiresHeight RGBA buffer through the host's real NTSC pipeline
    // — the same renderer its screen uses, so the editor canvas is pixel-identical
    // to the emulator. `mono` selects a monochrome render for the editor's mono
    // preview; colour otherwise. `outRgba` holds at least kHiresWidth*kHiresHeight
    // pixels in GL_RGBA / GL_UNSIGNED_BYTE order.
    virtual void renderHgrPage(const uint8_t* page8k, uint32_t* outRgba, bool mono) = 0;

    // Load a raw 8 KB HGR image from `path` into video RAM at absolute `baseAddr`.
    // Save the 8 KB page at `baseAddr` to `path`. Export the supplied RGBA image
    // (w×h, top-down) to a PNG. Each returns false + sets `err` on failure.
    virtual bool loadImage(const std::string& path, uint16_t baseAddr, std::string& err) = 0;
    virtual bool saveImage(const std::string& path, uint16_t baseAddr, std::string& err) = 0;
    virtual bool savePng(const std::string& path, const uint32_t* rgba,
                         int w, int h, std::string& err) = 0;
};

} // namespace hgrpaint

#endif // HGRPAINT_IHGRPAINT_HOST_H
