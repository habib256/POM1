# Sokoban — text mode

Classic push-boxes puzzle in 40×24 text. 45 levels: 3 teaching levels
plus Microban I #1..#42 by David W. Skinner (2000). Keyboard W/A/S/D
(QWERTY) or Z/Q/S/D (AZERTY). `U` = undo, `R` = reset level, `N` = next
level. The 4 KB build (default) is sized to fit on a real stock Apple 1
with no expansion RAM.

## Hardware

- Machine: Apple 1 (4 KB stock — `apple1_sok_4k.cfg`, default).
  Larger budgets (`apple1_sok_8k.cfg`, `apple1_sok_hgr.cfg`) ship for
  variants that target 8 KB / GEN2 HGR setups respectively.
- Cards: none (the HGR variant is consumed by `dev/projects/hgr_sokoban/`)
- Recommended POM1 preset: TODO — any text-mode Apple 1 preset.

## Sources

- `Sokoban.asm` — main entry, loads at `$0280`
- `apple1_sok_4k.cfg` — 4 KB DRAM target (default Makefile config)
- `apple1_sok_8k.cfg` — 8 KB DRAM target (used by `tms9918_sokoban`)
- `apple1_sok_hgr.cfg` — 8 KB DRAM + GEN2 HGR (used by `hgr_sokoban`)
- libs used: `dev/lib/apple1/`, `dev/lib/games/sokoban/`

## Build

    make                          # default = 4K → ../../../software/games/Sokoban.bin

By hand (any of the three variants):

    ca65 -I ../../lib/apple1 -I ../../lib/games/sokoban Sokoban.asm
    ld65 -C apple1_sok_4k.cfg Sokoban.o -o ../../../software/games/Sokoban.bin

## Run in POM1

1. POM1 → Presets → any text-mode Apple 1 preset (TODO).
2. File → Load → `software/games/Sokoban.bin`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. Microban I levels by David W. Skinner (2000).
License: TODO.
