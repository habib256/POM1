# CodeTank Lower-Bank Menu

*[← POM1 documentation index](../../../../doc/README.md)*

256-byte launcher that lives at the start of the CodeTank lower 16 kB
bank (`$4000-$40FF`). Lets the user pick between three games packed into
the same bank:

    $4100  TMS_A1GALAGA
    $6200  TMS_SOKOBAN
    $7600  TMS_SNAKE

TMS_LOGO V2.6 (turtle interpreter REPL) lives in the *upper* bank — flip
the CodeTank board jumper to "Upper" then `4000R` to launch it. Life
moved to its own cartridge (`Codetank_GAME3.rom`).

The menu only touches `$D010/$D011/$D012` and the JMP target — no RAM
use, no other cards required besides the TMS9918 (which the games need).

## Hardware

- Machine: Apple 1 (8 KB dual-bank motherboard RAM — preset **9** default)
- Cards: P-LAB TMS9918, P-LAB CodeTank (lower-bank jumper)
- Recommended POM1 preset: **9** — *P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)* (`MainWindow_Presets.cpp`).

## Sources

- `codetank_menu.asm` — main entry, loads at `$4000`
- `apple1_codetank_menu.cfg` — local linker config (`CODE` at `$4000`,
  256 B cap)
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../../software/Graphic TMS9918/codetank_menu.bin

By hand:

    ca65 -I ../../../lib/apple1 codetank_menu.asm
    ld65 -C apple1_codetank_menu.cfg codetank_menu.o \
        -o ../../../../software/Graphic TMS9918/codetank_menu.bin

## Run in POM1

1. POM1 → Presets → **9** (TMS9918 + CodeTank).
2. CodeTank board jumper = Lower; the menu ships in `roms/codetank/Codetank_GAME1.rom` (lower half).
3. Wozmon `\` prompt: type `4000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
