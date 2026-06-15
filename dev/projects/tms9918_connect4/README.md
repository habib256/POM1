# TMS Connect 4 — graphical port

Two-player Connect 4 (drop-piece, 7×6 grid) on the P-LAB TMS9918
Graphic Card. Companion of `games_connect4` (text mode) and
`hgr_connect4` (GEN2 HGR).

## Hardware

- Machine: Apple 1 (4 KB DRAM is enough)
- Cards: P-LAB TMS9918
- Recommended POM1 preset: 7 (P-LAB TMS9918 + CodeTank).

## Sources

- `TMS_Connect4.asm` — main entry, loads at `$0280`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/`

## Build

    make                          # produces ../../../software/tms9918/TMS_Connect4.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/tms9918 TMS_Connect4.asm
    ld65 -C ../../cc65/apple1_4k.cfg TMS_Connect4.o \
        -o ../../../software/tms9918/TMS_Connect4.bin

## Run in POM1

1. POM1 → Presets → preset 7 (P-LAB TMS9918 + CodeTank).
2. File → Load → `software/tms9918/TMS_Connect4.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
