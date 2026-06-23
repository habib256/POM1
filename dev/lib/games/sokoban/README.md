# lib/games/sokoban — Sokoban shared data

*[← lib/games index](../README.md)*

Level data and common structures shared between Sokoban variants. The same
levels back the text-mode 4 KB build, the GEN2 HGR build, and the TMS9918
build, so the level encoding is deliberately neutral.

## Files

- **`sokoban_common.inc`** — shared cell encoding (`TILE_FLOOR`..`TILE_PLAYER_TARGET`,
  0..6, plus `ascii_to_tile` / `leave_tile` / `enter_player` helpers), ZP slot
  equates, level-buffer layout. Expects the caller to define `STATE_GRID` and
  `LEVEL_BUF` — the absolute address of a >=240 byte scratch buffer (not a
  zeropage segment). The ZP slots fold onto the canonical pool from
  [`apple1/zp.inc`](../../apple1/zp.inc) (see the cross-lib zero-page contract in
  [`../../README.md`](../../README.md)) — `print_ptr_lo/hi` alias `print.asm`'s slots.
- **`sokoban_levels.inc`** — base level pack (Microban-style packing).
- **`sokoban_levels_ext.inc`** — extended level pack. Not currently linked by
  any project (kept for reference).

## Use

    .include "apple1.inc"
    .include "sokoban_common.inc"
    .include "sokoban_levels.inc"

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/games/sokoban

Used by `sketchs/apple1/game_sokoban/`, `sketchs/gen2/game_sokoban/`,
`sketchs/tms9918/game_sokoban/`.
