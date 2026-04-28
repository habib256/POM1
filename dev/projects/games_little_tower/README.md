# Little Tower v1.0 — text adventure

Apple 1 text adventure originally written September 2000, refreshed
April 2026. A short fantasy crawl in the early-`80s` style: read room
descriptions, type single-letter directions, hunt for the tower's
secret. Code lands at `$0280` (the canonical Apple-1 entry point), runs
in stock 8 KB DRAM (~6 KB binary, no expansion cards required).

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM is enough)
- Cards: none
- Recommended POM1 preset: TODO — any text-only Apple 1 preset.

## Sources

- `LittleTower-1.0.asm` — main entry, `.org $0280`
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/games/LittleTower-1.0.bin

By hand:

    ca65 -I ../../lib/apple1 LittleTower-1.0.asm
    ld65 -C ../../cc65/apple1_8k.cfg LittleTower-1.0.o -o ../../../software/games/LittleTower-1.0.bin

## Run in POM1

1. POM1 → Presets → any text-mode Apple 1 preset (TODO).
2. File → Load → `software/games/LittleTower-1.0.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, September 2000 (refreshed April 2026). License: TODO.
