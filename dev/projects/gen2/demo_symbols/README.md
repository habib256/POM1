# HGR Symbols Show — SCROLL-O-SPRITES "Symbols" on GEN2

*[← POM1 documentation index](../../../../doc/README.md)*

Displays the 23 SCROLL-O-SPRITES "Symbols" sprites (16×16, Quale CC-BY-3.0)
on Uncle Bernie's GEN2 Color Graphics Card. The sprites are laid out on a
6-column × 4-row grid (24 slots, 23 used), centred horizontally with a 36 px
top margin. A title line and a footer list of sprite names print to the text
screen; press any key to exit.

Sprite data comes from the shared HGR sprite library at
`dev/lib/gen2/sprites/sprites_symbols_hgr.asm`, auto-generated from the TMS9918
source by `tools/build_hgr_sprites.py` (the Makefile refreshes it on demand).

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: 11 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_SymbolsShow.asm` — main entry, loads at `$E000`
- `emit_HGR_SymbolsShow_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/gen2/`, `dev/lib/gen2/sprites/`

## Build

    make                          # produces ../../../../software/Graphic HGR/HGR_SymbolsShow.{bin,txt}

By hand:

    ca65 -I ../../../lib/apple1 -I ../../../lib/gen2 -I ../../../lib/gen2/sprites HGR_SymbolsShow.asm
    ld65 -C ../../../cc65/apple1_gen2.cfg HGR_SymbolsShow.o \
        -o ../../../../software/Graphic HGR/HGR_SymbolsShow.bin
    python3 emit_HGR_SymbolsShow_txt.py

## Run in POM1

1. POM1 → Presets → preset 11 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/Graphic HGR/HGR_SymbolsShow.txt`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. "Symbols" sprites by Quale (CC-BY-3.0).
License: GPL-3.0 (see repository LICENSE).
