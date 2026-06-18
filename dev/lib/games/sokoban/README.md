# lib/games/sokoban — Sokoban shared data

*[← POM1 documentation index](../../../../doc/README.md)*

Level data and common structures shared between Sokoban variants. The same
levels back the text-mode 4 KB build, the GEN2 HGR build, and the TMS9918
build, so the level encoding is deliberately neutral.

## Files

- **`sokoban_common.inc`** — shared cell encoding, ZP slot equates,
  level-buffer layout. Assumes the caller defines a `LEVELBUF` zeropage segment
  (see `dev/cc65/` linker configs that reserve it).
- **`sokoban_levels.inc`** — base level pack (Microban-style packing).
- **`sokoban_levels_ext.inc`** — extended level pack.

## Use

    .include "apple1.inc"
    .include "sokoban_common.inc"
    .include "sokoban_levels.inc"

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/games/sokoban

Used by `dev/projects/apple1/game_sokoban/`, `dev/projects/gen2/game_sokoban/`,
`dev/projects/tms9918/game_sokoban/`.
