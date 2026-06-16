# GT-6144 Life — Conway's Game of Life

40×60 cell grid, centred on the 64×96 GT-6144 matrix. Byte-per-cell
storage with a ghost border (so neighbour counting skips the bounds
check) and a B3/S23 rule LUT. Display writes only the cells that change
between generations using a two-buffer flip — the previous frame *is*
the comparison baseline, the classic trick that lets a 1 MHz 6502
animate a Life grid in real time.

## Hardware

- Machine: Apple 1 (stock 4 KB)
- Cards: SWTPC GT-6144
- Recommended POM1 preset: 2 (Apple-1 + SWTPC GT-6144).

## Sources

- `GT1_Life.asm` — main entry, loads at `$0300`
- `gt6144.cfg` — local linker config (`CODE` at `$0300`, 4 KB cap)
- libs used: `dev/lib/apple1/`, `dev/lib/gt6144/`

## Build

    make                          # produces ../../../software/Graphic gt-6144/GT1_Life.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/gt6144 GT1_Life.asm
    ld65 -C gt6144.cfg GT1_Life.o -o ../../../software/Graphic gt-6144/GT1_Life.bin

## Run in POM1

1. POM1 → Presets → preset 2 (Apple-1 + SWTPC GT-6144).
2. File → Load → `software/Graphic gt-6144/GT1_Life.txt`.
3. Wozmon `\` prompt: type `300R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
