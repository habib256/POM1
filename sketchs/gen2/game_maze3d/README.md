# game_maze3d (GEN2) — Wizardry-style 3D line maze for Uncle Bernie's GEN2 HGR card

*[← POM1 documentation index](../../doc/README.md)*

HGR port of the TMS9918 dungeon crawler
([`sketchs/tms9918/game_maze3d/`](../../tms9918/game_maze3d/) —
`TMS_Maze3D.asm`, the CodeTank bank build). The game logic — 11×7
backtracker-DFS maze, pseudo-3D wireframe renderer with depth stipple,
top-down map toggle, XP/level-up progression, turn-based combat against
goblins / orcs / dark mages with the epic narrator — is carried over
**verbatim**; only the Graphics II bitmap *primitives* were swapped for
HGR equivalents with identical contracts, so the 3D renderer, map view,
HUD and combat screens assemble untouched.

Runs on the "GEN2 HGR Color" 48 KB machine (POM1 preset 11) as a single
binary at `$6000` (chess model), drawing into HGR page 1. Entry: `6000R`.

## The primitive swap

The TMS version funnels every VRAM access through a small primitive set;
each got an HGR twin with the same name + register/ZP contract:

| Primitive | TMS9918 Graphics II | GEN2 HGR |
|---|---|---|
| `calc_pix_addr` | (x, y) → VRAM addr, 8 px/byte thirds layout | (x, y) → `hgr_lo/hi[y]` line base + `pix_col` byte column |
| `plot_set` | VRAM read-modify-write via data port | direct `ORA (ptr),Y` |
| `hline` / `vline` | auto-increment streams / 8-row group staging | `$7F` full bytes / per-scanline RMW at one byte column |
| `write_char`, `x2_tile` | glyph bytes streamed to the pattern table | `write_char` = thin wrapper over `hgr_putc8` (**`dev/lib/gen2/hgr_text8.asm`**, game font in TMS bit order via `ht_rev=1`); `x2_tile` stays local (doubled text) through `rev7_tab` |
| `draw_sprite16_x1/_x2/_x4` | 8×8-tile VRAM streams | `hgr_spr16_x1/_x2/_x4` from **`dev/lib/gen2/hgr_sprite16.asm`** — each output row built as a pixel bit-stream then repacked 7 px/HGR-byte, lossless (a naive byte-column mapping drops one column per source byte and visibly skews ×2 art) |
| `clear_viewport/bitmap/hud` | VRAM zero streams | scanline-band memset |
| `color_rect`, `fill_color_white` | Graphics II colour table | **stubs** (no colour table in HGR; depth cues survive via the stipple fills) |
| monster tint (`mob_colors`) | per-8×8-cell colour table entries | **NTSC artifact colour in the blitter**: `sp_put` ANDs each byte with a pixel-parity mask + ORs the palette bit — goblin **green**, orc **orange**, dark mage **violet** (title mascot, corridor clusters and the ×4 combat portrait all ride the same `mob_sprite_ptr` hook → `hgr_spr16_color_a` with `HSPR_*` codes; the parity mask halves the sprite's pixel density, the price of a pure HGR colour). The ×2 "MAZE 3D" title text rides the same attributes through `x2_put` and renders **orange** — the HGR red |
| `init_vdp_g2`, `init_sat`, pads | Mode II registers | `gen2_hgr_init_clear` + no-op stubs |
| `vdp_display_off/on` | display blank (R1 bit 6) around every full redraw | **flip to HGR page 2** (zeroed at boot) while page 1 is being drawn, flip back to reveal — the redraw is never visible, same UX as the TMS blank for two soft-switch reads |
| combat round repaint | full screen per exchange (cheap VDP streams) | full `draw_combat_screen` only on entry / next foe; each attack or failed-flee round repaints just the two HP fields (`combat_update_hp`, 4 digit cells ≈ 1.5k cycles instead of clear + ×4 portrait + labels ≈ 140k) |

**Pixel-space mapping**: the TMS 256×192 bitmap becomes 32 HGR byte
columns (4..35, centred in the 40-byte row). Each 8-px TMS byte column
lands on one 7-px HGR byte column: whole bytes go through `rev7_tab`
(bit 7 = leftmost → bit 0 = leftmost, rightmost pixel dropped);
per-pixel plots clamp `x%8 == 7` onto bit 6 so wall edges on those
columns still render. Text cells and sprite tiles stay 8-aligned in TMS
coordinates → byte-aligned in HGR, no shifting anywhere. The single-px
wireframe edges pick up NTSC artifact colour (green/violet by parity) —
a free, period-appropriate depth-cue bonus.

The SCROLL-O-SPRITES monster patterns are linked straight from the TMS
lib (`dev/lib/tms9918/sprites_{trollkind,characters}.asm` — pure
pattern data); the blitters convert bit order at draw time, so there is
**no asset conversion step**.

## Build

```bash
cd sketchs/gen2/game_maze3d
make            # -> "software/Graphic HGR"/HGR_Maze3D.{bin,txt}
```

Run it:

```bash
./build/POM1 --preset 11 --load 6000:"software/Graphic HGR/HGR_Maze3D.bin" --run 6000
```

or File → Load Memory the `.txt`/`.bin` from `software/Graphic HGR/`
(auto-enables the GEN2 card), then `6000R` from Wozmon. Also
discoverable in the DevBench (`.sketch.json` wires the cfg + sprite libs).

Headless smoke (any key at the title seeds the dungeon — same key =
same maze, so scripted runs are deterministic):

```bash
./build/POM1 --preset 11 --load 6000:"software/Graphic HGR/HGR_Maze3D.bin" --run 6000 \
  --paste-at-cycle 4000000 "A" --paste-at-cycle 8000000 "I" \
  --dump-after-cycles 11000000 --dump-gen2-frame /tmp/maze.png   # combat vs an orc
```

## Controls

Identical to the TMS version: `I`/`K` forward/backward, `J`/`L` turn,
`M` map toggle, `H` help, `A` attack / `F` flee in combat, `ESC` quit
to the monitor (restores TEXT mode). Find the exit `E`; beware the
denizens.

## Files

```
HGR_Maze3D.asm         main source (logic verbatim from TMS_Maze3D.asm +
                       HGR primitive layer; font + rev7 table in CODE)
apple1_maze3d_hgr.cfg  single-region cfg ($6000 code, $0E00 state segments)
Makefile               ca65 + sprite libs + ld65 + emit_gen2_txt.py
```
