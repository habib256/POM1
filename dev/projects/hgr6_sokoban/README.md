# HGR6 Sokoban — graphical port

Sokoban with a graphical playfield on the GEN2 Color Graphics Card.
Re-uses the level pack and core push logic from `dev/projects/games_sokoban/`
via `dev/lib/sokoban/`, swapping the text renderer for HGR sprites.
Sixth entry in the HGR demo series.

## Hardware

- Machine: Apple 1 (8 KB DRAM, GEN2 framebuffer at `$2000-$3FFF`)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR6_Sokoban.asm` — main entry, loads at `$0280`
- `HGR6_Sokoban_bbfont.inc` — local 8×8 font data
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`, `dev/lib/sokoban/`
- linker config: `apple1_sok_hgr.cfg` from `dev/projects/games_sokoban/`

## Build

    make                          # produces ../../../software/hgr/HGR6_Sokoban.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr -I ../../lib/sokoban HGR6_Sokoban.asm
    ld65 -C ../games_sokoban/apple1_sok_hgr.cfg HGR6_Sokoban.o \
        -o ../../../software/hgr/HGR6_Sokoban.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR6_Sokoban.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. Microban I levels by David W. Skinner (2000).
License: TODO.
