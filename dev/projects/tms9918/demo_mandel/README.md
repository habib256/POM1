# TMS Mandel — Mandelbrot set on TMS9918 Mode II (6502 port)

*[← POM1 documentation index](../../../../doc/README.md)*

6502 port of [J.B. Langston's TMS9918 Mandelbrot demo](https://github.com/jblang/TMS9918A/blob/master/examples/mandel.asm)
for the P-LAB Apple-1 TMS9918 Graphic Card. The fixed-point iteration is
[Rosetta Code's Z80 implementation](https://rosettacode.org/wiki/Mandelbrot_set#Z80_Assembly);
jblang adapted it to TMS9918 + Z180 hardware multiply. This port
re-derives the multiplication strategy for 6502 (no MUL on this CPU)
and re-uses the original's 8-pixel-strip colouring scheme to fit the
Mode II "two colours per 8-pixel row" constraint.

The visible region is identical to jblang's: `x ∈ [-2, +1]`,
`y ∈ [-1.125, +1.125]`, fed at 256×192 with `step = 3` in Q8.8 = 256
columns and 192 rows. **16 iterations** cap (jblang uses 14; 16 buys
slightly more interior detail since 6502 silicon-strict gating
already dominates the render budget).

## What it exercises in POM1

- **Mode II (Graphics II) bitmap, 256×192 pixels** — full pattern table
  ($0000–$17FF, 6 KB) and full colour table ($2000–$37FF, 6 KB) written
  cell-by-cell. 32×24 = 768 cells, 8 patterns + 8 colours per cell.
- **Sprites disabled at boot** — `disable_sprites` writes `Y=$D0` to
  SAT[0]. The TMS9918 silicon-strict slot table relaxes from
  ~12c (Mode II + sprites) to ~6c (Mode II + no sprites), letting the
  hot VDP write loop run with a single `JSR tms9918_pad12` per byte.
- **8-bit unsigned multiply via 512-byte square table** built at boot
  by the recurrence `i² = (i-1)² + (2i - 1)` instead of being baked
  into the binary. Saves 512 B of image and ~3 ms of build time. The
  table drives both general 8x8 multiply (via the `(a+b)/2 - (a-b)/2`
  identity) **and** 16-bit squaring (which is the hot path: 3 squarings
  per Mandelbrot iteration).
- **Squaring-only iteration kernel** — uses `2·z_re·z_im = (z_re + z_im)² − z_re² − z_im²`
  to avoid a signed 16x16 multiply entirely. Each Mandelbrot iteration
  needs only THREE squarings + a handful of 16-bit add/sub. Squaring is
  ~110c per call (vs ~270c for a generic 16x16 mul) → ~410c per iter
  → ~5 000c average per pixel.
- **8-pixel-strip colouring (jblang's `drawpixel`)** — Mode II's
  2-colours-per-8-pixel-row constraint is handled by tracking
  `primary` / `secondary` over the strip. Black (`color = 1`) takes
  over as primary on first encounter, kicking the previous primary
  into secondary and clearing the pattern. Greedy and matches the
  reference exactly.

## Render time

Cell-by-cell raster, top-to-bottom. At 1 MHz silicon-strict:

- First few cell rows (top sky): ~3-5 iter average per pixel → ~5 s/row
- Centre rows (around the bulb): up to 16 iter on interior pixels →
  ~10-15 s/row
- Total: ~3-5 minutes for the full 256×192 image

POM1 → Hardware → Speed → MAX (1 M cycles/frame) shrinks this to
roughly 4-6 seconds — useful for development; the wallclock figure
above is what real silicon would produce.

## Colour mapping (jblang convention)

| `color` | TMS9918 palette | Meaning |
|---------|-----------------|---------|
| 1       | black           | inside the set (never escaped) |
| 2       | medium green    | escaped on iteration 14 (last) |
| ...     | ...             | ... |
| 14      | grey            | escaped on iteration 2 |
| 15      | white           | escaped on iteration 1 (fastest) |

Bright = escaped fast, dark = inside or near-set. This is the inverse
of the typical "iteration-count colour ramp" but matches jblang's
output one-for-one.

## What's NOT ported (vs the Z80 original)

- Z180 hardware multiply path. The 6502 has no MUL, so we always use
  the table approach. (jblang's auto-detect Z80/Z180 split runs ~3×
  faster on Z180; not relevant here.)
- BDOS strings / Z180 register save-restore. Replaced by Wozmon ECHO
  for the boot greeting and a clean `JMP $FF00` on ESC.
- Real-time keypress polling inside the iteration loop. We poll only
  between cells (~64 pixels apart, fast enough for ESC to feel
  responsive).

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM at `$0000-$0FFF`)
- Card: P-LAB TMS9918 Graphic Card
- Recommended POM1 preset: 6 (P-LAB TMS9918 + CodeTank), or the P-LAB
  Multiplexing Fantasy preset 7.

## Memory map

    $0000-$004F  ZP (~80 B): Mandelbrot accumulators, multiply scratch,
                 per-cell rendering state, 8+8 pattern/colour buffers
    $0100-$01FF  6502 stack
    $0280-$0BFF  CODE (~1.5 KB image, 2.4 KB capacity)
    $0C00-$0CFF  sq_lo[256]   (low byte of i², BUILT AT BOOT)
    $0D00-$0DFF  sq_hi[256]   (high byte of i², BUILT AT BOOT)
    $0E00-$0FFF  free

## Sources

- `TMS_Mandel.asm` — main entry, full algorithm
- `apple1_mandel.cfg` — ld65 config (stock 4 KB layout)
- `emit_TMS_Mandel_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/` (m2 + pad)

## Build

    make                      # → ../../../../software/Graphic TMS9918/TMS_Mandel.{bin,txt}

## Run in POM1

1. POM1 → Hardware → plug the TMS9918 card (auto-enabled when loading
   from `software/Graphic TMS9918/`).
2. File → Load → `software/Graphic TMS9918/TMS_Mandel.txt`.
3. Wozmon `\` prompt: type `280R`.
4. Watch the cells fill in, top-to-bottom. The classic Mandelbrot
   silhouette emerges as the central rows render.
5. ESC at any point returns to Wozmon.

POM1 → Hardware → Speed → MAX strongly recommended for development
(50× faster).

## Author / License

VERHILLE Arnaud, 2026. Algorithm derives from Rosetta Code's Z80
Mandelbrot (CC BY-SA) and J.B. Langston's TMS9918 adaptation
(MIT license — see jblang/TMS9918A).
