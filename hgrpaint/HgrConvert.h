// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Image → Apple II HGR conversion, ii-pix style — the high-quality importer for
// the HGR Paint editor. Part of the portable hgrpaint/ toolkit (no emulator
// dependency): it decodes PNG/JPG (via stb_image, in a separate TU), resamples
// to 280×192, then dithers to the 8 KB HGR page by **analysis-by-synthesis**:
// for every byte it tries all 256 (7 pixels + palette) patterns, renders each
// through this module's own copy of the Apple II NTSC artifact pipeline (the same
// maths as GraphicsCard — pinned equal in the test), measures the result against
// the target in **CAM16-UCS** perceptual space, and keeps the closest, with
// Floyd-Steinberg error diffusion between bytes and rows.
//
// This beats Buckshot/bmp2dhr because it dithers against the *true* NTSC colours
// (including the sliding-window pixel coupling) rather than a fixed palette.

#ifndef HGRPAINT_CONVERT_H
#define HGRPAINT_CONVERT_H

#include <cstdint>
#include <string>
#include <vector>

namespace hgrpaint {

struct ImportOptions {
    bool  stretch = false;     // false = fit + letterbox (keep aspect), true = stretch
    bool  dither  = true;      // Floyd-Steinberg error diffusion
    bool  serpentine = false;  // alternate FS scan direction per row (kills diagonal smear)
    float diffusion = 1.0f;    // fraction of FS error propagated (1 = full grain, <1 = smoother)
    float brightness = 1.0f;   // multiply the source (1 = none)
    float contrast   = 1.0f;   // contrast around mid-grey (1 = none)
    float gamma      = 1.0f;   // mid-tone gamma applied before conversion (>1 brightens)
    // Perceptual chroma penalty ("colour noise" knob): low → vivid/noisy colour,
    // high → flat greys dither clean black/white instead of magenta confetti.
    float chromaWeight = 2.4f;
    // Optional source crop rectangle in source pixels, [x0,x1) × [y0,y1): only this
    // sub-region is resampled into the HGR page (its aspect drives fit/letterbox).
    // Degenerate (x1<=x0 or y1<=y0) → use the whole image.
    int cropX0 = 0, cropY0 = 0, cropX1 = 0, cropY1 = 0;
};

// Decode one HGR scanline (40 bytes) to 280 RGBA pixels through this module's
// NTSC pipeline. Exposed mainly so the test can pin it equal to GraphicsCard.
void hgrDecodeScanlineRgb(const uint8_t bytes[40], uint32_t out[280]);

// Convert a decoded RGBA image (srcW×srcH, 4 bytes/pixel, top-down) into an 8 KB
// HGR page (page-relative, $2000 layout). `outPage` must hold kHiresSize bytes.
void imageToHgrPage(const uint8_t* rgba, int srcW, int srcH,
                    const ImportOptions& opt, uint8_t* outPage);

// Decode a PNG/JPG/BMP file to RGBA (4 bytes/pixel). Implemented in
// HgrImageDecode.cpp (uses stb_image; the impl is linked from the host app), so
// the pure converter above stays unit-testable without an image decoder.
// Returns false + sets `err` on failure.
bool decodeImageFile(const std::string& path, int& w, int& h,
                     std::vector<uint8_t>& rgba, std::string& err);

} // namespace hgrpaint

#endif // HGRPAINT_CONVERT_H
