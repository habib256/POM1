# Connect 4 — text mode

Two-player drop-piece game on a 7×6 grid, rendered in 40×24 text mode on
the stock Apple 1 terminal. Players alternate dropping `X` and `O`; the
first to align four wins.

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM)
- Cards: none
- Recommended POM1 preset: TODO — any text-only Apple 1 preset works.

## Sources

- `Connect4.asm` — main entry, loads at `$0280`
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/games/Connect4.bin

By hand:

    ca65 -I ../../lib/apple1 Connect4.asm
    ld65 -C ../../cc65/apple1_4k.cfg Connect4.o -o ../../../software/games/Connect4.bin

## Run in POM1

1. POM1 → Presets → any text-mode Apple 1 preset (TODO).
2. File → Load → `software/games/Connect4.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
