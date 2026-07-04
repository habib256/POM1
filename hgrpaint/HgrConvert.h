// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Image → Apple II HGR conversion, ii-pix style — the high-quality importer for
// the HGR Paint editor. Part of the portable hgrpaint/ toolkit (no emulator
// dependency): it decodes PNG/JPG (via stb_image, in a separate TU), resamples
// to 280×192 in LINEAR light (gamma-correct averaging), then dithers to the
// 8 KB HGR page by **analysis-by-synthesis**: for every byte it searches all
// 256 (7 pixels + palette) patterns (branch-and-bound over the pattern bits,
// warm-started from the previous row and pinned bit-identical to the exhaustive
// scan), renders each through this module's own copy of the Apple II NTSC
// artifact pipeline (the same maths as GraphicsCard — pinned equal in the
// test), measures the result against the target in **CAM16-UCS** perceptual
// space with an in-candidate sequential error walk (ii-pix apply_one_line), and
// keeps the closest. Error diffusion runs in clamped linear RGB (selectable
// Floyd-Steinberg / jarvis-mod kernel), and 1-2 ICM refinement passes re-choose
// every byte against its ACTUAL committed neighbour words, monotonically
// lowering the perceptual cost (fixes colour fringes at byte seams).
//
// This beats Buckshot/bmp2dhr because it dithers against the *true* NTSC colours
// (including the sliding-window pixel coupling) rather than a fixed palette.

#ifndef HGRPAINT_CONVERT_H
#define HGRPAINT_CONVERT_H

#include <cstdint>
#include <string>
#include <vector>

#include "ImportCommon.h"   // DitherKernel + shared linear-RGB resampler

namespace hgrpaint {

struct ImportOptions {
    bool  stretch = false;     // false = fit + letterbox (keep aspect), true = stretch
    bool  dither  = true;      // error diffusion (kernel selectable below)
    // DEPRECATED — ignored by the HGR path. The NTSC half-dot carry + sliding
    // window are physically left→right, so a right-to-left dither row would have
    // to score candidates against a fabricated left word; the cross-byte
    // refinement passes below kill the diagonal smear serpentine used to hide.
    bool  serpentine = false;
    float diffusion = 1.0f;    // fraction of error propagated (1 = full grain, <1 = smoother)
    float brightness = 1.0f;   // multiply the source (1 = none)
    float contrast   = 1.0f;   // contrast around mid-grey (1 = none)
    float gamma      = 1.0f;   // mid-tone gamma applied before conversion (>1 brightens)
    // Perceptual chroma penalty ("colour noise" knob): low → vivid/noisy colour,
    // high → flat greys dither clean black/white instead of magenta confetti.
    float chromaWeight = 2.4f;
    // Error-diffusion kernel. The editor defaults HGR to JarvisMod (ii-pix's HGR
    // choice: its 4-pixel forward reach matches the NTSC sliding window); the
    // library default stays Floyd-Steinberg so existing callers/tests keep their
    // pinned behaviour.
    DitherKernel kernel = DitherKernel::FloydSteinberg;
    // ICM-style cross-byte refinement passes after the dither pass: each byte is
    // re-chosen against the ACTUAL committed neighbour words (instead of pass 1's
    // guessed right-hand context), accepting only moves that strictly lower the
    // frozen-target perceptual cost — monotone, fixes colour fringes at byte seams.
    int refinePasses = 2;
    // Test/debug: disable the early-abort + warm-start candidate search. The fast
    // search is pinned bit-identical to this exhaustive one in hgr_convert_smoke.
    bool exhaustiveSearch = false;
    // Optional source crop rectangle in source pixels, [x0,x1) × [y0,y1): only this
    // sub-region is resampled into the HGR page (its aspect drives fit/letterbox).
    // Degenerate (x1<=x0 or y1<=y0) → use the whole image.
    int cropX0 = 0, cropY0 = 0, cropX1 = 0, cropY1 = 0;
};

// Optional conversion telemetry (pinned by the smoke test).
struct ImportStats {
    // Frozen-target perceptual objective after the dither pass ([0]) and after
    // each refinement pass — non-increasing by construction.
    std::vector<float> passCost;
    std::vector<int>   passChanged;   // bytes changed per refinement pass
    float maxErrAbs = 0.0f;           // max |linear-RGB diffusion error| seen (≤1: clamped)
};

// Decode one HGR scanline (40 bytes) to 280 RGBA pixels through this module's
// NTSC pipeline. Exposed mainly so the test can pin it equal to GraphicsCard.
void hgrDecodeScanlineRgb(const uint8_t bytes[40], uint32_t out[280]);

// Convert a decoded RGBA image (srcW×srcH, 4 bytes/pixel, top-down) into an 8 KB
// HGR page (page-relative, $2000 layout). `outPage` must hold kHiresSize bytes.
// `stats` (optional) receives refinement/diffusion telemetry.
void imageToHgrPage(const uint8_t* rgba, int srcW, int srcH,
                    const ImportOptions& opt, uint8_t* outPage,
                    ImportStats* stats = nullptr);

// Convert a decoded RGBA image into a 1 KB Apple II lo-res (GR) text page. GR has
// no NTSC coupling: each of kGrCols×kGrRows blocks is one flat palette index, so
// this quantises each block to the nearest of the 16 colours in CAM16-UCS with
// linear-RGB error diffusion (kernel/dither/chroma-weight knobs shared with the
// HGR path). `outPage` must hold at least 0x400 bytes (text-page $0400 layout).
void imageToGrPage(const uint8_t* rgba, int srcW, int srcH,
                   const ImportOptions& opt, uint8_t* outPage);

// Decode a PNG/JPG/BMP file to RGBA (4 bytes/pixel). Implemented in
// HgrImageDecode.cpp (uses stb_image; the impl is linked from the host app), so
// the pure converter above stays unit-testable without an image decoder.
// Returns false + sets `err` on failure.
bool decodeImageFile(const std::string& path, int& w, int& h,
                     std::vector<uint8_t>& rgba, std::string& err);

} // namespace hgrpaint

#endif // HGRPAINT_CONVERT_H
