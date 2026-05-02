# HGR Maze — Recursive Backtracker on full HGR

Full-screen maze generator on the GEN2 Color Graphics Card. Recursive
Backtracker (DFS) algorithm, 34×23 cells mapped to the 280×192 HGR
buffer with sub-byte rendering via lookup tables (no NTSC artifact
fringing on cell borders).

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR_Maze.asm` — main entry, loads at `$E000`
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/hgr/HGR_Maze.bin

By hand:

    ca65 -I ../../lib/apple1 HGR_Maze.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_Maze.o \
        -o ../../../software/hgr/HGR_Maze.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR_Maze.bin`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
