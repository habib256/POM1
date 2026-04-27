# HGR7 Connect 4 — graphical port

Two-player Connect 4 (drop-piece, 7×6 grid) on the GEN2 Color Graphics
Card. Seventh entry in the HGR demo series.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR7_Connect4.asm` — main entry, loads at `$0280`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/hgr/HGR7_Connect4.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR7_Connect4.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR7_Connect4.o \
        -o ../../../software/hgr/HGR7_Connect4.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR7_Connect4.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
