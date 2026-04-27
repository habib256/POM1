# TMS LOGO — turtle interpreter

Apple 1 LOGO interpreter targeting the P-LAB TMS9918 Graphic Card.
Mode-2 bitmap turtle, REPL with line editor, error reporting,
user-defined procedures stored in a heap segment, banner / demo. The
public face of the m6502 + tms9918 shared libraries — the entry source
imports `signed_sin`, `init_vdp_g2`, `line_xy`, etc. and links three
modules together.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: P-LAB TMS9918
- Recommended POM1 preset: preset 8 (P-LAB Apple-1 with TMS9918) —
  TODO confirm.

## Sources

- `TMS_Logo.asm` — main entry, loads at `$0280` (link order matters —
  must be first so the entry point sits at `$0280`)
- `apple1_logo.cfg` — local linker config (CODE `$0280`, 6.4 KB; LBUF
  zero-page line buffer; PROC heap)
- `emit_TMS_Logo_txt.py` — assemble all 3 modules + link + emit Woz hex
- libs used: `dev/lib/apple1/`, `dev/lib/m6502/` (`math.asm` linked as
  separate module), `dev/lib/tms9918/` (`tms9918m2.asm` linked as
  separate module)

## Build

    make                          # produces ../../../software/tms9918/TMS_Logo.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918 TMS_Logo.asm
    ca65 -I ../../lib/apple1 -o math.o ../../lib/m6502/math.asm
    ca65 -I ../../lib/apple1 -o tms9918m2.o ../../lib/tms9918/tms9918m2.asm
    ld65 -C apple1_logo.cfg TMS_Logo.o math.o tms9918m2.o \
        -o ../../../software/tms9918/TMS_Logo.bin
    python3 emit_TMS_Logo_txt.py

## Run in POM1

1. POM1 → Presets → preset 8 (TMS9918 + CodeTank, TODO confirm).
2. File → Load → `software/tms9918/TMS_Logo.txt`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
