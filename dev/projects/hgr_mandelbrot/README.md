# HGR Mandelbrot — fixed-point explorer

Mandelbrot set on the GEN2 Color Graphics Card. 4.12 fixed-point math,
byte-column rendering, six preset positions selectable at runtime.
Inspired by Fred Stark's text-mode `mandelbrot-65`.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR_Mandelbrot.asm` — main entry, loads at `$E000`
- `emit_HGR_Mandelbrot_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/`

## Build

    make                          # produces ../../../software/hgr/HGR_Mandelbrot.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR_Mandelbrot.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_Mandelbrot.o \
        -o ../../../software/hgr/HGR_Mandelbrot.bin
    python3 emit_HGR_Mandelbrot_txt.py

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR_Mandelbrot.txt`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
