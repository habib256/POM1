# Connect 4 — text mode

Two-player drop-piece game on a 7×6 grid, rendered in 40×24 text mode on
the stock Apple 1 terminal. Players alternate dropping `X` and `O`; the
first to align four wins.

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM)
- Cards: none
- Recommended POM1 preset: 1 (Apple-1 with ACI & BASIC; any text preset works).

## Sources

- `Connect4.asm` — main entry, loads at `$0280`
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/Apple-1 games/Connect4.bin

By hand:

    ca65 -I ../../lib/apple1 Connect4.asm
    ld65 -C ../../cc65/apple1_4k.cfg Connect4.o -o ../../../software/Apple-1 games/Connect4.bin

## Run in POM1

1. POM1 → Presets → preset 1 (Apple-1 with ACI & BASIC).
2. File → Load → `software/Apple-1 games/Connect4.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
