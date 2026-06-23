# lib/apple1 — Apple-1 hardware equates + shared primitives

*[← dev/lib library hub](../README.md)*

Equates for Apple-1 ROM + PIA 6821 I/O (`apple1.inc` — pure equates, no
segments) plus small utility routines used across most projects.
Drop each `.asm` in via `.include`; pure-data `.inc` files via the same.

**Siblings:** [`../apple1c/`](../apple1c/) is the cc65 **C** mirror of this
base; [`../m6502/`](../m6502/) adds machine-agnostic math / PRNG that share
this directory's [`zp.inc`](zp.inc) slot pool.

## ZP convention — `zp.inc` (opt-in)

`zp.inc` declares the canonical 8-byte ZP slot pool used by all the
`.include`-style libs in this directory plus `lib/m6502/`, **and** emits
`.exportzp` so separately-compiled libs (`lib/m6502/math.asm`,
`lib/tms9918/tms9918m2.asm`) can `.importzp` the same names at link.
One include unifies both integration paths.

Layout (8 bytes from $00, declaration order):

| Address | Symbol | Owner |
|---|---|---|
| `$00` | `tmp` | scratch — `kbd.asm`, `math.asm`, `tms9918m2.asm` |
| `$01` | `tmp2` | scratch — `math.asm`, `tms9918m2.asm` |
| `$02` | `print_ptr_lo` | `print.asm` |
| `$03` | `print_ptr_hi` | `print.asm` |
| `$04` | `mul_tmp` | `lib/m6502/multiply.asm` |
| `$05` | `mul_res0` | `lib/m6502/multiply.asm` |
| `$06` | `prng_lo` | `lib/m6502/prng8.asm` + `prng16.asm` |
| `$07` | `prng_hi` | same |

Usage:

```asm
.include "apple1.inc"
.include "zp.inc"          ; first thing in your ZP region

; ... your project's own ZP slots from $08 onwards ...
```

Rules:

1. `.include "zp.inc"` must come **before** any other ZP allocation,
   otherwise the layout shifts away from `$00`.
2. Aliases for ZP-tight projects (Sokoban, Galaga) go **before** the
   include. The per-slot `.ifndef` guards detect a pre-existing alias
   and skip the duplicate `.res 1`:
   ```asm
   str_lo:  .res 1
   str_hi:  .res 1
   print_ptr_lo = str_lo
   print_ptr_hi = str_hi
   .include "zp.inc"             ; reuses str_lo/str_hi for print
   ```
3. `zp.inc` deliberately does **not** include `apple1.inc` — keep it
   pure ZP allocation so projects that already declare hardware equates
   inline don't get duplicate-symbol errors.

Heavy libs (`math.asm`, `tms9918m2.asm`) own additional ZP slots beyond
these 8 bytes; see their READMEs for the extra symbols you must
`.exportzp` in your project.

## Files

- **`apple1.inc`** — hardware equates. See public-symbols table below.
- **`zp.inc`** — shared ZP slot pool (see *ZP convention* above).
- **`print.asm`** — `print_str_ax`: print NUL-terminated ASCIIZ string
  via Wozmon `ECHO` (`$FFEF`). Uses `print_ptr_lo / print_ptr_hi` (from
  `zp.inc` if used, else allocated locally via `.ifndef`).
- **`kbd.asm`** — `wait_key` (blocking) and `poll_key` (non-blocking).
  No ZP usage. Replaces 15+ inline copies across projects.
- **`delay.asm`** — `delay_ms_a`: A = ms count, ~1 ms per unit at the
  Apple-1's 1.022727 MHz clock. No ZP. A = 0 wraps to 256 ms.
- **`print_num.asm`** — `print_byte_dec`: A = byte → prints exactly
  three ASCII decimal digits (`"000"`..`"255"`) via ECHO. No ZP.

## apple1.inc — Public symbols

| Symbol     | Address | Description                                  |
|------------|---------|----------------------------------------------|
| `ECHO`     | `$FFEF` | Wozmon character output (`A = char | $80`)   |
| `PRBYTE`   | `$FFDC` | Wozmon: print A as two hex digits            |
| `PRHEX`    | `$FFE5` | Wozmon: print low nibble of A as one hex     |
| `KBD`      | `$D010` | PIA: keyboard data (read clears strobe)      |
| `KBDCR`    | `$D011` | PIA: keyboard control (bit 7 = key ready)    |
| `DSP`      | `$D012` | PIA: display data + busy (bit 7)             |
| `DSPCR`    | `$D013` | PIA: display control                         |
| `WOZMON`   | `$FF1A` | Wozmon prompt entry — prints `\` + CR        |
| `WOZ_RST`  | `$FF1F` | Wozmon warm-restart (post-prompt, silent)    |

**`WOZMON` vs `WOZ_RST`** — the canonical "return control to the
monitor" target is `JMP WOZMON` (`$FF1A`). It prints `\` + CR before
falling through to the line editor, giving the user a visible "I'm
back" signal. `WOZ_RST` (`$FF1F`) skips the prompt — looks like a hang
from the user's POV. Use `WOZ_RST` only for warm-restart paths where
the program has just printed its own status line. The previous
`GETLINE = $FF1F` alias was misleadingly named (`$FF1F` is **not**
GETLINE) and has been removed; rename any usage to `WOZ_RST`.

## Public routines — quick reference

| Routine | Module | Inputs | Outputs | Clobbers | ZP |
|---|---|---|---|---|---|
| `print_str_ax` | `print.asm` | A=lo, X=hi (ptr to ASCIIZ) | — | A, Y | `print_ptr_*` |
| `wait_key` | `kbd.asm` | — | A = key & $7F | A | none |
| `poll_key` | `kbd.asm` | — | A = key & $7F or 0; Z reflects | A | none |
| `delay_ms_a` | `delay.asm` | A = ms (1..255; 0→256) | — | A, X, Y | none |
| `print_byte_dec` | `print_num.asm` | A = byte | "DDD" via ECHO | A, X | none |

## Use

In your `.asm`:

```asm
.include "apple1.inc"
.include "zp.inc"
.include "print.asm"
.include "kbd.asm"
.include "delay.asm"
.include "print_num.asm"

main:
        LDA #<msg
        LDX #>msg
        JSR print_str_ax
        JSR wait_key
        LDA #100
        JSR delay_ms_a
        JMP WOZMON

msg:    .byte "HELLO!", $0D, 0
```

In your project Makefile:

    LIB := -I ../../lib/apple1

Validation: every routine here is exercised by shipping projects under
`sketchs/apple1/` (e.g. `game_sokoban`, `game_chess`) and by
`tools/test_*.py` smoke harnesses.

## Source of truth (asm ↔ C)

The Apple-1 hardware addresses live in **two** tracks: the asm equates in
**`apple1.inc`** (`ECHO`, `KBD`, `KBDCR`, …) and the cc65 mirror in
[`../apple1c/apple1io.h`](../apple1c/apple1io.h) (`ECHO`, `KBD_DATA`,
`KBD_CTRL`). **`apple1.inc` is canonical** — lowest level, closest to the bus.
Edit it first, then sync the C header. `tools/check_lib_equates.py` (run by
`make -C dev/lib check`) fails if the two ever disagree.
