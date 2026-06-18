# lib/games/rogue — roguelike shared primitives

*[← POM1 documentation index](../../../../doc/README.md)*

Display-agnostic 6502 building blocks for grid-based dungeon-crawlers, shared
across the rogue variants (TMS9918, and any future HGR / GT-6144 port). The
per-variant renderer, map storage and input glue stay in
`dev/projects/<card>/game_rogue/`.

## Files

- **`shadowcast.asm`** — recursive shadowcasting field-of-view (Björn
  Bergström, RogueBasin 2002). Partitions the plane around the player into 8
  octants processed recursively, narrowing the visible cone whenever a wall is
  hit. Result: mathematically symmetric FOV, no slope artifacts, no coverage
  holes. The lib walks an abstract 1-byte-per-cell map and fires two
  caller-supplied callbacks (`mark_visible_at_cur`, `is_opaque_at_cur`); the
  caller supplies grid dimensions as `SHADOW_COLS` / `SHADOW_ROWS` and the
  player position / radius via `player_col` / `player_row` / `fov_r`. Slope
  cross-multiplications go through `umul4`, so the caller must also
  `.include "multiply.asm"` (from `lib/m6502`).
- **`dungeon.asm`** — procedural dungeon-gen primitives. Today exposes
  `rand_mod` (uniform `[0, max)` PRNG wrapper around `prng16`); the bigger
  BSP-light generator stays inline in the project for now (see the file
  header). Requires `prng16` state (`.include "prng16.asm"` first) and a
  caller-owned `tmp` scratch byte.

## Use from a variant

In your project Makefile, add this directory (plus `lib/m6502` for the PRNG /
multiply helpers) to the include search path:

    LIB := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/games/rogue

Then, respecting the include order (PRNG / multiply before the consumers):

    .include "prng16.asm"
    .include "multiply.asm"
    .include "dungeon.asm"
    .include "shadowcast.asm"

See `dev/projects/tms9918/game_rogue/TMS_Rogue.asm` for the canonical caller
(`compute_fov` wrapper, `gen_dungeon` / `finalize_doors`).
