# HGR Test Card — NTSC artifact colours

Displays the six NTSC artifact colours produced by the GEN2 Color
Graphics Card: black, green, violet, white, orange, and blue. Useful
reference image for tuning monitor / emulator colour.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: 12 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_TestCard.asm` — main entry, loads at `$E000`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/hgr/HGR_TestCard.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR_TestCard.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_TestCard.o \
        -o ../../../software/hgr/HGR_TestCard.bin

## Run in POM1

1. POM1 → Presets → preset 12 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/hgr/HGR_TestCard.bin`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
