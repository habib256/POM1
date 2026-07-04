// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 paint model — the emulator-agnostic VRAM-plotting logic behind the
// (forthcoming) TMS9918 Paint editor. Pure (no GL / ImGui / emulator), so it
// unit-tests standalone (tms_paint_plot_smoke) and ports verbatim to POM2.
// Mirrors hgrpaint/HgrPaintModel for the P-LAB TMS9918 Graphic Card.
//
// Two canvas modes are supported, each with a FIXED canonical VRAM layout the
// editor programs onto the chip (registers + name table). Pixel addressing then
// reads/writes the pattern (and, for Graphics II, the colour) table directly:
//
//   Graphics II (bitmap)  256×192, 2 colours per 8×1 cell ("colour clash").
//     name $1800 (identity, name[i]=i&0xFF), pattern $0000, colour $2000.
//     A pixel is a pattern-table bit; its 8-pixel row shares one (fg,bg) pair
//     from the colour table — the central constraint this model encapsulates.
//
//   Multicolor            64×48 blocks of 4×4 px, any of 15 colours, NO clash.
//     name $1800 (per-section, name[r*32+c]=(r/4)*32+c), pattern $0000.
//     Each 4×4 block is one free nibble in the pattern table.
//
// The VRAM addressing here is derived from — and pinned against — TMS9918's real
// per-line renderers (renderGfxIILineRaw / renderMulticolorLineRaw): the test
// installs these canonical tables, plots through this model, renders the chip,
// and asserts the displayed colour matches. The layout constants are TMS9918
// hardware facts, identical on any host, so the module carries its own copy
// rather than reaching into a host renderer for them.

#ifndef TMSPAINT_MODEL_H
#define TMSPAINT_MODEL_H

#include <cstdint>

namespace tmspaint {

// 16 KB VRAM (== TMS9918::kVramSize). Addresses below are VRAM-relative.
constexpr int      kVramSize = 0x4000;

// Canonical table bases the editor programs (shared by both modes where noted).
constexpr uint16_t kNameBase          = 0x1800;   // 768-byte name table
constexpr uint16_t kPatternBase       = 0x0000;   // pattern generator table
constexpr uint16_t kGfx2ColorBase     = 0x2000;   // Graphics II colour table
constexpr uint16_t kSpriteAttrBase    = 0x1B00;   // SAT (kept clear by editor)
constexpr uint16_t kSpritePatternBase = 0x3800;   // sprite patterns

// Canvas geometry per mode.
constexpr int kGfx2Width  = 256;
constexpr int kGfx2Height = 192;
constexpr int kMcWidth    = 64;
constexpr int kMcHeight   = 48;

enum class Mode : uint8_t { GraphicsII = 0, Multicolor };

// Logical canvas size for a mode.
int width(Mode m);
int height(Mode m);

// Fill `regs[8]` with the canonical register set that puts a live TMS9918 into
// `m` with the table layout above (R0..R7). Backdrop (R7) = palette index 1
// (black). The host writes these to the chip when the editor selects a mode.
void canonicalRegisters(Mode m, uint8_t regs[8]);

// Write the canonical name table for `m` into `vram` (16 KB) at kNameBase. The
// pattern/colour tables are left untouched (the drawing surface). Graphics II
// uses an identity table; Multicolor a per-section table so all 64×48 blocks
// address distinct pattern nibbles.
void writeCanonicalNameTable(uint8_t* vram, Mode m);

// ── Graphics II addressing ─────────────────────────────────────────────────
// VRAM address of the pattern byte holding pixel (x,y), x∈0..255, y∈0..191, or
// -1 if out of range. The pixel's bit within that byte is gfx2BitMask(x).
int gfx2PatternAddr(int x, int y);
// VRAM address of the colour byte governing pixel (x,y)'s 8×1 cell (= pattern
// addr + colour table base), or -1 if out of range. Byte = (fg<<4)|bg.
int gfx2ColorAddr(int x, int y);
// Bit mask within the pattern/colour byte for column x (0x80 = leftmost pixel).
uint8_t gfx2BitMask(int x);

// ── Multicolor addressing ──────────────────────────────────────────────────
// VRAM address of the pattern byte holding block (bx,by), bx∈0..63, by∈0..47,
// or -1 if out of range. mcHighNibble(bx) selects which nibble is the colour.
int mcBlockAddr(int bx, int by);
// True if block bx uses the high nibble (left 4×4) of its byte, false = low.
bool mcHighNibble(int bx);

// ── Plot / read ────────────────────────────────────────────────────────────
// Set the pixel/block at (x,y) to palette index `color` (0..15). For Multicolor
// this just writes the nibble. For Graphics II it honours the 2-colours-per-cell
// constraint: if `color` already equals the cell's fg or bg, only the pattern
// bit moves; otherwise the least-used colour slot of the cell is reassigned to
// `color` (recolouring the few pixels that used it) — the standard TMS paint
// "colour clash" behaviour. Returns true if any VRAM byte changed.
bool plotPixel(uint8_t* vram, Mode m, int x, int y, int color);

// Palette index visible at (x,y), or -1 if out of range.
int colorAt(const uint8_t* vram, Mode m, int x, int y);

// Flood-fill the 4-connected region of equal colour at (x,y) and recolour it to
// `color`, mutating VRAM in place. Returns the number of pixels/blocks filled
// (0 if out of range). Unlike HGR there are no artifact colours, so connectivity
// follows colorAt() directly.
int fillRegion(uint8_t* vram, Mode m, int x, int y, int color);

} // namespace tmspaint

#endif // TMSPAINT_MODEL_H
