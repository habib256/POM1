# lib/m6502 — generic 6502 utilities

*[← POM1 documentation index](../../../doc/README.md)*

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

See `dev/projects/tms9918/tool_logo/TMS_Logo.asm` for a working example.

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

## shadowcast.asm — recursive shadowcasting field-of-view

Display-agnostic Björn Bergström FOV (RogueBasin 2002) for grid-based
games: roguelikes, stealth, board games with line-of-sight. Partitions
the plane around the player into 8 octants and processes each one
recursively, narrowing the visible cone every time a wall is encountered.
Result: mathematically symmetric FOV, no slope artifacts, no coverage
holes.

| Symbol               | Description                                            |
|----------------------|--------------------------------------------------------|
| `shadowcast_octants` | Cast FOV from `(player_col, player_row)` over 8 octants|

The lib walks an abstract 1-byte-per-cell map; the caller wires up two
callbacks (`mark_visible_at_cur` and `is_opaque_at_cur`) and supplies
the grid dimensions as `SHADOW_COLS` / `SHADOW_ROWS` constants. All
slope numerators / denominators stay in `0..2*radius+1 ≤ 15`, so slope
comparisons go through `umul4` (caller must `.include "multiply.asm"`
first). Recursion depth ≤ radius (≤ 7 typical), ~11 bytes of frame per
level on the hardware stack — peak ~80 bytes, fits comfortably.

Reserves its own ZP slots `oct_xx/xy/yx/yy`, `oct_idx`, `cast_depth/col/
blocked/start_n,d/end_n,d/save_n,d/lslope_n,d/rslope_n,d/xprod` via
`.ifndef cast_depth` guard. `cur_x/cur_y` are gated by a separate
`.ifndef cur_x` guard so callers that already use them as scratch (e.g.
the rogue's dagger-throw animation) can pre-declare them.

Used by `tms9918_rogue`. See its `compute_fov` wrapper for the canonical
4-phase setup (pick radius, wipe vis_buffer, light player cell,
`JSR shadowcast_octants`, post-pass).

## dungeon.asm — procedural dungeon-gen primitives

Today: just `rand_mod` (uniform `[0, max)` PRNG wrapper around
`prng16`). The bigger BSP-light pattern from `tms9918_rogue` (room
placement, L-corridor with marker tiles, neighbour-based door
classification, three-pass corridor finalisation) stays inline in the
project because it bakes in 16-wide grid math (`AND #$0F` + `LSR×4`
for index → (col, row)) and tile codes. Promotion will land here when
a second rogue / dungeon-crawler shows up — see the lib's header for
the shopping list of parametrisations.

| Symbol     | Description                                                  |
|------------|--------------------------------------------------------------|
| `rand_mod` | A = max → A in `[0, max)`. Clobbers `tmp`; calls `prng16`.   |

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

    .include "math.asm"      ; assembles the module inline

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/m6502

## Integration with the unified ZP convention

The `.include`-style libs in this directory (`multiply.asm`, `prng8.asm`,
`prng16.asm`) compose with `dev/lib/apple1/zp.inc` — include `zp.inc`
once near the top of your `.asm` and the slots `mul_tmp / mul_res0`
(at `$04/$05`) and `prng_lo / prng_hi` (at `$06/$07`) are pre-declared.
The libs' own `.ifndef` guards detect the pre-declaration and skip
duplicate allocation. The same `zp.inc` `.exportzp`s `tmp / tmp2` so
`math.asm` (separately compiled into `math.o`) can `.importzp` them at
link without manual project boilerplate. See
`dev/projects/tms9918/tool_logo/TMS_Logo.asm` for a worked example (math.asm separately compiled, see its Makefile).
