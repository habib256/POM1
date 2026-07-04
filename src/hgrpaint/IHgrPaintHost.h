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
#include <vector>

#include "imgui.h"   // ImTextureID for textureToImTexture()

namespace hgrpaint {

// A ready-made sprite from the host's built-in library (POM1 ships the GEN2 HGR
// SCROLL-O-SPRITES catalogue under dev/lib/gen2/sprites/). `bytes` is row-major
// HGR (hRows rows × wBytes bytes/row, bit 0 = leftmost pixel) — exactly the
// layout hgrsprite::stamp expects, so the sprite editor drops it straight into
// its scratch page. Empty for a host with no such library (the default).
struct DevSprite {
    std::string name;
    int wBytes = 3, hRows = 16;
    std::vector<uint8_t> bytes;      // wBytes*hRows
};
struct DevSpriteCategory {
    std::string name;
    std::vector<DevSprite> sprites;
};

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

    // Sync the host's LIVE display to the page/mode the editor is now editing:
    // grMode selects lo-res GR vs HIRES, page2 selects page 2 vs page 1. A POM1/
    // POM2 host flips its graphics soft switches (GRAPHICS + full screen + the
    // page + HIRES/lo-res) so the on-screen card follows the editor's HGR/HGR2/
    // GR/GR2 selector. Default no-op keeps the portable editor + headless/test
    // hosts unaffected (their canvas is rendered from the page bytes regardless).
    virtual void setDisplayMode(bool grMode, bool page2) { (void)grMode; (void)page2; }

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

    // Native OS file picker. Returns true and writes the chosen absolute path
    // into `outPath` when the host can show a native dialog (a desktop POM1/POM2
    // host wired to its OS picker); false -> the editor falls back to its own
    // built-in ImGui file browser (the default below, also taken on WASM or a
    // Linux box without zenity/kdialog). `extCsv` is a comma-separated extension
    // list WITHOUT dots, e.g. "png,jpg,bmp" or "hgr"; empty matches everything.
    // The default keeps the portable editor self-contained — it never names a
    // native dialog itself.
    virtual bool pickFilePath(bool forSave,
                              const std::string& title,
                              const std::string& filterDesc,
                              const std::string& extCsv,
                              const std::string& defaultDir,
                              const std::string& defaultName,
                              std::string& outPath)
    {
        (void)forSave; (void)title; (void)filterDesc; (void)extCsv;
        (void)defaultDir; (void)defaultName; (void)outPath;
        return false;
    }

    // True when the host can pop an OS-native picker right now. Lets the editor
    // tell the two false-returns of pickFilePath apart: when this is true a false
    // return means the user CANCELLED, so the editor must NOT fall back to the
    // built-in ImGui browser (jarring to switch UIs mid-pick). When this is false
    // (default: WASM, or Linux without zenity/kdialog) pickFilePath is never
    // available and the ImGui browser is the only path.
    virtual bool nativeFilePickerAvailable() const { return false; }

    // Initial directory the file browser (native picker AND the ImGui fallback)
    // should open in on first use. Empty (the default) = the portable editor
    // falls back to the process CWD, keeping it standalone for POM2. POM1's host
    // returns the writable sdcard/HGR/ folder so painted pages round-trip with
    // the microSD SD CARD OS. Mirrors IBenchHost::browseDir().
    virtual std::string browseDir() const { return {}; }

    // The host's built-in HGR sprite library, grouped by category, or empty when
    // the host ships none (the default). POM1 parses dev/lib/gen2/sprites/*.asm;
    // the sprite editor shows a browser so you can drop a ready-made sprite into
    // the canvas. Called rarely (the editor caches the result), so a host MAY
    // parse files on demand here.
    virtual std::vector<DevSpriteCategory> devSprites() { return {}; }

    // Texture lifecycle — the HOST owns the graphics backend, so the portable
    // editor never names GL/GLFW/SDL/Metal. The editor hands over RGBA8 (w*h,
    // top-down) and draws the returned opaque handle via
    //   ImGui::Image(host->textureToImTexture(handle), ...);
    // so the host can translate the handle into whatever ImTextureID its
    // graphics backend expects (GLuint for OpenGL, id<MTLTexture> pointer for
    // Metal, etc. — see PomRenderer::asImTextureID).
    //
    // uploadTexture: pass tex==nullptr to create; reuse the returned handle
    // for follow-up uploads. The host MAY destroy + recreate internally when
    // the dimensions change (matches the historical glTexImage2D semantics).
    // `linear` selects bilinear filtering vs crisp nearest. Default no-op
    // impls let a headless/test host link without any graphics backend.
    virtual void* uploadTexture(void* tex, const void* rgba,
                                int w, int h, bool linear)
    { (void)tex; (void)rgba; (void)w; (void)h; (void)linear; return nullptr; }
    virtual void  destroyTexture(void* tex) { (void)tex; }

    // Translate an opaque texture handle into the ImTextureID accepted by
    // ImGui::Image / AddImage. Default: bit-cast the pointer through
    // uintptr_t (correct for backends whose ImTextureID *is* the texture
    // pointer, e.g. Metal's id<MTLTexture>); the GL implementation overrides
    // to return the underlying GLuint instead.
    virtual ImTextureID textureToImTexture(void* tex) const
    { return (ImTextureID)(uintptr_t)tex; }
};

} // namespace hgrpaint

#endif // HGRPAINT_IHGRPAINT_HOST_H
