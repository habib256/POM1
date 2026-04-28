# TMS Galaga — hardware-sprite shoot-'em-up

Hardware-sprite Galaga on the P-LAB TMS9918 Graphic Card: a player ship
at the bottom plus three increasingly tough alien types hovering in
formation that periodically dive at the player. HP per type 2 / 4 / 6,
three player lives.

Three linker-config variants ship for different deployment scenarios:

- `apple1_galaga.cfg` — stock cassette/`.txt` load at `$0280` (default).
- `apple1_galaga_codetank.cfg` — full 16 kB CodeTank ROM image at `$4000`.
- `apple1_galaga_codetank_bank.cfg` — lower-bank slot (`$4100`) inside
  the multi-game CodeTank bank used by `tms9918_codetank_menu`.

## Sprite layout

22 distinct 16×16 sprite patterns share the TMS9918 sprite-pattern table
(each 16×16 sprite consumes 4 of the 32 pattern slots). The full map
lives at the top of `TMS_Galaga.asm` (lines ~82-105) under
*Sprite pattern names*; ASCII-art pixel maps for every glyph follow
inline alongside the data tables (search for `; --- Sprite patterns ---`
and the `.byte %...` rows). Animation frames use the `_ALT` slots
(`P_ENEMY*_ALT`, `P_EXP_ALT`, `P_PLAYER_TH`); the 32×32 super-boss is
stitched from four quadrants (`P_SUPER_{TL,TR,BL,BR}`).

## Hardware

- Machine: Apple 1 (4 KB DRAM for the cassette build)
- Cards: P-LAB TMS9918 (CodeTank for the ROM variants)
- Recommended POM1 preset: TODO — pick a TMS9918 preset; preset 8 for
  the CodeTank variants.

## Sources

- `TMS_Galaga.asm` — main entry, loads at `$0280` (or `$4000`/`$4100`
  for the CodeTank variants)
- `apple1_galaga.cfg` — default cassette config (`CODE` at `$0280`)
- `apple1_galaga_codetank.cfg` — standalone CodeTank ROM (`$4000`, 16 kB)
- `apple1_galaga_codetank_bank.cfg` — slot inside the menu bank (`$4100`)
- `emit_TMS_Galaga_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/`

## Build

    make                          # default = cassette → ../../../software/tms9918/TMS_Galaga.{bin,txt}

Override the linker config from the command line:

    make CFG=apple1_galaga_codetank.cfg

## Run in POM1

1. POM1 → Presets → TMS9918 preset (TODO; preset 8 for CodeTank).
2. File → Load → `software/tms9918/TMS_Galaga.txt`.
3. Wozmon `\` prompt: type `280R` (cassette) or `4000R` (CodeTank).

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
