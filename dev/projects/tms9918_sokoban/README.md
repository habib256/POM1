# TMS Sokoban — graphical port

Sokoban with a 16×16-block playfield (2×2 grid of 8×8 chars per tile)
on the P-LAB TMS9918 Graphic Card. Re-uses the level pack and core
push logic from `dev/lib/sokoban/` (Microban I #1..#44 by D.W. Skinner
plus three teaching levels). Each tile type lives in its own colour
group so the TMS9918 palette colours every block automatically.

Two linker-config variants (both target the CodeTank ROM window):

- `apple1_sokoban_codetank.cfg` — full 16 kB CodeTank ROM image at
  `$4000` (default).
- `apple1_sokoban_codetank_bank.cfg` — CodeTank lower-bank slot at
  `$5E00` inside the multi-game ROM image.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: P-LAB TMS9918, P-LAB CodeTank
- Recommended POM1 preset: preset 8 (TMS9918 + CodeTank) — TODO confirm.

## Sources

- `TMS_Sokoban.asm` — main entry, loads at `$4000` (or `$5E00`)
- `apple1_sokoban_codetank.cfg` — standalone CodeTank ROM (`$4000`,
  16 kB)
- `apple1_sokoban_codetank_bank.cfg` — slot inside the menu bank (`$5E00`)
- libs used: `dev/lib/apple1/`, `dev/lib/sokoban/`

## Build

    make                          # default = standalone CodeTank → ../../../software/tms9918/TMS_Sokoban.bin

Override the linker config from the command line:

    make CFG=apple1_sokoban_codetank_bank.cfg

## Run in POM1

1. POM1 → Presets → preset 8 (TMS9918 + CodeTank, TODO confirm).
2. CodeTank ROM in place; jumper = Lower.
3. Wozmon `\` prompt: type `4000R` (standalone) or `5E00R` (bank slot).

## Author / License

VERHILLE Arnaud, 2026. Microban I levels by David W. Skinner (2000).
License: TODO.
