// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 sprite model — the emulator-agnostic VRAM logic behind the TMS9918
// Sprite editor. Pure (no GL / ImGui / emulator), so it unit-tests standalone
// (tms_sprite_plot_smoke) and ports verbatim to POM2. Sibling of
// tmspaint/TmsPaintModel, but for the sprite plane rather than the playfield.
//
// TMS9918 sprites are MONOCHROME bitmaps (one palette colour per sprite, taken
// from the SAT, index 0 = transparent). Two geometries:
//
//   8×8   — one 8-byte pattern per sprite (pattern # 0..255), 8 bytes at
//           patternBase + n*8 (row y → byte y, column x → bit 0x80>>x).
//   16×16 — a "sprite" is FOUR consecutive 8-byte patterns (32 bytes) whose
//           base number is a multiple of 4 (the chip masks pattern # with 0xFC).
//           The four quadrants are ordered top-left, bottom-left, top-right,
//           bottom-right — matching TMS9918::renderSpritesLineRaw exactly:
//             quadrant(x,y) = (x>=8 ? 2 : 0) + (y>=8 ? 1 : 0)
//             byte = base + (patNum + quadrant)*8 + (y & 7)
//
// The Sprite Attribute Table (SAT, 32 entries × 4 bytes = Y, X, pattern#,
// colour/EarlyClock) and a blank Graphics-I backdrop let the editor place the
// sprite being drawn on the live chip so what you paint is what the card shows.
// All addressing here is pinned against the real per-line renderer by
// tms_sprite_plot_smoke.

#ifndef TMSSPRITE_MODEL_H
#define TMSSPRITE_MODEL_H

#include <cstdint>

namespace tmssprite {

// 16 KB VRAM (== TMS9918::kVramSize). Addresses below are VRAM-relative and
// match the canonical layout the editor programs onto the chip.
constexpr int      kVramSize          = 0x4000;
constexpr uint16_t kSpritePatternBase = 0x3800;   // sprite pattern generator
constexpr uint16_t kSpriteAttrBase    = 0x1B00;   // SAT (32 × 4 bytes)
constexpr uint16_t kNameBase          = 0x1800;   // backdrop name table (768 B)
constexpr uint16_t kGfx1PatternBase   = 0x0000;   // backdrop pattern generator
constexpr uint16_t kGfx1ColorBase     = 0x2000;   // backdrop colour table

constexpr int kMaxSprites   = 32;   // SAT entries
constexpr int kPatternSlots = 256;  // 8-byte pattern slots
constexpr int kSatTerminator = 0xD0; // Y value that ends the visible sprite list

// Sprite geometry. 8×8 uses one pattern slot; 16×16 uses four.
enum class Size : uint8_t { S8 = 0, S16 };

int dim(Size s);                 // side length in pixels (8 or 16)
int slotsPerSprite(Size s);      // pattern slots consumed (1 or 4)

// VRAM address of the pattern byte holding sprite pixel (x,y) of the sprite
// whose base pattern number is `patNum`, or -1 if (x,y) is out of range for the
// geometry. The pixel's bit within that byte is bitMask(x). For 16×16 `patNum`
// is masked to a multiple of 4 (the chip's behaviour).
int patternByteAddr(int patNum, Size s, int x, int y);
uint8_t bitMask(int x);          // 0x80 = leftmost pixel of the 8-pixel byte

// Read / set one monochrome sprite pixel. setPixel returns true if VRAM changed.
bool getPixel(const uint8_t* vram, int patNum, Size s, int x, int y);
bool setPixel(uint8_t* vram, int patNum, Size s, int x, int y, bool on);

// SAT entry write helpers (idx 0..31). writeSatEntry stores Y,X,pattern#,colour
// (colour low nibble = palette index, bit7 = Early Clock). terminateSat writes
// the 0xD0 Y that hides this entry and every later one.
void writeSatEntry(uint8_t* vram, int idx, uint8_t y, uint8_t x,
                   uint8_t pat, uint8_t colourEc);
void terminateSat(uint8_t* vram, int idx);

// ── Live-preview backdrop ───────────────────────────────────────────────────
// Fill `regs[8]` with a Graphics-I register set (blank playfield, sprite engine
// live) honouring the sprite geometry + magnification. `backdrop` is the border/
// background palette index (0..15). The editor writes these to the chip so the
// sprite it is drawing actually displays.
void canonicalRegisters(uint8_t regs[8], Size s, bool magnified, int backdrop);

// Clear the backdrop tables (name table, pattern slot 0, colour table) in `vram`
// so the Graphics-I playfield renders as a flat `backdrop` field — leaving the
// SAT and sprite pattern table (the editor's drawing surface) untouched.
void writeCanonicalBackdrop(uint8_t* vram, int backdrop);

} // namespace tmssprite

#endif // TMSSPRITE_MODEL_H
