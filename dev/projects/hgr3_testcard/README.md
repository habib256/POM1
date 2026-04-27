# HGR3 Test Card — NTSC artifact colours

Displays the six NTSC artifact colours produced by the GEN2 Color
Graphics Card: black, green, violet, white, orange, and blue. Useful
reference image for tuning monitor / emulator colour. Third entry in
the HGR demo series.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR3_TestCard.asm` — main entry, loads at `$0280`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/hgr/HGR3_TestCard.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR3_TestCard.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR3_TestCard.o \
        -o ../../../software/hgr/HGR3_TestCard.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR3_TestCard.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
