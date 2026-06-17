# TMS Maze 3D — wire-frame dungeon crawler

*[← POM1 documentation index](../../../doc/README.md)*

A 1976-style first-person dungeon crawler with monsters, on the P-LAB
TMS9918 Graphic Card. Recursive-Backtracker maze (11×7 cells),
pseudo-3D wire-frame view with depth shading (stipple/hatching), top-
down map toggle, turn-based combat against three monster archetypes.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: P-LAB TMS9918
- Recommended POM1 preset: 7 (P-LAB TMS9918 + CodeTank).

## Sources

- `TMS_Maze3D.asm` — main entry, loads at `$0280`
- `apple1_maze3d.cfg` — local linker config (CODE `$0280`, 6.25 KB;
  GRID, STK, MOBS bss segments)
- `emit_TMS_Maze3D_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/`

## Build

    make                          # produces ../../../software/Graphic TMS9918/TMS_Maze3D.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/tms9918 TMS_Maze3D.asm
    ld65 -C apple1_maze3d.cfg TMS_Maze3D.o \
        -o ../../../software/Graphic TMS9918/TMS_Maze3D.bin
    python3 emit_TMS_Maze3D_txt.py

## Run in POM1

1. POM1 → Presets → preset 7 (P-LAB TMS9918 + CodeTank).
2. File → Load → `software/Graphic TMS9918/TMS_Maze3D.txt`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
