# HGR8 BB Font Show — CP437 font sheet

Displays all 256 code points from `fonts/font_codepage_437_8x8.png` in
linear CP437 order (index = IBM code point) on the GEN2 Color Graphics
Card. Useful as a glyph reference for HGR programs that need
non-Wozmon characters. Eighth entry in the HGR demo series.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR8_BBFontShow.asm` — main entry, loads at `$E000`
- `HGR8_BBFont.inc` — local CP437 8×8 font data
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/hgr/HGR8_BBFontShow.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR8_BBFontShow.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR8_BBFontShow.o \
        -o ../../../software/hgr/HGR8_BBFontShow.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR8_BBFontShow.bin`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
