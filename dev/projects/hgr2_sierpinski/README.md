# HGR2 Sierpinski — bitwise fractal

Full-screen, centred Sierpinski triangle on the GEN2 Color Graphics
Card. Deterministic bitwise rendering — each lit pixel obeys the
classic `(x AND y) == 0` rule. Second entry in the HGR demo series.

## Hardware

- Machine: Apple 1 (8 KB DRAM — GEN2 framebuffer at `$2000-$3FFF`)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR2_Sierpinski.asm` — main entry, loads at `$E000`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/` (`hgr_tables.inc`)

## Build

    make                          # produces ../../../software/hgr/HGR2_Sierpinski.bin

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR2_Sierpinski.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR2_Sierpinski.o \
        -o ../../../software/hgr/HGR2_Sierpinski.bin

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR2_Sierpinski.bin`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
