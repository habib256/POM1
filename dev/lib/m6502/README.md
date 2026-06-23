# lib/m6502 — generic 6502 utilities

*[← dev/lib library hub](../README.md)*

Machine-agnostic helpers usable on any 6502 system. They use `apple1.inc`'s
`ECHO` for output (so today they're tied to Apple-1), but the math/RNG/division
routines have no Apple-1-specific addresses and can be relocated.

**Siblings:** the Apple-1 ROM/PIA base these build on is [`../apple1/`](../apple1/)
(asm) / [`../apple1c/`](../apple1c/) (C). The shared zero-page slot pool and the
two integration models (textual `.include` vs separately-linked `.o`) are owned
by the [library hub](../README.md) — this doc only notes which slots each routine
claims.

## math.asm

Fixed-point trig, 16-bit Galois LFSR, decimal output, modulo-360.

### Public routines

| Symbol                 | Description                                              |
|------------------------|----------------------------------------------------------|
| `roll_lfsr`            | advance 16-bit Galois LFSR (taps `$B400`)                |
| `print_decimal`        | print `arg_lo:arg_hi` as unsigned decimal (1..5 digits)  |
| `div_arg_by_10`        | `arg_lo:hi /= 10` unsigned, returns remainder in A       |
| `mod360_arg`           | `arg_lo:hi mod 360`                                      |
| `mod360_tmp`           | `tmp:tmp2 mod 360`                                       |
| `norm360`              | `th_lo:th_hi mod 360`                                    |
| `signed_sin`           | `A = signed_sin(tmp:tmp2) * 64`, range -64..+64          |
| `mul_dist_by_signed`   | `prod = (arg_lo * A) / 64`, signed                       |
| `negate_prod`          | `prod_lo:prod_hi = -prod_lo:prod_hi`                     |

### Caller responsibilities

`math.asm` owns no state. The caller must declare these zero-page and BSS
slots and `.export` / `.exportzp` them so `math.asm`'s `.import` directives
resolve at link time:

    ; ZP
    tmp, tmp2
    arg_lo, arg_hi, arg2_lo, arg2_hi
    th_lo, th_hi
    ; BSS
    prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi

See `sketchs/tms9918/tool_logo/TMS_Logo_16k.asm` for a working example.

## prng8.asm — 8-bit shift LFSR ($2D tap)

Tiny PRNG used by maze generators. `.include "prng8.asm"` exposes:

| Symbol   | Description                                                      |
|----------|------------------------------------------------------------------|
| `random` | 8-bit shift LFSR ($2D tap). Returns A = new `prng_lo`.           |

Reserves its own ZP slots `prng_lo / prng_hi` via `.ifndef` guard. ZP-tight
projects can alias the slot pair before the include:

    prng_lo = my_lo
    prng_hi = my_hi
    .include "prng8.asm"

Used by: `demo_maze/Maze_Sidewinder`, `demo_maze/Maze2_Backtracker`,
`hgr_maze/HGR_Maze`. Caller must seed the state to nonzero somewhere
(zeroed state stays zero).

## multiply.asm — unsigned shift-and-add multiplies

Standard 6502 shift-and-add multiplies (ref: 6502.org). Promoted out of
`dev/lib/gen2/hgr_tables.inc` so projects that don't need multiply
(HGR_Sierpinski, HGR_TestCard) no longer pay for the ZP slots.

| Symbol  | Description                                                       |
|---------|-------------------------------------------------------------------|
| `umul8` | A = multiplicand, X = multiplier → A = low byte, X = high byte    |
| `umul4` | A = multiplicand (0..15), X = multiplier (0..15) → A = product    |

`umul4` is half the cost of `umul8` when both operands are known to fit
in 4 bits (slope arithmetic, 0..F-bound table indices, percentage
math). It pre-shifts the multiplier into the high nybble so the inner
loop only needs 4 iterations instead of 8; the result fits in one byte
since 15 × 15 = 225 < 256.

Reserves its own ZP slots `mul_tmp / mul_res0` via `.ifndef` guard. Used
by `hgr_mandelbrot` (as building block for an inline 16×16 → 32 fixed-
point multiply that adds its own `mul_res1..3` accumulator slots),
`hgr_house`, `hgr_sokoban`, `hgr_connect4`, `hgr_bbfont_show`.
`umul4` is used by `shadowcast.asm` for its slope
cross-multiplications.

## shadowcast.asm + dungeon.asm — moved to lib/games/rogue

The recursive shadowcasting FOV (`shadowcast.asm`) and the procedural
dungeon-gen primitives (`dungeon.asm`) now live in `dev/lib/games/rogue/`;
include them with `-I ../../lib/games/rogue`. See that directory's README
for the API and ZP usage.

## prng16.asm — 16-bit Galois LFSR ($B400 tap)

Tiny PRNG used by arcade games. `.include "prng16.asm"` exposes:

| Symbol   | Description                                                      |
|----------|------------------------------------------------------------------|
| `prng16` | 16-bit Galois LFSR (taps 16, 14, 13, 11). Returns A = new `prng_lo`. |

Reserves its own ZP slots `prng_lo / prng_hi` via `.ifndef` guard with the
same alias-on-tight-ZP convention as `prng8.asm`. **Same canonical names
as `prng8.asm`** — a project that includes both shares one state pair
physically (you'd typically pick one in practice; the unified naming
simply lets `dev/lib/apple1/zp.inc` declare `prng_lo / prng_hi` once at
`$06/$07` and have both libs find them). Used by: `tms9918_galaga`,
`tms9918_snake`.

A semantically equivalent routine (same $B4 tap, no return value) lives in
`math.asm` as `roll_lfsr` — it operates on `lfsr_lo / lfsr_hi` (BSS, not
ZP) which is a **physically distinct** state pair from `prng_lo/hi`.
Projects already integrated with `math.asm`'s full ZP+BSS convention
(e.g. `tms9918_logo`) can keep using `roll_lfsr`.

## Use

The `.include`-style libs (`multiply.asm`, `prng8.asm`, `prng16.asm`) paste
inline — drop them in and call. A minimal program that rolls a random byte,
squares it, and prints the result as decimal:

```asm
.include "apple1.inc"          ; ECHO
.include "zp.inc"              ; declares mul_tmp/mul_res0 ($04/$05),
                              ;          prng_lo/prng_hi ($06/$07)
.include "multiply.asm"        ; umul8 / umul4
.include "prng16.asm"          ; prng16
.include "print_num.asm"       ; print_byte_dec (from lib/apple1)

reset:  LDA #$A5               ; seed the LFSR — must be nonzero
        STA prng_lo
        STA prng_hi
        JSR prng16             ; A = new prng_lo (a pseudo-random byte)
        TAX                    ; X = multiplier
        JSR prng16             ; advance again for an independent byte
                              ; A = multiplicand, X = multiplier
        JSR umul8              ; A = product low, X = product high
        JSR print_byte_dec     ; print the low byte as "DDD"
        JMP WOZMON
```

`math.asm` is the exception — it carries a CODE segment and `.export`s, so it is
**separately compiled** to `math.o` and put on the linker line (via the project's
`.sketch.json` `extraAsm` or a Makefile `EXTRA_ASM`), not `.include`d. See
`sketchs/tms9918/tool_logo/TMS_Logo_16k.asm` for a worked example.

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/m6502

## Integration with the unified ZP convention

These routines park their scratch in the shared `$00-$07` pool declared by
[`../apple1/zp.inc`](../apple1/zp.inc) — `multiply.asm` claims `mul_tmp /
mul_res0`, the PRNGs claim `prng_lo / prng_hi`, and `math.asm` `.importzp`s
`tmp / tmp2`. Include `zp.inc` once near the top of your `.asm` (before any other
ZP `.res`) and every slot is pre-declared; each lib's `.ifndef` guard then skips
the duplicate allocation. The authoritative slot-by-slot map and the positional
`$00-$07` DANGER contract are owned by [`../apple1/README.md`](../apple1/README.md);
the textual-`.include` vs separately-linked-`.o` split (which decides whether a
lib here is pasted or linked) is in the [library hub](../README.md).
