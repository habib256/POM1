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
//
// External deps the host must provide (beyond ImGui + GL):
//   - IconsFontAwesome6.h on the include path (toolbar glyphs).
//   - stb_image.h + stb_image_write.h on the include path, and the STB_IMAGE*
//     _IMPLEMENTATION compiled once in the host (HgrImageDecode.cpp/the PNG
//     export use stb for the image-import + Save-PNG features).

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

    // Optional bulk-write batching. Between beginBatch()/endBatch() the host MAY
    // coalesce the intervening pokeByte() writes into a single transaction (one
    // lock + one screen/snapshot update) instead of one per byte. Default = no-op,
    // so a host without batching still works unchanged. The editor brackets bulk
    // edits (fill, clear, paste, undo/redo, image apply) with these; interactive
    // freehand stays unbatched so it still appears live. Calls are not nested.
    virtual void beginBatch() {}
    virtual void endBatch() {}

    // Render an editor page into a kHiresWidth×kHiresHeight RGBA buffer through the
    // host's real video pipeline — the same renderer its screen uses, so the canvas
    // is pixel-identical to the emulator. `mono` selects a monochrome render for the
    // editor's mono preview; colour otherwise. `grMode=false` renders the bytes as an
    // 8 KB HIRES page ($2000-layout); `grMode=true` renders the first 1 KB as a
    // lo-res (GR) text page ($0400-layout, 40×48 16-colour blocks upscaled to the
    // same 280×192 canvas). `outRgba` holds at least kHiresWidth*kHiresHeight pixels
    // in GL_RGBA / GL_UNSIGNED_BYTE order.
    virtual void renderHgrPage(const uint8_t* page8k, uint32_t* outRgba, bool mono,
                               bool grMode = false) = 0;

    // Load a raw image dump from `path` into video RAM at absolute `baseAddr` (the
    // file's own length is loaded — an 8 KB HIRES page or a 1 KB lo-res page). Save
    // `sizeBytes` of video RAM at `baseAddr` to `path` (kHiresSize for HIRES, 0x400
    // for lo-res GR). Export the supplied RGBA image (w×h, top-down) to a PNG. Each
    // returns false + sets `err` on failure.
    virtual bool loadImage(const std::string& path, uint16_t baseAddr, std::string& err) = 0;
    virtual bool saveImage(const std::string& path, uint16_t baseAddr, int sizeBytes,
                           std::string& err) = 0;
    virtual bool savePng(const std::string& path, const uint32_t* rgba,
                         int w, int h, std::string& err) = 0;

    // Texture lifecycle — the HOST owns the graphics backend, so the portable
    // editor never names GL/GLFW/SDL. The editor hands over RGBA8 (w*h, top-down)
    // and draws the returned opaque handle via ImGui::Image((ImTextureID)handle).
    // uploadTexture: pass tex==0 to create; reuse the returned handle to update.
    // `linear` = bilinear filtering (true) vs crisp nearest (false). The default
    // no-op impls let a headless/test host link without any graphics backend.
    virtual unsigned int uploadTexture(unsigned int tex, const void* rgba,
                                       int w, int h, bool linear)
    { (void)tex; (void)rgba; (void)w; (void)h; (void)linear; return 0; }
    virtual void destroyTexture(unsigned int tex) { (void)tex; }
};

} // namespace hgrpaint

#endif // HGRPAINT_IHGRPAINT_HOST_H
