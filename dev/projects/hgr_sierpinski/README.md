# HGR Sierpinski — bitwise fractal

Full-screen, centred Sierpinski triangle on the GEN2 Color Graphics
Card. Deterministic bitwise rendering — each lit pixel obeys the
classic `(x AND y) == 0` rule.

## Hardware

- Machine: Apple 1 (8 KB DRAM — GEN2 framebuffer at `$2000-$3FFF`)
- Cards: GEN2 HGR
- Recommended POM1 preset: 12 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_Sierpinski.asm` — main entry, loads at `$E000`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/` (`hgr_tables.inc`)

## Build

    make                          # produces ../../../software/Graphic HGR/HGR_Sierpinski.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR_Sierpinski.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_Sierpinski.o \
        -o ../../../software/Graphic HGR/HGR_Sierpinski.bin

## Run in POM1

1. POM1 → Presets → preset 12 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/Graphic HGR/HGR_Sierpinski.txt`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
