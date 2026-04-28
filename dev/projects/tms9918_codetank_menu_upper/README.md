# CodeTank Upper-Bank Dispatcher Menu

128-byte launcher that lives at the start of the CodeTank **upper** 16 kB
bank (`$4000-$407F`). Replaces the Tetris-only auto-loader at `$4000` with
a 1-key prompt:

    1 = TETRIS   (copy 7 308 B payload from $4080 to $0280, JMP $0280)
    2 = LOGO     (JMP $5E00, runs in place from ROM)

Layout in the upper bank when the CodeTank board jumper = **Upper**:

    $4000-$407F  This menu (128 B, fits in the slot ahead of Tetris)
    $4080-$5DAB  Tetris raw payload (7 308 B, copied to $0280 at launch)
    $5DAC-$5DFF  $FF padding
    $5E00-$76FF  LOGO V1.8 ROM (run-in-place)
    $7700-$7FFF  $FF padding

Companion of [`tms9918_codetank_menu`](../tms9918_codetank_menu/) (lower
bank, 4-game menu). The two are independent: each owns half of the 32 kB
28c256 image picked by the CodeTank jumper. The lower bank ships the
arcade pack (Galaga / Sokoban / Snake / Life); the upper bank ships
Tetris + LOGO.

The menu only touches `$D010/$D011/$D012` and the JMP target — no RAM use
beyond the Tetris page-copy scratch (`$00-$03`), no other cards required
besides the TMS9918 (which both targets need).

## Hardware

- Machine: Apple 1 (4 KB DRAM)
- Cards: P-LAB TMS9918, P-LAB CodeTank (**upper-bank** jumper)
- Recommended POM1 preset: preset 2 (P-LAB Apple-1 with TMS9918 +
  CodeTank daughterboard) — set the CodeTank jumper to *Upper* via the
  Hardware → CodeTank dialog.

## Sources

- `codetank_menu_upper.asm` — main entry, loads at `$4000`
- `apple1_codetank_menu_upper.cfg` — 128 B linker config (`CODE` at
  `$4000`)
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/tms9918/codetank_menu_upper.bin

By hand:

    ca65 -I ../../lib/apple1 codetank_menu_upper.asm
    ld65 -C apple1_codetank_menu_upper.cfg codetank_menu_upper.o \
        -o ../../../software/tms9918/codetank_menu_upper.bin

The 128 B `.bin` then ships embedded inside `roms/codetank/Codetank_GAME1.rom`
(upper half) — see `tools/build_codetank_logo_rom.py`.

## Run in POM1

1. POM1 → Presets → preset 2 (TMS9918 + CodeTank).
2. Hardware → CodeTank → jumper = **Upper**, ROM = `Codetank_GAME1.rom`.
3. Wozmon `\` prompt: type `4000R`, then `1` for Tetris or `2` for LOGO.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
