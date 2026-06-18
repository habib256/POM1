# TMS Plasma — Cruzer / jblang plasma effect (6502 port)

*[← POM1 documentation index](../../../../doc/README.md)*

6502 port of [J.B. Langston's TMS9918 plasma demo](https://github.com/jblang/TMS9918A/blob/master/examples/plasma.asm)
for the P-LAB Apple-1 TMS9918 Graphic Card. The demo data and algorithm
are themselves a Z80 port of *Plascii Petsma* (Cruzer / Camelot, C64 —
[csdb.dk/release/?id=159933](https://csdb.dk/release/?id=159933));
gradient patterns ripped from *Produkthandler Kom Her*
([csdb.dk/release/?id=760](https://csdb.dk/release/?id=760)).

The demo cycles through **all 12 effects × 16 palettes** verbatim from
jblang's `PlasmaParamList` / `ColorPalettes`, switching automatically
every 450 frames (~15 s on POM1). Every cell of the 32×24 character
grid is the sum of 8 sine waves (`StillFrame[y][x] = Σ_{n=0..7}
sine_table[StartAngle[n] + ColFreq[n]·x + RowFreq[n]·y]`); each frame
adds `linear_phase++` so the colour rings ripple outward.

## What it exercises in POM1

- **Mode I (Graphics I) full-screen redraw** — 768 cells written to the
  name table every frame (~14 600 c at 1 MHz, ~85 % of frame budget).
- **Pattern table 256 × 8 B** uploaded once at boot (8 reps of the same
  32-pattern bank).
- **Colour table 32 × 1 B** rewritten on every effect-switch (8 distinct
  pairs × 4 ColorRepeats).
- **Polling V-blank sync** — P-LAB wires /INT → /IRQ (verified by
  Parmigiani), but this loop polls $CC01 by choice and runs as fast as
  the silicon-strict slot table permits.
- **Silicon-strict timing in "Mode I + no sprites"** — `init_vdp_g1`
  disables sprites at boot (Y=$D0 at SAT[0]), which drops the floor
  from 7.5 c (Mode I + sprites) to 6 c. The hot loops run *without*
  `JSR tms9918_pad12` because the gap STA→STA = 19 c is already > 3×
  above floor. Init phases keep `pad12` only inside the lib's
  `init_vdp_g1` / `vdp_set_write` (which can't make the same
  no-sprites assumption).

## What's NOT ported

- Per-row sine warping (`CalcPlasmaFrame` body) — the Z80 original
  generates auto-modifying speed code; the 6502 budget at 1 MHz can't
  match without a similar generator. Linear phase animation only.
- Keyboard interactivity beyond ESC (jblang has `?qhpndavr` + parameter
  modifiers — easy to layer on top later).
- Random parameters / random palette selection.

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM at `$0000-$0FFF`)
- Card: P-LAB TMS9918 Graphic Card
- Recommended POM1 preset: 9 (P-LAB TMS9918 + CodeTank), or the P-LAB
  Multiplexing Fantasy preset 10. The stock 4 KB layout
  works on every TMS9918 preset.

## Memory map

    $0000-$004F  ZP (80 B): pointers, sine accumulators, params copy,
                 palette copy, counters
    $0100-$01FF  6502 stack
    $0280-$0BFF  CODE (~1.4 KB image, 2.4 KB capacity)
                 program + sine_src (64 B) + patterns (256 B) +
                 color_palettes (16 × 8 = 128 B) + per-effect tables
                 (3 × 96 B + 12 B palette index)
    $0C00-$0EFF  plasma_starts (768 B = pre-computed StillFrame)
    $0F00-$0FFF  sine_table (256 B, page-aligned for abs,Y)

## Sources

- `TMS_Plasma.asm` — main entry, all data tables inlined
- `apple1_plasma.cfg` — ld65 config (4 KB stock layout)
- `emit_TMS_Plasma_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/` (m1 + pad)

## Build

    make                      # → ../../../../software/Graphic TMS9918/TMS_Plasma.{bin,txt}

## Run in POM1

1. POM1 → Hardware → plug the TMS9918 card (auto-enabled when loading
   from `software/Graphic TMS9918/`).
2. File → Load → `software/Graphic TMS9918/TMS_Plasma.txt`.
3. Wozmon `\` prompt: type `280R`.
4. Wait ~1 s for `calc_plasma_starts` (~7 frames). The first effect
   uses palette `Pal01` (mostly black + green — jblang's default).
5. Watch the demo cycle through 12 effects (~100 s for a full loop).
   Each effect-switch causes a brief ~0.2 s freeze while
   `calc_plasma_starts` recomputes.
6. ESC returns to Wozmon.

For a faster comparison against the MSX original, POM1 → Hardware →
Speed → MAX (1 M cycles / frame ≈ 50× boost).

## Author / License

VERHILLE Arnaud, 2026. Algorithm and data tables derive from
J.B. Langston (Z80 port, MIT license — see jblang/TMS9918A) and
Cruzer / Camelot (C64 originals — see CSDb releases above).
