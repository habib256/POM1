# HGR Maze — Recursive Backtracker on full HGR

Full-screen maze generator on the GEN2 Color Graphics Card. Recursive
Backtracker (DFS) algorithm, 34×23 cells mapped to the 280×192 HGR
buffer with sub-byte rendering via lookup tables (no NTSC artifact
fringing on cell borders).

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: 12 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_Maze.asm` — main entry, loads at `$E000`
- `emit_HGR_Maze_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/m6502/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/Graphic HGR/HGR_Maze.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/hgr HGR_Maze.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_Maze.o \
        -o ../../../software/Graphic HGR/HGR_Maze.bin
    python3 emit_HGR_Maze_txt.py

## Run in POM1

1. POM1 → Presets → preset 12 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/Graphic HGR/HGR_Maze.txt`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
