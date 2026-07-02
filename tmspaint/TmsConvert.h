// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Image → TMS9918 conversion, ii-pix style — the importer for the TMS9918 Paint
// editor. Part of the portable tmspaint/ toolkit (no emulator dependency): it
// decodes PNG/JPG (via stb_image, separate TU), resamples to the mode canvas, and
// dithers to a 16 KB VRAM image scored in CAM16-UCS perceptual space.
//
// Unlike HGR there are no NTSC artifact colours — the TMS palette is 15 fixed
// RGB entries — so the search is direct:
//   Graphics II  per 8×1 cell, pick the 2 palette colours (fg,bg) that minimise
//                the summed perceptual cost of assigning each of the 8 pixels to
//                its nearer of the two, with Floyd-Steinberg error diffusion.
//   Multicolor   per 4×4 block, pick the nearest palette colour (with FS dither).
// The output is a full 16 KB VRAM (canonical name table + pattern / colour
// tables) so the editor commits it with one diff against its shadow.

#ifndef TMSPAINT_CONVERT_H
#define TMSPAINT_CONVERT_H

#include <cstdint>
#include <string>
#include <vector>

#include "ImportCommon.h"    // hgrpaint::DitherKernel + shared linear-RGB resampler
#include "TmsPaintModel.h"   // Mode + geometry

namespace tmspaint {

struct ImportOptions {
    bool  stretch = false;     // false = fit + letterbox (keep aspect), true = stretch
    bool  dither  = true;      // error diffusion (kernel selectable below)
    bool  serpentine = false;  // alternate dither scan direction per row
    float diffusion = 1.0f;    // fraction of error propagated (1 = full grain)
    float brightness = 1.0f;   // multiply the source (1 = none)
    float contrast   = 1.0f;   // contrast around mid-grey (1 = none)
    float gamma      = 1.0f;   // mid-tone gamma (>1 brightens)
    float chromaWeight = 2.4f; // perceptual chroma penalty: low → vivid, high → clean greys
    // Error-diffusion kernel (Floyd-Steinberg default; jarvis-mod available for a
    // smoother, forward-weighted grain — see hgrpaint/ImportCommon.h).
    hgrpaint::DitherKernel kernel = hgrpaint::DitherKernel::FloydSteinberg;
    // Test/debug: disable the early-abort + warm-start pair search. The fast
    // search is pinned bit-identical to this exhaustive one in tms_convert_smoke.
    bool exhaustiveSearch = false;
    // Optional source crop rectangle in source pixels, [x0,x1) × [y0,y1).
    // Degenerate (x1<=x0 or y1<=y0) → use the whole image.
    int cropX0 = 0, cropY0 = 0, cropX1 = 0, cropY1 = 0;
};

// Convert a decoded RGBA image (srcW×srcH, 4 bytes/pixel, top-down) into a 16 KB
// TMS9918 VRAM image for `mode`. `outVram` must hold kVramSize bytes; it receives
// the canonical name table plus the dithered pattern (and, in Graphics II, colour)
// tables. Letterbox / empty cells stay 0 (transparent → backdrop).
void imageToTmsVram(const uint8_t* rgba, int srcW, int srcH,
                    Mode mode, const ImportOptions& opt, uint8_t* outVram);

// Decode a PNG/JPG/BMP file to RGBA (4 bytes/pixel). Implemented in
// TmsImageDecode.cpp (uses stb_image; impl linked from the host app). Returns
// false + sets `err` on failure.
bool decodeImageFile(const std::string& path, int& w, int& h,
                     std::vector<uint8_t>& rgba, std::string& err);

} // namespace tmspaint

#endif // TMSPAINT_CONVERT_H
