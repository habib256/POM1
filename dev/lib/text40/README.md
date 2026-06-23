# lib/text40 — Apple-1 40×24 text-mode UI primitives

*[← dev/lib library hub](../README.md)*

Small, mode-neutral UI helpers (keyboard-layout picker, numbered menu,
repeat-char) factored from the patterns that recur across the text / HGR /
TMS9918 games in this tree. Each `.asm` is a textual `.include` (Model A — no
separate compilation; see the [library hub](../README.md)), builds on
[`../apple1/`](../apple1/)'s `print_str_ax` / `wait_key` / `ECHO`, and parks its
scratch in that directory's [`zp.inc`](../apple1/zp.inc) slot pool.

**Status: available, not yet adopted.** These are extracted, validated helpers
that no shipping project links *yet* — the games they were modelled on still
carry their own inline copies (see *Adoption* below). They are ready to drop in;
the snippet under *Use* is all it takes.

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

The `-I ../../lib/apple1` is mandatory, not optional: every routine here calls
into `print.asm` / `kbd.asm` / `apple1.inc`. `make -C dev/lib check` compiles all
three sources so they can't rot before the first project adopts them.

## Adoption

No shipping project links text40 *yet* — the games these helpers were modelled
on still carry their own inline copies of the same patterns:

- `select_wasd_layout` ⟵ the ~17-line WASD-layout dispatch in the apple1 / gen2 /
  tms9918 Sokoban variants, tms9918 Snake / Galaga, and Connect 4.
- `menu_select` ⟵ the CodeTank menu prompts (now under `dev/projects/codetank/`).
- `repeat_char_ax` ⟵ the grid/border build-up in Little Tower and Connect 4.

Each is correct by inspection and exercised by the `make -C dev/lib check`
compile. Adopting one is a mechanical swap: delete the project's inline copy, add
`.include "layout.asm"` (etc.) plus `-I ../../lib/text40`, and confirm the symbol
names line up with the *Public routines* table above. New 40×24 UI code should
prefer these over hand-rolling the loop again.
