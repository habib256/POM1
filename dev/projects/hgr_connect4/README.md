# HGR Connect 4 — graphical port

Two-player Connect 4 (drop-piece, 7×6 grid) on the GEN2 Color Graphics
Card.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR_Connect4.asm` — main entry, loads at `$E000`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/hgr/HGR_Connect4.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR_Connect4.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_Connect4.o \
        -o ../../../software/hgr/HGR_Connect4.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR_Connect4.bin`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
