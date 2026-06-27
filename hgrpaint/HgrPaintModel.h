// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR paint model — the emulator-agnostic bit-plotting logic behind the HGR
// Paint editor. Pure (no GL / ImGui / emulator), so it unit-tests standalone
// (hgr_paint_plot_smoke) and ports verbatim to POM2.
//
// The HGR colour model is faithful: 7 pixels per byte share one high bit
// (palette select), so the six artifact colours obey column parity and the
// per-byte high-bit constraint. The Apple II non-linear row interleave + the
// 280×192 / 8 KB constants live here too — they are Apple II hardware facts,
// identical on any host, so the module carries its own copy rather than
// reaching into a host renderer for them.

#ifndef HGRPAINT_MODEL_H
#define HGRPAINT_MODEL_H

#include <cstdint>
#include <functional>

namespace hgrpaint {

// Apple II HIRES geometry (host-independent).
constexpr int      kHiresWidth  = 280;
constexpr int      kHiresHeight = 192;
constexpr int      kHiresSize   = 0x2000;   // 8 KB page
constexpr uint16_t kHiresBase   = 0x2000;   // page 1 ($2000); page 2 = $4000

// Address of the first byte of HIRES scanline `y` (Apple II non-linear
// interleave), page 1 ($2000). Page 2 differs only by base, so the model works
// page-relative and adds the base at the call site.
uint16_t hgrRowAddress(int y);

// The six Apple II HGR artifact colours. Black/White are palette-agnostic;
// the four chromatic colours split by per-byte high bit (Violet/Green =
// palette 0, Blue/Orange = palette 1) and by column parity.
enum class HgrColor : uint8_t { Black = 0, White, Violet, Green, Blue, Orange };

// A function that renders an 8 KB HGR page (page-relative bytes) to a
// kHiresWidth×kHiresHeight RGBA buffer through the host's real NTSC pipeline —
// the same renderer the host screen uses. Used by fillRegion so flood
// connectivity matches what the eye sees. (POM1 wraps GraphicsCard.)
using RenderPageFn = std::function<void(const uint8_t* page8k, uint32_t* outRgba)>;

// Page-relative byte offset (0..0x1FFF) of logical pixel (x,y), x in 0..279,
// y in 0..191. Page-independent (page 1 and 2 share the layout, only the base
// differs).
int hgrByteOffset(int x, int y);

// Bit within the byte for column x (0 = leftmost pixel of the 7-pixel group).
int hgrBit(int x);

// Snap column x to a parity valid for colour c (chromatic colours only live
// on one parity of columns). Black/White are returned unchanged.
int snapColumn(int x, HgrColor c);

// Final page byte offset a plot of colour c at (x,y) would touch (after column
// snapping), or -1 if (x,y) is out of range.
int targetOffset(int x, int y, HgrColor c);

// Apply colour c at logical pixel (x,y) to an 8 KB page buffer. Returns the
// changed byte offset, or -1 if nothing changed / out of range. Setting a
// chromatic colour also flips the byte's shared high bit — the real NTSC
// behaviour that recolours the other pixels in the same byte.
int plotPage(uint8_t* page, int x, int y, HgrColor c);

// True if the logical pixel (x,y) is lit in the given 8 KB page buffer.
bool pixelOn(const uint8_t* page, int x, int y);

// Classify the artifact colour visible at logical pixel (x,y): Black if off,
// White if its same-byte horizontal neighbours fill it in, else the chromatic
// colour implied by the byte's high bit (palette) and the column parity.
HgrColor colorAt(const uint8_t* page, int x, int y);

// True if byte column `byteCol` (0..38) at row y starts a "palette seam": it and
// its right neighbour both have lit pixels but disagree on the shared high bit,
// the boundary where NTSC artifact-colour bleed occurs.
bool byteHasPaletteSeam(const uint8_t* page, int byteCol, int y);

// Flip only the shared high bit (palette) of byte column `byteCol` (0..39) at
// row y — recolours its 7 pixels without touching any pixel bit. msb: 0=clear,
// 1=set, else toggle. Returns the changed page offset, or -1 if unchanged / out
// of range.
int setBytePalette(uint8_t* page, int byteCol, int y, int msb);

// Flood-fill the connected region of equal *perceived* artifact colour at (x,y)
// and recolour it to c, mutating the 8 KB page in place. Returns the number of
// pixels in the filled region (0 if out of range). `render` paints the current
// page so connectivity follows what the eye SEES, not the raw pixel on/off bits:
// an HGR colour area is bit-dithered (a solid violet field is the byte pattern
// $55, so its odd columns are *off*), so a naive raw-bit flood leaks straight
// through every chromatic region via those off sub-pixels. Recolour = clear the
// whole region first, then stamp c's parity pattern, so an old colour's bits
// can't combine with the new ones into white (green $2A | violet $55 = $7F).
int fillRegion(uint8_t* page, int x, int y, HgrColor c, const RenderPageFn& render);

// ── Apple II lo-res (GR) block model ─────────────────────────────────────────
// GR is 40 columns × 48 block-rows of 16-colour blocks stored in the TEXT page
// ($0400 page 1 / $0800 page 2). Each text byte holds TWO vertically-stacked
// blocks: low nibble = upper block (even block-row), high nibble = lower block
// (odd block-row). Rows use the same DRAM-refresh interleave as text, NOT the
// HIRES one. The editor maps the 280×192 canvas to blocks (bx = px/7, by = py/4)
// so GR shares the canvas with HGR. Page-relative (base added at the call site).
constexpr int kGrCols = 40;
constexpr int kGrRows = 48;

// Page-relative byte offset of block (bx,by), or -1 if out of range.
int grBlockOffset(int bx, int by);

// Set block (bx,by) to colourIndex (0..15) in a lo-res page buffer. Returns the
// changed byte offset, or -1 if unchanged / out of range. Only the block's own
// nibble is touched; the other block sharing the byte is preserved.
int plotGrBlock(uint8_t* page, int bx, int by, int colorIndex);

// Colour index (0..15) of block (bx,by), or -1 if out of range.
int grBlockColorAt(const uint8_t* page, int bx, int by);

} // namespace hgrpaint

#endif // HGRPAINT_MODEL_H
