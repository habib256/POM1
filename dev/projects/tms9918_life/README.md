# TMS Life — Conway's Game of Life (multi-pattern)

Conway's Game of Life on the P-LAB TMS9918 Graphic Card. 32×24 cells
(one 8×8 char per cell) in TMS9918 Graphics I mode, double-buffered,
B3/S23 rules with dead borders. Press any key (or `K`) to cycle to the
next preset pattern; `SPACE` pauses; `.` single-steps when paused.

Two linker-config variants:

- Default cassette build at `$0280` (uses `dev/cc65/apple1_4k.cfg`).
- `apple1_life_codetank_bank.cfg` — CodeTank lower-bank slot at `$7A00`
  inside the multi-game ROM image.

## Hardware

- Machine: Apple 1 (4 KB DRAM for cassette; 8 KB to play comfortably)
- Cards: P-LAB TMS9918 (CodeTank for the bank variant)
- Recommended POM1 preset: 7 (P-LAB TMS9918 + CodeTank).

## Sources

- `TMS_Life.asm` — main entry, loads at `$0280` (or `$7A00`)
- `apple1_life_codetank_bank.cfg` — CodeTank lower-bank slot (`$7A00`)
- `emit_TMS_Life_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/`

## Build

    make                          # default = cassette → ../../../software/Graphic TMS9918/TMS_Life.{bin,txt}

Override the linker config from the command line:

    make CFG=apple1_life_codetank_bank.cfg

## Run in POM1

1. POM1 → Presets → preset 7 (P-LAB TMS9918 + CodeTank).
2. File → Load → `software/Graphic TMS9918/TMS_Life.txt`.
3. Wozmon `\` prompt: type `280R` (cassette) or `7A00R` (CodeTank).

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
