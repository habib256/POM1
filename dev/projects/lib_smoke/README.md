# LibSmoke — unified ZP convention validation

This project is **not a user program**. It exists to validate that
`dev/lib/apple1/zp.inc` and the factored helpers (`kbd.asm`, `delay.asm`,
`print_num.asm`, the corrected `WOZMON = $FF1A` equate) compose correctly
across both integration paths used by the libraries:

- **Textual `.include`** — `print.asm`, `kbd.asm`, `delay.asm`,
  `print_num.asm`, `multiply.asm`, `prng8.asm`, `prng16.asm` are pulled
  into `LibSmoke.o` directly.
- **Cross-object `.importzp` / `.import`** — `math.asm` is compiled
  separately into `math.o` and linked. Its `print_decimal` routine reads
  `arg_lo:arg_hi` via ZP slots whose addresses must be resolved by `ld65`
  from this project's exports. If `zp.inc` skips an `.exportzp`, the link
  fails with `Unresolved external symbol: tmp` and the bug is caught at
  build time.

## Build

```
make
```

Produces `software/dev/LibSmoke.bin` (~970 bytes) and the matching Woz hex
`.txt` for paste-load.

## Run

In POM1, stock Apple-1 preset (#0):

```
File > Load Memory → software/dev/LibSmoke.txt
280R
```

Expected output:

```
LIB SMOKE — UNIFIED ZP CONVENTION
PRESS ANY KEY: <key echoes back>
PRINT_BYTE_DEC(42) = 042
UMUL8(6,7) = 042
PRNG8 SAMPLE = <some 0..255 value>
PRNG16 SAMPLE = <some 0..255 value>
PRINT_DECIMAL(1234) = 1234
DELAY 1S... <~1 second pause> DONE
\
```

The trailing `\` + cursor is Wozmon's prompt — proof that `JMP WOZMON`
landed at `$FF1A` (which prints `\` + CR before falling through to the
line editor) rather than the silent `$FF1F`.

## What each step proves

| Step | Library exercised | Convention point validated |
|---|---|---|
| Banner | `print.asm` (`print_str_ax`) | `print_ptr_lo/hi` resolved via `zp.inc` |
| Key prompt | `kbd.asm` (`wait_key`) | No-ZP routine works through textual include |
| `print_byte_dec(42)` | `print_num.asm` | 3-digit decimal output, leading zeros |
| `umul8(6,7)` | `multiply.asm` | `mul_tmp/mul_res0` slots from `zp.inc` |
| `prng8` | `prng8.asm` (`random`) | `prng_lo/hi` slots from `zp.inc` |
| `prng16` | `prng16.asm` | Same `prng_lo/hi` slots — unification |
| `print_decimal(1234)` | `math.asm` (cross-object) | `.importzp` / `.exportzp` round-trip |
| 1 s delay | `delay.asm` | `delay_ms_a` calibration at 1.022 MHz |
| Return to Wozmon | `apple1.inc` (`WOZMON`) | `$FF1A` entry, prompt visible |

## Cleaning up

`make clean` removes the local `.o` files. The output `.bin` / `.txt` in
`software/dev/` are kept (they're part of the shipped tree for users who
want to re-validate the convention without rebuilding cc65 themselves).
