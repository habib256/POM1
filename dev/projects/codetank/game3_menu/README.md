# CodeTank GAME3 Upper-Bank Menu

*[← POM1 documentation index](../../../../doc/README.md)*

256-byte launcher at the start of the CodeTank **upper** 16 kB bank
(`$4000-$40FF`, board jumper = "Upper"). Lets the user pick between three
TMS9918 demos packed into the `Codetank_GAME3.rom` upper bank:

    $4100  TMS_LIFE     (~1 558 B)
    $4900  TMS_MANDEL   (~1 571 B)
    $5100  TMS_PLASMA   (~1 613 B)

The lower bank of the same cartridge ships TMS_Tetris/CodeTank at `$4000`
— flip the board jumper to "Lower" and `4000R` to launch it.

The menu only touches `$D010/$D011/$D012` and the JMP target — no RAM use,
no other cards required besides the TMS9918 (which the demos need).

## Hardware

- Machine: Apple 1 (8 KB dual-bank motherboard RAM — preset **9** default)
- Cards: P-LAB TMS9918, P-LAB CodeTank (upper-bank jumper)
- Recommended POM1 preset: **9** — *P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)* (`MainWindow_Presets.cpp`).

## Sources

- `codetank_game3_menu.asm` — main entry, loads at `$4000`
- `apple1_codetank_game3_menu.cfg` — local linker config (`CODE` at `$4000`, 256 B cap)
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../../software/Graphic TMS9918/codetank_game3_menu.bin

## Run in POM1

1. POM1 → Presets → **9** (TMS9918 + CodeTank).
2. CodeTank board jumper = Upper; the menu ships in `roms/codetank/Codetank_GAME3.rom` (upper half).
3. Wozmon `\` prompt: type `4000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
