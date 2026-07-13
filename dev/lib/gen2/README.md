# lib/gen2 — Uncle Bernie's GEN2 release card (equates, beam-sync, HGR tables)

*[← dev/lib index](../README.md)*

Assembly support for the GEN2 *release* Color Graphics Card: the soft-switch /
hardware equates, the HST0 beam-synchronisation engine, and the HGR data tables
plus sub-byte rendering primitives. (Folded in from the former `lib/hgr` — HGR
*is* the GEN2 card, so its tables live here alongside the rest of the GEN2
support.)

Siblings: the C runtime for the same card is in [`../gen2c/`](../gen2c/); the
card-neutral 2D layer it shares with TMS9918 is [`../gfx/`](../gfx/); HGR sprite
data is in [`sprites/`](sprites/). **Tutorial:** [step-by-step GEN2 HGR assembly guide](../../../sketchs/doc/Programming_GEN2.md).

## Release equates & sync

- **`gen2.inc`** — GEN2 release soft-switch / hardware equates (canonical
  card-hardware spec + soft-switch reference is transcribed in the POM1
  documentation set; the equates here are the single source of truth for asm).
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
- **`smiley.inc`** — sample 28×28 HGR emoji sheet (`hgr_smiley28_font`,
  6 glyphs × 112 B; legacy, no current consumer).
- **`bbfont_cp437.inc`** — Beautiful Boot 8×8 font, full CP437 (256 glyphs,
  2 KB). Encoding: `AND #$7F`, 8 rows top→bottom, bit 0 = left. Source:
  Michael Pohoreski's `apple2_hgr_font_tutorial`. Label: `HGR_BBFont`,
  constants `HGR_BBFONT_BYTES_PER_GLYPH` / `HGR_BBFONT_GLYPH_COUNT`.
  Used by `sketchs/gen2/demo_hgr_bbfont_show/`. **This is the single font
  master** (Axe 2 of the lib factoring): `tools/build_shared_font.py` emits the
  HGR C slice (`dev/lib/gen2c/gen2_bbfont.inc`, bit 0 = left) *and* the TMS9918
  pattern tables (`dev/lib/tms9918/bbfont_tms.inc` + `font_hud8x8.inc`,
  bit 7 = left, the bit-reverse) from it — edit here, re-run the tool.
- **`bbfont_subset.inc`** — 39-glyph subset of the same font, hand-curated
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
- **`hgr_sprite16.asm`** — `hgr_spr16_x1/_x2/_x4` + `hgr_spr16_color_a`:
  blit a **TMS9918-format 16×16 pattern** (the SCROLL-O-SPRITES layout of
  `dev/lib/tms9918/sprites_*.asm`) into HGR at ×1/×2/×4 with optional NTSC
  artifact colour (`HSPR_WHITE/GREEN/ORANGE/VIOLET/BLUE`). Each output row
  is built as a pixel **bit-stream** then repacked 7 px/HGR-byte
  (`sp_pack_row`) — **lossless**: a naive 8-px-byte → 7-px-byte column
  mapping drops one column per source byte and visibly skews ×2 art
  (proven on Maze3D's goblin — byte-column mapping is fine for 8-px-grid
  text/lines, never for sprite art). Owns its ZP (`sp_ptr/sp_x/sp_y` +
  colour attrs + scratch) and `dblnib`/`quadbits` (+ the shared `rev7_tab`
  via `rev7.inc`); needs `hgr_scanline.inc`. First consumer:
  `sketchs/gen2/game_maze3d/`.
- **`hgr_text8.asm`** — `hgr_putc8` / `hgr_puts8`: byte-aligned 8×8 text with
  a VDP-style cursor (advance + wrap `ht_wrap`→`ht_left`, next text row) —
  what lets TMS-ported print loops run unchanged. Font is caller-provided
  (`ht_font_lo/hi`, 8 B/glyph from `$20`), in either bit order (`ht_rev`:
  0 = HGR like `bbfont_ascii5f.inc`, 1 = TMS via `rev7_tab`). `hgr_putc8`
  preserves A/X/Y. Replaces the private emitters of `HGR_Rogue`
  (`hgr_emit_a`) and `HGR_Maze3D` (`write_char`); `GEN2_Chess`'s `putc_hgr`
  is the remaining migration candidate. Micro-test: `test/micro/t15`.
- **`hgr_blit2.asm`** — `hgr_blit2` / `hgr_blit4`: byte-aligned 2-/4-byte-wide
  rectangle blits of HGR-packed rows with a raster op (`bl_mode`: OR /
  inverted-box FLASH / STORE) — the cell/tile + soft-entity workhorse of the
  TMS→GEN2 ports. Never touches X (pool loops keep offsets there). First
  consumer: `sketchs/gen2/game_rogue/` (tiles, entities, 28×32 boss).
  Migration candidate: `game_sokoban`'s `draw_tile`. Micro-test:
  `test/micro/t15`.
- **`rev7.inc`** — `rev7_tab[256]`: TMS bit order → HGR bit order (input
  bit 0, the TMS rightmost pixel, is dropped — fine for 8-px-grid glyphs,
  NOT for sprite art; that's what `hgr_sprite16`'s bit-stream repack is
  for). Shared, include-guarded, pulled by both modules above.
- **`sprites/`** — HGR sprite data (`.asm` + `.inc` per category), mirrored from
  the TMS9918 sprite sources by `tools/build_hgr_sprites.py`. See
  [`sprites/README.md`](sprites/).
- **`fonts/`** — font source slices (`font7x8.s`, `fontbb.s`). The reference PNG
  sheets live in `dev/assets/`.

## Exported symbols quick-reference

Scannable list of the labels / equates / constants each include actually
provides (real names — `.import` them or `.include` the file, then `JSR`).

| Provider | Symbols |
|---|---|
| `gen2.inc` | switches `GEN2_TEXTOFF/TEXTON/MIXOFF/MIXON/PAGE1/PAGE2/LORES/HIRES` (`$C250-$C257`); framebuffers `GEN2_TEXT1/TEXT2` (`$0400/$0800`), `GEN2_HGR1/HGR2` (`$2000/$4000`); speaker `GEN2_SPEAKER` (`$C030`); timing `GEN2_CYC_PER_LINE`, `GEN2_LINES_60HZ/50HZ`, `GEN2_CYC_FRAME_60HZ/50HZ`, `GEN2_VISIBLE_LINES` |
| `gen2_init.asm` | `gen2_hgr_init`, `gen2_lores_init` (C aliases `_gen2_hgr_init` / `_gen2_lores_init`) |
| `gen2_sync.asm` | `gen2_waitvbl` (coarse VBL), `gen2_beam_lock` (cycle-exact); config equates `GEN2_POLL` (default `GEN2_PAGE1`), `GEN2_ZP3`, `GEN2_SYNC_SHIM` |
| `hgr_scanline.inc` | tables `hgr_lo[192]`, `hgr_hi[192]` (scanline `Y` → base) |
| `hgr_plot_tables.inc` | tables `hgr_col[256]` (`x/7`), `hgr_mask[256]` (`1<<x%7`) |
| `hgr_plot.asm` | `plot_pixel` (needs ZP `cur_x/cur_y/ptr_lo/ptr_hi` + both table sets) |
| `hgr_clear.asm` | `clear_hgr` (zeroes `$2000-$3FFF`; needs ZP `ptr_lo/ptr_hi`) |
| `hgr_tables.inc` | umbrella — re-exports all of the four above |
| `hgr_sprite16.asm` | `hgr_spr16_x1/_x2/_x4`, `hgr_spr16_color_a`; equates `HSPR_WHITE/GREEN/ORANGE/VIOLET/BLUE`; tables `dblnib[16]`, `quadbits[4]` (+ `rev7_tab` via `rev7.inc`); public ZP `sp_ptr/sp_x/sp_y/sp_cm_ev/sp_cm_od/sp_cbit` (needs `hgr_scanline.inc`) |
| `hgr_text8.asm` | `hgr_putc8` (preserves A/X/Y), `hgr_puts8`; public ZP `ht_col/ht_sl/ht_left/ht_wrap/ht_font_lo/ht_font_hi/ht_rev/ht_src_lo/ht_src_hi` + colour attrs `ht_cm_ev/ht_cm_od/ht_cbit` (white $7F/$7F/$00 = pass-through) + page selector `ht_page` ($00/$60) — init all at boot (needs `hgr_scanline.inc`; pulls `rev7.inc`) |
| `hgr_blit2.asm` | `hgr_blit2`, `hgr_blit4` (modes OR/FLASH/STORE/PALFLIP; never touch X); public ZP `bl_src/bl_col/bl_sl/bl_h/bl_mode/bl_page` (page selector $00/$60, EORed into the scanline hi byte — init at boot; needs `hgr_scanline.inc`) |
| `rev7.inc` | `rev7_tab[256]` (TMS bit 7-leftmost → HGR bit 0-leftmost, rightmost source pixel dropped; include-guarded) |
| `subbyte4.inc` | tables `sb4_byte_off`, `sb4_mask1`, `sb4_mask2` (7-phase, 4-px blocks) |
| `subbyte_fill.asm` | `subbyte_fill_4` (ZP `sb_ptr_lo/sb_ptr_hi`, `tmp`, `tmp2`) |
| `bbfont_cp437.inc` | `HGR_BBFont` + `HGR_BBFONT_BYTES_PER_GLYPH` (8) / `HGR_BBFONT_GLYPH_COUNT` (256) |
| `bbfont_subset.inc` | `HGR_Sokoban_bbfont` + `BBFONT_SOKOBAN_BYTES_PER_GLYPH` (8) / `BBFONT_SOKOBAN_GLYPH_COUNT` (39) |
| `smiley.inc` | `hgr_smiley28_font` + `HGR_SMILEY_BYTES_PER_GLYPH` (112) / `HGR_SMILEY_GLYPH_COUNT` (6) |

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
project under `sketchs/<card>/<name>/`):

    LIB := -I ../../../lib/apple1 -I ../../../lib/gen2
    LOAD_CFG := ../../../cc65/apple1_gen2.cfg

## Source of truth (asm ↔ C)

Two things are written once and shared with the C runtime in
[`../gen2c/`](../gen2c/):

- **Soft-switch / framebuffer addresses** — canonical in **`gen2.inc`**
  (`GEN2_TEXTOFF` = `$C250`, `GEN2_HGR1` = `$2000`, `GEN2_HGR2` = `$4000`);
  `gen2c/gen2.h` mirrors them (`GEN2_SS`, `GEN2_HGR1/2`). Edit `gen2.inc` first.
  Pinned by `tools/check_lib_equates.py`.
- **Beautiful Boot font** — canonical in **`bbfont_cp437.inc`** (the 256-glyph
  master); `tools/build_shared_font.py` emits the HGR and TMS9918 tables from it.
  Pinned by `build_shared_font.py --check`.

Both checks run under `make -C dev/lib check`.
