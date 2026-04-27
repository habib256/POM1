# HGR5 House — scene drawing

Draws a simple house and tree scene using fills and lines on the GEN2
Color Graphics Card, exercising the NTSC artifact-colour palette. Fifth
entry in the HGR demo series.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR5_House.asm` — main entry, loads at `$0280`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/hgr/HGR5_House.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR5_House.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR5_House.o \
        -o ../../../software/hgr/HGR5_House.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR5_House.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
