// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 Paint portable module — the seam between the emulator-agnostic editor
// (TmsPaintEditor, forthcoming) and a host emulator (POM1 / POM2). The host owns
// the TMS9918 VRAM, its renderer and the file I/O; the editor owns the canvas,
// tools, undo and clipboard. Mirrors hgrpaint/IHgrPaintHost — but the TMS9918's
// 16 KB VRAM lives behind the $CC00/$CC01 ports, NOT in the 6502 address space,
// so pokes carry a VRAM address (0..0x3FFF), not a CPU address.
//
// To port the editor to a new emulator: copy tmspaint/ verbatim and implement
// one ITmsPaintHost (poke VRAM, program the mode registers, render VRAM to RGBA,
// and the file ops). No editor change needed.
//
// External deps the host must provide (beyond ImGui + GL): IconsFontAwesome6.h
// on the include path, and (for the forthcoming image-import + Save-PNG) stb on
// the include path with STB_IMAGE*_IMPLEMENTATION compiled once in the host.

#ifndef TMSPAINT_ITMSPAINT_HOST_H
#define TMSPAINT_ITMSPAINT_HOST_H

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"   // ImTextureID for textureToImTexture()

namespace tmspaint {

// A ready-made sprite from the host's built-in library (POM1 ships the TMS9918
// SCROLL-O-SPRITES catalogue under dev/lib/tms9918/). `bytes` is the native
// 32-byte 16×16 sprite pattern (left half col 0..7 = 16 bytes, then right half
// col 8..15 = 16 bytes) that streams straight into a sprite-pattern slot group
// at $3800 + patNum*8. Empty for a host with no such library (the default).
struct DevSprite {
    std::string name;
    std::vector<uint8_t> bytes;      // 32 (16×16)
};
struct DevSpriteCategory {
    std::string name;
    std::vector<DevSprite> sprites;
};

class ITmsPaintHost {
public:
    virtual ~ITmsPaintHost() = default;

    // Poke one byte into the host's TMS9918 VRAM (16 KB, addr & 0x3FFF) so
    // strokes appear on the live Graphic Card window in real time.
    virtual void pokeVram(uint16_t addr, uint8_t value) = 0;

    // Optional bulk-write batching — same contract as hgrpaint: between
    // beginBatch()/endBatch() the host MAY coalesce the intervening pokeVram()
    // writes into one transaction (single lock + single screen/snapshot update).
    // Default = no-op. The editor brackets bulk edits (fill, clear, paste,
    // undo/redo, image apply); interactive freehand stays unbatched. Not nested.
    virtual void beginBatch() {}
    virtual void endBatch() {}

    // Program the canonical register set `regs[8]` onto the live chip so the
    // poked VRAM actually displays in the chosen mode. Called when the editor
    // selects / switches a canvas mode.
    virtual void applyRegisters(const uint8_t regs[8]) = 0;

    // Render a 16 KB VRAM image + `regs[8]` to a kGfx2Width×kGfx2Height (256×192)
    // RGBA buffer through the host's real TMS9918 pipeline. Used for the image-
    // import preview (a hypothetical VRAM not yet on the chip).
    virtual void renderVram(const uint8_t* vram16k, const uint8_t regs[8],
                            uint32_t* outRgba) = 0;

    // Copy the live chip's 16 KB VRAM (the editor's read source for its drawing
    // model — colour/flood/eyedropper), so the editor mirrors the real chip
    // regardless of snapshot gating.
    virtual void readVram(uint8_t* out16k) = 0;

    // Copy the live chip's active 256×192 framebuffer — the chip's own rendered
    // output, exactly what the card window shows. The editor displays this as its
    // canvas so it never diverges from the real card. Returns false if no live
    // framebuffer is available (e.g. a headless host); the editor then falls back
    // to renderVram(). `outRgba` holds at least 256*192 pixels (GL_RGBA order).
    virtual bool liveFramebuffer(uint32_t* outRgba) = 0;

    // Load a raw 16 KB VRAM image from `path` into the chip / `saveVram` the
    // current 16 KB VRAM to `path` / export the supplied RGBA image (w×h,
    // top-down) to a PNG. Each returns false + sets `err` on failure.
    virtual bool loadVram(const std::string& path, std::string& err) = 0;
    virtual bool saveVram(const std::string& path, std::string& err) = 0;
    virtual bool savePng(const std::string& path, const uint32_t* rgba,
                         int w, int h, std::string& err) = 0;

    // Native OS file picker — see hgrpaint/IHgrPaintHost.h::pickFilePath for the
    // contract. Returns true + writes the chosen path on a native desktop host;
    // false (the default) -> the editor falls back to its ImGui file browser.
    // `extCsv` is a comma-separated extension list WITHOUT dots ("png,jpg" / "tms").
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

    // True when the host can pop an OS-native picker right now — see
    // hgrpaint/IHgrPaintHost.h::nativeFilePickerAvailable for the contract.
    // When true a false return from pickFilePath means the user CANCELLED, so
    // the editor must NOT fall back to its built-in ImGui browser.
    virtual bool nativeFilePickerAvailable() const { return false; }

    // Initial directory the file browser (native + ImGui fallback) opens in on
    // first use. Empty (default) = process CWD (keeps the portable editor
    // standalone). POM1's host returns the writable sdcard/TMS/ folder. Mirrors
    // hgrpaint/IHgrPaintHost.h::browseDir and IBenchHost::browseDir.
    virtual std::string browseDir() const { return {}; }

    // The host's built-in TMS9918 sprite library, grouped by category, or empty
    // when the host ships none (the default). POM1 parses dev/lib/tms9918/
    // sprites_*.asm; the sprite editor shows a browser so you can drop a ready-made
    // 16×16 sprite into VRAM. Called rarely (the editor caches the result).
    virtual std::vector<DevSpriteCategory> devSprites() { return {}; }

    // Texture lifecycle — the HOST owns the graphics backend (see
    // hgrpaint/IHgrPaintHost.h for the design rationale). Opaque void* so a
    // Metal id<MTLTexture> fits the same slot as a GL GLuint. Pass
    // tex==nullptr to create; the host MAY reallocate internally on
    // dimension change. `linear` = bilinear filtering vs crisp nearest.
    // Default no-op impls let a headless/test host link without graphics.
    virtual void* uploadTexture(void* tex, const void* rgba,
                                int w, int h, bool linear)
    { (void)tex; (void)rgba; (void)w; (void)h; (void)linear; return nullptr; }
    virtual void  destroyTexture(void* tex) { (void)tex; }

    // Translate an opaque texture handle into the ImTextureID accepted by
    // ImGui::Image. Default: pointer bit-cast; GL hosts override to return
    // the underlying GLuint.
    virtual ImTextureID textureToImTexture(void* tex) const
    { return (ImTextureID)(uintptr_t)tex; }
};

} // namespace tmspaint

#endif // TMSPAINT_ITMSPAINT_HOST_H
