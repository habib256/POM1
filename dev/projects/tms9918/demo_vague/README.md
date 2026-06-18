# TMS Vague — "Boat on a wave" (MSX1 tile technique)

*[← POM1 documentation index](../../../../doc/README.md)*

Tile-based sea/wave effect for the P-LAB Apple-1 TMS9918 Graphic Card, in
Mode 1 (Graphics I). It uses the canonical MSX1-scene technique: the wave
surface is the **background tile map**, not sprites. Nine "water-height" tile
patterns (#0 = empty sky … #8 = full-water cell) are pre-uploaded; each frame
the 3-row wave band (rows 11–13) is filled per column from a sine LUT, with
solid water (tile #8) below and sky (tile #0) above. The boat is a single
16×16 hardware sprite that samples the same sine function at its X position so
its hull stays glued to the local wave height.

Per-frame work is about 5.5k cycles on the 1.022 MHz 6502 (~32% CPU), split
into wave/tile computation during active display and VRAM streaming during
V-blank. Press ESC to exit to Wozmon.

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM)
- Card: P-LAB TMS9918 Graphic Card
- Recommended POM1 preset: 9 (P-LAB TMS9918 + CodeTank).

## Sources

- `TMS_Vague.asm` — main entry, loads at `$0280`
- `apple1_vague.cfg` — ld65 config (4 KB stock layout)
- `emit_TMS_Vague_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/` (m1 + pad)

## Build

    make                          # → ../../../../software/Graphic TMS9918/TMS_Vague.{bin,txt}

## Run in POM1

1. POM1 → Hardware → plug the TMS9918 card (auto-enabled when loading
   from `software/Graphic TMS9918/`).
2. File → Load → `software/Graphic TMS9918/TMS_Vague.txt`.
3. Wozmon `\` prompt: type `280R`.
4. ESC returns to Wozmon.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
