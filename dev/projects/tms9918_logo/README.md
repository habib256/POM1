# TMS LOGO — turtle interpreter (V1.8 final 8 KB)

Apple 1 LOGO interpreter targeting the P-LAB TMS9918 Graphic Card.
Mode-2 bitmap turtle, REPL with line editor, error reporting,
user-defined procedures with parameters and tail-recursive calls,
IF / STOP / arithmetic, REPEAT (incl. FOREVER + ESC abort),
banner / demo. Links three modules together (TMS_Logo + math +
tms9918m2).

## Status

**V1.8 is the FINAL 8 KB build.** The cassette / `.txt`-loadable variant
that fits a stock Apple-1 with 8 KB DRAM is frozen — CODE and PROC
areas are at the absolute byte-level limit and no further features
should be added without growing past 8 KB. The roadmap for new
features (deeper recursion, more procs, longer bodies, IF/THEN/ELSE,
LISTS, etc.) is the V2.0 16 KB variant intended for CodeTank delivery
(see "16 KB variant" below).

## Variants

| Build | Linker config | Runtime location | Target |
|-------|---------------|------------------|--------|
| Cassette / `.txt` (default) | `apple1_logo.cfg` | RAM `$0280-$1FFF` | 8 KB Apple-1 |
| CodeTank ROM (upper bank, alongside Tetris) | `apple1_logo_codetank_bank.cfg` | ROM `$5E00+`, RAM BSS at `$0200`/`$0280` | preset 8 |
| 16 KB variant (planned) | `apple1_logo_16k.cfg` (TODO) | RAM `$0280-$3FFF` | 16 KB Apple-1 / future CodeTank ROM |

## Hardware

- Machine: Apple 1 (8 KB DRAM minimum; 16 KB for the planned V2.0)
- Cards: P-LAB TMS9918
- Recommended POM1 preset: preset 8 (P-LAB Apple-1 with TMS9918 + CodeTank)

## Language summary (V1.8)

- **Turtle**: `FD/FORWARD n`, `BK/BACK n`, `TR/RIGHT n`, `TL/LEFT n`,
  `PU/PENUP`, `PD/PENDOWN`, `HOME`, `CS/CLEARSCREEN`, `SETXY x y`, `SETH n`.
- **Console**: `PRINT "WORD` or `PRINT N`, `WAIT N`, `BYE`, `HELP`.
- **Control flow**: `REPEAT N [ ... ]`, `REPEAT FOREVER [ ... ]`
  (ESC or Ctrl-G aborts cleanly, procs survive). `IF a > b [ ... ]`
  (also `<` and `=`). `STOP` exits the current proc.
- **Variables**: `MAKE NAME N`, then `:NAME` reads it. Single-level
  arithmetic in arg position: `:size + 2`, `:a - 1`. `RANDOM N` for
  `0..N-1`.
- **Procedures**: `TO NAME :p1 :p2 ... END`. Up to 5 procs, 154 B body,
  6-char identifiers (A-Z + 0-9, so `THING` and `THING1` stay
  distinct). Tail recursion has zero frame cost — `spiral :size :angle`
  going `:size + 2` to 100 runs ~50 logical levels in one frame.

## Sources

- `TMS_Logo.asm` — main entry. Loads at `$0280` (link order matters —
  must be first so the entry point sits at `$0280`).
- `apple1_logo.cfg` — standalone 8 KB cassette config.
- `apple1_logo_codetank_bank.cfg` — CodeTank ROM build (CODE at
  `$5E00`, BSS contiguous from `$0200`).
- `emit_TMS_Logo_txt.py` — assemble all 3 modules + link + emit Woz hex.
- libs used: `dev/lib/apple1/`, `dev/lib/m6502/` (`math.asm` linked as
  a separate module), `dev/lib/tms9918/` (`tms9918m2.asm` linked as a
  separate module).

## Build

    make                          # produces ../../../software/tms9918/TMS_Logo.{bin,txt}

CodeTank ROM (assembles into `roms/codetank/tetris_logo.rom`):

    python3 ../../../tools/build_codetank_logo_rom.py

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918 TMS_Logo.asm
    ca65 -I ../../lib/apple1 -o math.o ../../lib/m6502/math.asm
    ca65 -I ../../lib/apple1 -o tms9918m2.o ../../lib/tms9918/tms9918m2.asm
    ld65 -C apple1_logo.cfg TMS_Logo.o math.o tms9918m2.o \
        -o ../../../software/tms9918/TMS_Logo.bin
    python3 emit_TMS_Logo_txt.py

## Run in POM1

1. POM1 → Presets → preset 8 (TMS9918 + CodeTank).
2. File → Load → `software/tms9918/TMS_Logo.txt`.
3. Wozmon `\` prompt: type `280R`.
4. Or for the CodeTank ROM: load `roms/codetank/tetris_logo.rom`,
   jumper Upper, type `4000R`, pick `2` for LOGO.

## 16 KB variant (V2.0, active)

Targets an Apple-1 with 16 KB DRAM (and a future CodeTank ROM with the
extra space). Source: `TMS_Logo_16k.asm`, cfg: `apple1_logo_16k.cfg`,
build target: `make v20`.

Currently implemented (vs V1.8):

- **More procs / longer bodies**: 10 procs of 224 B (V1.8 had 5 of 154 B).
- **Deep non-tail recursion**: 1 024-byte control stack in PROCBSS,
  16 frames. Replaces V1.8's pair of 60 B save buffers; tail-call
  optimization unchanged so deep tail recursion still costs zero
  frames.
- **IFELSE**: `IFELSE a OP b [yes] [no]` runs the matching block.
- **Operators**: `<= >= <>` in addition to V1.8's `< > =`. All operators
  are encoded as a 3-bit "allowed signs" mask (NEG/ZERO/POS) and
  evaluated by a single 16-bit signed subtraction + bitmask check.
- **Dynamic-turtle sprites**: `SETSHAPE "NAME` swaps the 16×16
  TMS9918 hardware sprite that represents the turtle. Built-in shapes:
  `BIRD1`, `BIRD2` (wings up / down), `TURTL` (chunky turtle). First
  `SETSHAPE` flips the VDP into 16×16-sprite mode and erases any
  visible bitmap-triangle turtle so the two render paths don't ghost
  each other. Animations like the classic
  ```
  TO FLY
    SETSHAPE "BIRD1
    FORWARD 4
    SETSHAPE "BIRD2
    FORWARD 4
  END
  REPEAT 30 [FLY]
  ```
  work directly. PU before flying suppresses the trail.
- **PRINT bug fix**: `PRINT "WORD]` now stops on `]` so it doesn't eat
  the closing bracket of an enclosing IF/IFELSE/REPEAT block.

Still planned:

- `LIST` / `WORD` types or at least string variables.
- More commands: arc / dot / fill, color, sprite control.
- Bigger demo, longer help.

The 8 KB build stays frozen at V1.8 and will only receive bug fixes
that don't grow the binary.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
