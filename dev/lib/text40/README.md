# lib/text40 — Apple-1 40×24 text-mode UI primitives

*[← POM1 documentation index](../../../doc/README.md)*

Small, mode-neutral UI helpers used by every text/HGR/TMS9918 game in
this tree. Each `.asm` is a textual `.include` (no separate compilation),
follows the `lib/apple1/zp.inc` convention, and lives next to the other
`.include`-style libs.

## Files

- **`layout.asm`** — `prompt_wasd_layout` + `select_wasd_layout`.
  QWERTY/AZERTY keyboard selector. Stores `key_up_code` (W/Z) and
  `key_left_code` (A/Q) in ZP; down='S' / right='D' are universal.
  Replaces 17 lines of dispatch in 6+ projects.
- **`menu.asm`** — `menu_select`. Wait for a digit in `[min..max]`,
  echo it, return. For top-level menus (CodeTank picker, layout
  prompt, in-game options).
- **`repeat.asm`** — `repeat_char_ax`. Print A (low 7 bits) X times
  via ECHO. For grid borders, padding, separators.

## Public routines

| Routine | Inputs | Output | Clobbers | ZP |
|---|---|---|---|---|
| `prompt_wasd_layout` | — | `key_up_code`/`key_left_code` set | A | `key_up_code`, `key_left_code` |
| `select_wasd_layout` | — | same | A | same |
| `menu_select` | A=min, X=max digit | A=key | A | `tmp`, `tmp2` |
| `repeat_char_ax` | A=char, X=count | X=0 | A | none |

## ZP convention

- `key_up_code` / `key_left_code` (1 byte each) — owned by `layout.asm`,
  guarded by `.ifndef`. Allocate via `zp.inc` extension OR alias to
  existing scratch in tight projects.
- `tmp` / `tmp2` — used by `menu.asm`, expected from
  `lib/apple1/zp.inc` (or caller-declared).

## Use

```asm
.include "apple1.inc"
.include "zp.inc"
.include "print.asm"
.include "kbd.asm"
.include "layout.asm"
.include "menu.asm"
.include "repeat.asm"

main:
        ; --- Banner + layout pick ---
        LDA #<msg_banner
        LDX #>msg_banner
        JSR print_str_ax
        JSR prompt_wasd_layout      ; sets key_up_code + key_left_code

        ; --- Menu pick (1..3) ---
        LDA #<msg_menu
        LDX #>msg_menu
        JSR print_str_ax
        LDA #'1'
        LDX #'3'
        JSR menu_select             ; A = '1', '2', or '3'

        ; --- Repeat-char border ---
        LDA #'-'
        LDX #40
        JSR repeat_char_ax          ; "----------------------------------------"
```

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/text40

## Validation

**Status: not yet adopted by any shipping project.** The CodeTank menus
(`sketchs/tms9918/*_menu*/`) and the WASD games still carry their own
inline copies of these patterns; `menu_select`, `repeat_char_ax` and
`select_wasd_layout` are correct by inspection but a future migration will
replace each project's inline copy with `.include "layout.asm"` (etc.).
