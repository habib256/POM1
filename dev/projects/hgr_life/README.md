# HGR Life — Conway's Game of Life (multi-pattern)

*[← POM1 documentation index](../../../doc/README.md)*

Conway's Game of Life on the GEN2 Color Graphics Card. 40×40 cell grid,
7×4 pixels per cell, double-buffered. B3/S23 rules with dead borders
(gliders die at the edge). Press any key to cycle to the next preset
pattern, ESC to exit.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: 12 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_Life.asm` — main entry, loads at `$E000`
- `emit_HGR_Life_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/Graphic HGR/HGR_Life.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR_Life.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_Life.o \
        -o ../../../software/Graphic HGR/HGR_Life.bin
    python3 emit_HGR_Life_txt.py

## Run in POM1

1. POM1 → Presets → preset 12 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/Graphic HGR/HGR_Life.txt`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
