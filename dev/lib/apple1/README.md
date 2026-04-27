# lib/apple1 — Apple-1 hardware equates + tiny shared routines

Equates for Apple-1 ROM + PIA 6821 I/O (`apple1.inc` — pure equates, no
segments) plus tiny utility routines used across many projects (one
`.asm` per routine, drop in via `.include`).

## Files

- **`apple1.inc`** — hardware equates. See public-symbols table below.
- **`print.asm`** — `print_str_ax`: print NUL-terminated ASCIIZ string
  via Wozmon `ECHO` ($FFEF). Owns its own 2-byte ZP slot pair
  (`print_ptr_lo / print_ptr_hi`). Caller must already have `ECHO`
  defined in scope (either inline `ECHO = $FFEF` or via
  `.include "apple1.inc"`); the module deliberately does not
  re-include `apple1.inc` to avoid duplicate-symbol errors in projects
  that declare ECHO inline.

  Use:

      LDA #<msg
      LDX #>msg
      JSR print_str_ax

  Drop into your `.asm` with `.include "print.asm"`.

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
| `WOZ_RST`  | `$FF1F` | Wozmon reset entry (warm restart)            |
| `GETLINE`  | `$FF1F` | alias for legacy code                        |

## Use

In your `.asm`:

    .include "apple1.inc"
    LDA #'A' | $80
    JSR ECHO

In your project Makefile, pass the include path to `ca65`:

    LIB := -I ../../lib/apple1

Every Apple-1 program in `dev/projects/` imports this header.
