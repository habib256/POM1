# lib/gen2 — Uncle Bernie's GEN2 release card (equates, beam-sync, HGR tables)

*[← POM1 documentation index](../../../doc/README.md)*

Assembly support for the GEN2 *release* Color Graphics Card: the soft-switch /
hardware equates, the HST0 beam-synchronisation engine, and the HGR data tables
plus sub-byte rendering primitives. (Folded in from the former `lib/hgr` — HGR
*is* the GEN2 card, so its tables live here alongside the rest of the GEN2
support.) The C runtime for the same card is in [`../gen2c/`](../gen2c/).

## Release equates & sync

- **`gen2.inc`** — GEN2 release soft-switch / hardware equates. Hardware ref:
  `doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf`, transcribed in
  `doc/GEN2_RELEASE_questions.md`; developer guide `doc/GEN2_RELEASE.md`.
- **`gen2_sync.asm`** — HST0 beam synchronisation (`gen2_waitvbl` coarse V-blank
  sync, `gen2_beam_lock`), extracted from the validation demo
  `sketchs/gen2/demo_a1_crazycycle/`.
- **`gen2_init.asm`** — `gen2_hgr_init` / `gen2_lores_init`: read all four GEN2
  soft-switch pairs into a known state (Bernie Q8 — latch survives RESET). Call
  before any HGR/LORES drawing in asm; C links the same routines via `gen2_blit.s`.

## HGR data tables + sub-byte rendering

Lookup tables for the GEN2 HGR framebuffer (passive RAM-mapped at `$2000-$3FFF`,
280×192 NTSC artifact-color; Apple-II non-linear scanline addressing).

- **`hgr_tables.inc`** — umbrella bundle: `.include`s `hgr_plot.asm`,
  `hgr_clear.asm`, `hgr_scanline.inc`, and `hgr_plot_tables.inc` in the original
  order so legacy consumers stay byte-for-byte unchanged. Needs ZP `cur_x`,
  `cur_y`, `ptr_lo`, `ptr_hi`. Pull in a single module directly to adopt only
  part of it.
- **`hgr_scanline.inc`** — row-address LUTs `hgr_lo[192]` / `hgr_hi[192]`
  mapping scanline `Y (0..191)` → base address inside `$2000-$3FFF`. Apple-II
  compatible non-linear ordering. (Split out of `hgr_tables.inc`.)
- **`hgr_plot_tables.inc`** — `hgr_col` / `hgr_mask` x-lookup tables for
  `plot_pixel`.
- **`hgr_plot.asm`** — `plot_pixel` (needs `cur_x`/`cur_y`/`ptr_*` + both table
  sets). **`hgr_clear.asm`** — `clear_hgr` (needs ZP `ptr_lo`/`ptr_hi`).
- **`smiley.inc`** — sample 16×16 smiley sprite (legacy, no current consumer).
- **`bbfont_cp437.inc`** — Beautiful Boot 8×8 font, full CP437 (256 glyphs,
  2 KB). Encoding: `AND #$7F`, 8 rows top→bottom, bit 0 = left. Source:
  Michael Pohoreski's `apple2_hgr_font_tutorial`. Label: `HGR_BBFont`,
  constants `HGR_BBFONT_BYTES_PER_GLYPH` / `HGR_BBFONT_GLYPH_COUNT`.
  Used by `sketchs/gen2/demo_hgr_bbfont_show/`. **This is the single font
  master** (Axe 2 of the lib factoring): `tools/build_shared_font.py` emits the
  HGR C slice (`dev/lib/gen2c/gen2_bbfont.inc`, bit 0 = left) *and* the TMS9918
  pattern tables (`dev/lib/tms9918/bbfont_tms.inc` + `font_hud8x8.inc`,
  bit 7 = left, the bit-reverse) from it — edit here, re-run the tool.
- **`bbfont_subset.inc`** — 38-glyph subset of the same font, hand-curated
  for the HGR Sokoban HUD/title (digits, "MOVES:", "PUSHES:", "SOKOBAN",
  letters). Label: `HGR_Sokoban_bbfont`. Used by
  `sketchs/gen2/game_sokoban/`. Drop-in compatible with `bbfont_cp437.inc`
  glyph layout.
- **`subbyte4.inc`** — 7-phase sub-byte mask LUTs for **4-pixel-wide**
  blocks. Three tables (`sb4_byte_off`, `sb4_mask1`, `sb4_mask2`) cover
  grid columns `gx ∈ [0..68]`, encoding the byte offset and OR-mask
  pair for each phase. Lifted from `game_maze`. To support a different
  block width, copy this file as a template and recompute the masks
  (the structure is identical).
- **`subbyte_fill.asm`** — `subbyte_fill_4`: read-modify-write OR a
  4-pixel × 4-row block at `(gx, scanline_ptr)`. Uses the LUTs from
  `subbyte4.inc`.
- **`sprites/`** — HGR sprite data (`.asm` + `.inc` per category), mirrored from
  the TMS9918 sprite sources by `tools/build_hgr_sprites.py`.
- **`fonts/`** — font source slices (`font7x8.s`, `fontbb.s`). The reference PNG
  sheets live in `dev/assets/`.

## Sub-byte rendering quick reference

HGR's 7 px/byte layout means non-7-aligned sprite widths must straddle
byte boundaries. The 7-phase pattern for **4-pixel** blocks is:

| `gx%7` | byte+ | mask1 | mask2 |
|---|---|---|---|
| 0 | +0 | $0F | $00 |
| 1 | +0 | $70 | $01 |
| 2 | +1 | $1E | $00 |
| 3 | +1 | $60 | $03 |
| 4 | +2 | $3C | $00 |
| 5 | +2 | $40 | $07 |
| 6 | +3 | $78 | $00 |

Bit 7 of every byte stays clear (NTSC group selector, not a pixel) —
colour-aware rendering must set bit 7 separately on each scanline byte.

## subbyte_fill — public routine

| Routine | Inputs | Output | Clobbers | ZP |
|---|---|---|---|---|
| `subbyte_fill_4` | X = gx (0..68), `sb_ptr_lo:hi` = first scanline | OR a 4×4 block | A, X, Y | `tmp`, `tmp2`, `sb_ptr_lo/hi` |

Constraint: the 4 scanlines must lie in the **same HGR group** (Apple-II
non-linear layout — within a group, consecutive scanlines are at
`+$0400`). For blocks crossing group boundaries, do a full
`hgr_lo/hi` lookup per row.

## Use

```asm
.include "apple1.inc"
.include "zp.inc"            ; provides tmp / tmp2
.include "hgr_scanline.inc"  ; hgr_lo / hgr_hi scanline LUTs
.include "subbyte4.inc"      ; 7-phase mask data
.include "subbyte_fill.asm"  ; subbyte_fill_4 routine
```

In your project Makefile (sketch under `sketchs/gen2/<name>/`, or multi-file
project under `dev/projects/<card>/<name>/`):

    LIB := -I ../../../lib/apple1 -I ../../../lib/gen2
    LOAD_CFG := ../../../cc65/apple1_gen2.cfg
