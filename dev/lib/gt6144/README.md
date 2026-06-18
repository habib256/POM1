# lib/gt6144 — SWTPC GT-6144 Graphic Terminal primitives

*[← POM1 documentation index](../../../doc/README.md)*

The Apple-1's first commercial graphics card (1976): 64×96 mono pixels
on six Intel 2102 SRAMs, write-only port at `$D00A`. Replaces the
inline clear + plot logic in `dev/projects/gt6144_hello` and
`gt6144_life`, gives future SWTPC demos a 10-line head-start.

## Files

- **`gt6144.inc`** — `GT_PORT = $D00A`, phase masks (`GT_LATCH_ON`,
  `GT_COMMIT`), control opcodes (`GT_OP_BLANK`, `GT_OP_INVERT`, etc.),
  geometry constants. Pure equates, idempotent.
- **`gt6144.asm`** — plot + clear + control wrappers.

## Public routines

| Routine | Inputs | Output | Clobbers |
|---|---|---|---|
| `gt_clear`     | — | full-screen OFF | A, X, Y |
| `gt_plot_on`   | A=x (0..63), Y=y (0..95) | pixel ON at (x,y) | A; Y preserved |
| `gt_plot_off`  | A=x, Y=y | pixel OFF at (x,y) | A; Y preserved |
| `gt_blank`     | — | framebuffer hidden | A |
| `gt_unblank`   | — | framebuffer shown | A |
| `gt_invert`    | — | video inversion ON | A |
| `gt_normal`    | — | cancel inversion | A |

ZP usage: zero. All routines work directly with registers.

## The 4-phase $D00A protocol

| byte range | meaning |
|---|---|
| `0..63`     | latch X = byte, pixel OFF |
| `64..127`   | latch X = byte − 64, pixel ON |
| `128..223`  | commit Y = byte − 128, draws at (latched X, Y) |
| `224..255`  | control opcode (low 3 bits = cmd: 0=invert, 1=normal, 4=unblank, 5=blank) |

Plot sequence ON: `STA $D00A` with `x | $40`, then `STA $D00A` with
`y | $80`. The 2102 SRAMs hold the framebuffer — power-on bistable
noise is visible until you call `gt_clear`.

## Use

```asm
.include "apple1.inc"
.include "gt6144.inc"
.include "gt6144.asm"

main:
        JSR gt_clear

        ; Plot a diagonal: (0,0), (1,1), ..., (63, 63 mod 96)
        LDA #0
        TAY
@d:     PHA
        JSR gt_plot_on
        PLA
        CLC
        ADC #1
        TAY
        TAX            ; X = next iter index
        CMP #64
        BNE @d

        JMP WOZMON
```

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/gt6144

## Required preset

GT-6144 must be plugged. POM1 presets:

- `--preset 5` — SWTPC GT-6144 with stock Apple-1 (recommended)
- `--enable gt6144` — adds GT-6144 to any other preset

If the card isn't plugged, all `STA $D00A` writes hit RAM at `$D00A`
silently — no error, just no graphics.

## Migration path for existing projects

`gt6144_hello/GT1_Hello.asm` (CLR_SCREEN, ~14 lines) and
`gt6144_life/GT1_Life.asm` (clear_gt, identical). Replace each local
copy with `.include "gt6144.asm"` + `JSR gt_clear`. ~14 lines saved per
project, plus consistency.

## What's NOT in this lib

- **Fonts** — `GT1_Hello` ships a 10-glyph 3×5 font (10 specific letters
  for "APPLE-1 / GT-6144"). A general font would need ~50+ glyphs and
  is project-specific data. Future text-heavy SWTPC demos should ship
  their own font alongside.
- **DRAW_STR / DRAW_CH** — string/glyph plotters. The plot primitives
  here are the building blocks; specific text layout is up to the
  caller.
