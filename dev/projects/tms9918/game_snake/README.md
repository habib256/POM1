# TMS Snake — classic snake on TMS9918

*[← POM1 documentation index](../../../../doc/README.md)*

Eat the apples, grow longer, don't bite yourself or hit the wall. Score
= food eaten. 32×24 Graphics I mode on the P-LAB TMS9918 Graphic Card;
row 0 is the score HUD, rows 1..23 form the playfield (30×21 playable
inside the walls). Algorithmic collision checks (no cell grid) keep the
whole game inside the stock 4 KB DRAM footprint.

Two linker-config variants:

- `apple1_snake.cfg` — stock cassette/`.txt` load at `$0280` (default).
- `apple1_snake_codetank_bank.cfg` — CodeTank lower-bank slot at
  `$7100` inside the multi-game ROM image.

## Hardware

- Machine: Apple 1 (4 KB DRAM is enough)
- Cards: P-LAB TMS9918 (CodeTank for the bank variant)
- Recommended POM1 preset: 9 (P-LAB TMS9918 + CodeTank).

## Sources

- `TMS_Snake.asm` — main entry, loads at `$0280` (or `$7100`)
- `apple1_snake.cfg` — default cassette config (CODE `$0280`, 2.4 KB;
  SNX/SNY ring buffers at `$0C00`/`$0D00`)
- `apple1_snake_codetank_bank.cfg` — CodeTank lower-bank slot (`$7100`)
- `emit_TMS_Snake_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/m6502/`, `dev/lib/tms9918/`

## Build

    make                          # default = cassette → ../../../../software/Graphic TMS9918/TMS_Snake.{bin,txt}

Override the linker config from the command line:

    make CFG=apple1_snake_codetank_bank.cfg

## Run in POM1

1. POM1 → Presets → preset 9 (P-LAB TMS9918 + CodeTank).
2. File → Load → `software/Graphic TMS9918/TMS_Snake.txt`.
3. Wozmon `\` prompt: type `280R` (cassette) or `7100R` (CodeTank).

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
