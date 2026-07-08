# lib/text40 — Apple-1 40×24 text-mode UI primitives

*[← dev/lib library hub](../README.md)*

*Tutorial: [step-by-step Apple-1 assembly guide](../../../sketchs/doc/Programming_Apple1_ASM.md).*

Small, mode-neutral UI helpers (numbered menu, repeat-char) factored from the
patterns that recur across the text / HGR / TMS9918 games in this tree. Each `.asm` is a textual `.include` (Model A — no
separate compilation; see the [library hub](../README.md)), builds on
[`../apple1/`](../apple1/)'s `print_str_ax` / `wait_key` / `ECHO`, and parks its
scratch in that directory's [`zp.inc`](../apple1/zp.inc) slot pool.

**Status: adopted.** `menu_select` is linked by both shipping CodeTank launcher
menus (`dev/codetank/game1_menu/` + `demos_menu/` — the ARCADE and
DEMOS cartridges); see *Adoption* below for the per-helper history. The snippet
under *Use* is all it takes to drop one into a new project.

## Files

- **`menu.asm`** — `menu_select`. Wait for a digit in `[min..max]`,
  echo it, return. For top-level menus (CodeTank picker, in-game
  options).
- **`repeat.asm`** — `repeat_char_ax`. Print A (low 7 bits) X times
  via ECHO. For grid borders, padding, separators.

## Public routines

| Routine | Inputs | Output | Clobbers | ZP |
|---|---|---|---|---|
| `menu_select` | A=min, X=max digit | A=key | A | `tmp`, `tmp2` |
| `repeat_char_ax` | A=char, X=count | X=0 | A | none |

## ZP convention

- `tmp` / `tmp2` — used by `menu.asm`, expected from
  `lib/apple1/zp.inc` (or caller-declared).

## Use

```asm
.include "apple1.inc"
.include "zp.inc"
.include "print.asm"
.include "kbd.asm"
.include "menu.asm"
.include "repeat.asm"

main:
        ; --- Banner ---
        LDA #<msg_banner
        LDX #>msg_banner
        JSR print_str_ax

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

- `menu_select` — **adopted juillet 2026** by the two CodeTank launcher menus
  (`dev/codetank/game1_menu/codetank_menu.asm` +
  `demos_menu/codetank_demos_menu.asm`, shipped in the ARCADE and DEMOS
  cartridges). Their cfgs have no ZEROPAGE segment (tiny ROM stubs), so they
  alias `tmp`/`tmp2` onto $00/$01 with plain equates instead of `zp.inc` —
  the supported stand-alone mode documented in `menu.asm`'s header. Net
  effect vs the old inline loops: out-of-range keys are rejected by bounds
  check instead of an exact-match list, and the chosen digit is echoed.
- `repeat_char_ax` — modelled on Little Tower's `GRAPH`/`GRAPHIT` subroutine,
  which turned out to be dead code (never called) and was deleted from
  `LittleTower-1.0.asm` in juillet 2026; Connect 4's `+---+` separator is a
  static string a repeat loop can't produce. So the helper currently has no
  shipping consumer — it is for NEW grid/border code.

(The former `layout.asm` — the QWERTY/AZERTY WASD-layout selector — was retired
in juillet 2026 when every game switched to fixed IJKL controls, the same
physical keys on both layouts.)

Everything here is exercised by the `make -C dev/lib check` compile *and* (for
`menu_select`) by the CodeTank ROM gate (`tools/verify_codetank_roms.py`).
Adopting a helper is a mechanical swap: delete the project's inline copy, add
`.include "menu.asm"` (etc.) plus `-I ../../lib/text40`, and confirm the symbol
names line up with the *Public routines* table above. New 40×24 UI code should
prefer these over hand-rolling the loop again.
