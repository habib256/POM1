# HGR Sokoban — graphical port

*[← POM1 documentation index](../../../doc/README.md)*

Sokoban with a graphical playfield on the GEN2 Color Graphics Card.
Re-uses the level pack and core push logic from `dev/projects/games_sokoban/`
via `dev/lib/games/sokoban/`, swapping the text renderer for HGR sprites.

## Hardware

- Machine: Apple 1 (8 KB DRAM, GEN2 framebuffer at `$2000-$3FFF`)
- Cards: GEN2 HGR
- Recommended POM1 preset: 12 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_Sokoban.asm` — main entry, loads at `$0280`
- `bbfont_subset.inc` (in `dev/lib/hgr/`) — HUD/title 8×8 font subset
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`, `dev/lib/games/sokoban/`
- linker config: `apple1_sok_hgr.cfg` from `dev/projects/games_sokoban/`

## Build

    make                          # produces ../../../software/Graphic HGR/HGR_Sokoban.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr -I ../../lib/games/sokoban HGR_Sokoban.asm
    ld65 -C ../games_sokoban/apple1_sok_hgr.cfg HGR_Sokoban.o \
        -o ../../../software/Graphic HGR/HGR_Sokoban.bin

## Run in POM1

1. POM1 → Presets → preset 12 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/Graphic HGR/HGR_Sokoban.txt`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. Microban I levels by David W. Skinner (2000).
License: GPL-3.0 (see repository LICENSE).
