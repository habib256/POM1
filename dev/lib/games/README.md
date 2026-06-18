# lib/games — shared 6502 game libraries

*[← POM1 documentation index](../../../doc/README.md)*

Reusable, display-agnostic asm building blocks for the Apple-1 games shipped
by POM1. Each subdirectory holds the data and routines shared across that
game's text / TMS9918 / HGR / GT-6144 variants; the per-variant renderer and
input glue stay in `dev/projects/<card>/<name>/`.

## Game libs

| Directory | Contents |
|---|---|
| [`chess/`](chess/README.md) | Platform-agnostic chess engine (`chess_engine.asm`), equates, direction tables, algebraic text I/O. |
| [`rogue/`](rogue/README.md) | Roguelike primitives: recursive shadowcasting FOV (`shadowcast.asm`) + procedural dungeon-gen helpers (`dungeon.asm`). |
| [`sokoban/`](sokoban/README.md) | Shared Sokoban cell encoding, ZP layout, and RLE-packed level data. |

## Rule of Three / promotion note

Code stays inline in a single project until a **second** consumer needs it;
promotion to a `lib/games/<name>/` module lands when the **third** would
(the classic "Rule of Three"). Until then the candidate stays in the
originating project with its parametrisation hooks documented in the relevant
comment blocks — see, for example, `dungeon.asm`, which only exposes the
truly grid-agnostic `rand_mod` while the project-specific BSP-light generator
remains inline in `dev/projects/tms9918/game_rogue/TMS_Rogue.asm`.

Include any of these libs by adding the directory to your project's `-I`
search path, e.g. `-I ../../lib/games/rogue`.
