// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR sprite blit — the pure byte-level placement logic behind the GEN2 HGR
// Sprite editor. The Apple II has NO hardware sprites: a "sprite" is just a
// rectangle of HIRES bytes (wBytes × hRows, byte-aligned horizontally) a program
// copies/XORs onto the screen. This module extracts such a rectangle from — and
// stamps it back into — an 8 KB HGR page, honouring the Apple II non-linear row
// interleave (via hgrpaint::hgrByteOffset). No GL/ImGui/emulator dependency, so
// it unit-tests standalone (hgr_sprite_blit_smoke) and ports verbatim to POM2.
//
// The per-pixel drawing model (7 px/byte + shared palette high bit + artifact
// colour) is the already-pinned hgrpaint::HgrPaintModel — this module only moves
// whole bytes around, so it is the one piece the sprite editor adds on top.

#ifndef HGRSPRITE_BLIT_H
#define HGRSPRITE_BLIT_H

#include <cstdint>
#include <functional>

namespace hgrsprite {

// Byte columns / rows of an Apple II HIRES page (280 px = 40 bytes × 7, 192 rows).
constexpr int kByteCols = 40;
constexpr int kRows     = 192;

// Extract a wBytes×hRows sprite whose top-left byte column is srcByteCol, top row
// srcRow, from an 8 KB page (page-relative bytes) into row-major `out`
// (hRows*wBytes bytes). Cells whose page byte is out of range read as 0.
void extract(const uint8_t* page, int srcByteCol, int srcRow,
             int wBytes, int hRows, uint8_t* out);

// Stamp a row-major wBytes×hRows sprite into a page at (dstByteCol,dstRow). Each
// in-range cell calls poke(pageRelativeOffset, value) — so a caller can route it
// to pokeByte(base+off) for the live card, or to a local buffer. Cells that fall
// off the page (column ≥ 40 or row ≥ 192) are silently skipped.
void stamp(const uint8_t* sprite, int wBytes, int hRows,
           int dstByteCol, int dstRow,
           const std::function<void(int, uint8_t)>& poke);

} // namespace hgrsprite

#endif // HGRSPRITE_BLIT_H
