# lib/m6502 — generic 6502 utilities

Machine-agnostic helpers usable on any 6502 system. They use `apple1.inc`'s
`ECHO` for output (so today they're tied to Apple-1), but the math/RNG/division
routines have no Apple-1-specific addresses and can be relocated.

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

See `dev/projects/tms9918_logo/TMS_Logo.asm` for a working example.

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

Used by: `games_maze/Maze_Sidewinder`, `games_maze/Maze2_Backtracker`,
`hgr_maze/HGR_Maze`. Caller must seed the state to nonzero somewhere
(zeroed state stays zero).

## multiply.asm — unsigned 8×8 → 16-bit multiply

Standard 6502 shift-and-add multiply (ref: 6502.org). Promoted out of
`dev/lib/hgr/hgr_tables.inc` so projects that don't need multiply
(HGR2_Sierpinski, HGR3_TestCard) no longer pay for the ZP slots.

| Symbol  | Description                                                       |
|---------|-------------------------------------------------------------------|
| `umul8` | A = multiplicand, X = multiplier → A = low byte, X = high byte    |

Reserves its own ZP slots `mul_tmp / mul_res0` via `.ifndef` guard. Used
by `hgr4_mandelbrot` (as building block for an inline 16×16 → 32 fixed-
point multiply that adds its own `mul_res1..3` accumulator slots),
`hgr5_house`, `hgr6_sokoban`, `hgr7_connect4`, `hgr8_bbfont_show`,
`hgr9_smiley16`.

## prng16.asm — 16-bit Galois LFSR ($B400 tap)

Tiny PRNG used by arcade games. `.include "prng16.asm"` exposes:

| Symbol   | Description                                                      |
|----------|------------------------------------------------------------------|
| `prng16` | 16-bit Galois LFSR (taps 16, 14, 13, 11). Returns A = new `seed_lo`. |

Reserves its own ZP slots `seed_lo / seed_hi` via `.ifndef` guard with the
same alias-on-tight-ZP convention as `prng8.asm`. Used by:
`tms9918_galaga`, `tms9918_snake`.

A semantically equivalent routine (same $B4 tap, no return value) lives in
`math.asm` as `roll_lfsr` — projects already integrated with `math.asm`'s
full ZP convention (e.g. `tms9918_logo`) can keep using that.

## Use

    .include "math.asm"      ; assembles the module inline

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/m6502
