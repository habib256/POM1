# CodeTank Upper-Bank Tetris Loader

128-byte bootstrap that lives at `$4000` in the CodeTank *upper* 16 kB
bank. Tetris (Nippur72 / KickC, `software/tms9918/tetris.bin`) is a
7 308-byte raw binary that loads at `$0280` with absolute references
baked into the code — no source available to relink — so we ship the
payload in ROM at file offset `$0080` (CPU `$4080`) and copy it down to
`$0280` on launch, then `JMP $0280`.

Two-stage flow: bootstrap (this file, runs from ROM at `$4000`)
→ page-copy 29 × 256 B from `$4080` to `$0280` → `JMP $0280` (Tetris
runs from RAM). The bootstrap stays in ROM and never touches RAM
beyond the 4-byte page-copy scratch (`$00-$03`).

> **Note:** the shipped `roms/codetank/Codetank_GAME1.rom` upper bank now
> uses [`tms9918_codetank_menu_upper`](../tms9918_codetank_menu_upper/)
> (Tetris + LOGO dispatcher) ahead of the same Tetris payload. The
> standalone bootstrap here is kept for ROM images that want Tetris as
> the sole upper-bank target.

Layout in the upper bank (standalone Tetris build):

    $4000  Bootstrap (this file)
    $4080  Tetris payload (7 308 B, padded with $FF to fill the bank)

Tetris occupies `$0280-$1FAC` once copied, so it needs an Apple 1 with
at least 8 KB DRAM (preset 8 ships 16 KB).

## Hardware

- Machine: Apple 1 (8 KB DRAM minimum)
- Cards: P-LAB TMS9918, P-LAB CodeTank (Upper jumper)
- Recommended POM1 preset: preset 8 (TMS9918 + CodeTank) — TODO confirm.

## Sources

- `tetris_loader.asm` — main entry, loads at `$4000`
- `apple1_tetris_loader.cfg` — local linker config (`CODE` at `$4000`,
  128 B cap)
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/tms9918/tetris_loader.bin

By hand:

    ca65 -I ../../lib/apple1 tetris_loader.asm
    ld65 -C apple1_tetris_loader.cfg tetris_loader.o \
        -o ../../../software/tms9918/tetris_loader.bin

The full upper-bank ROM image (loader + payload) is stitched together
externally — this Makefile only builds the bootstrap.

## Run in POM1

1. POM1 → Presets → preset 8 (TMS9918 + CodeTank, TODO confirm).
2. CodeTank board jumper = Upper; ROM image with the loader + Tetris
   payload installed.
3. Wozmon `\` prompt: type `4000R`.

## Author / License

VERHILLE Arnaud, 2026 (loader). Tetris payload: Nippur72 / KickC port.
License: TODO.
