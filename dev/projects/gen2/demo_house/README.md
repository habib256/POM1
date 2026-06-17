# HGR House — scene drawing

*[← POM1 documentation index](../../../../doc/README.md)*

Draws a simple house and tree scene using fills and lines on the GEN2
Color Graphics Card, exercising the NTSC artifact-colour palette.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: 12 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_House.asm` — main entry, loads at `$E000`
- libs used: `dev/lib/apple1/`, `dev/lib/gen2/`, `dev/lib/m6502/`

## Build

    make                          # produces ../../../../software/Graphic HGR/HGR_House.bin

By hand:

    ca65 -I ../../../lib/apple1 -I ../../../lib/gen2 -I ../../../lib/m6502 HGR_House.asm
    ld65 -C ../../../cc65/apple1_gen2.cfg HGR_House.o \
        -o ../../../../software/Graphic HGR/HGR_House.bin

## Run in POM1

1. POM1 → Presets → preset 12 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/Graphic HGR/HGR_House.bin`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
