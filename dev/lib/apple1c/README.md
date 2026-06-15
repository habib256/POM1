# apple1c/ — shared Apple-1 text + keyboard I/O for C (cc65)

The **card-neutral base** every Apple-1 C program builds on. Output goes through
the WOZ Monitor `ECHO` routine (`$FFEF`); input reads the PIA keyboard
(`$D010`/`$D011`). It has **no dependency on any graphics card** — the two C
graphics runtimes sit *on top* of this shared base:

```
                 apple1c  (this lib — woz_puts / woz_getkey / woz_mon)
                /        \
   GEN2 HGR (gen2c)       TMS9918 (apple1-videocard-lib: screen1 / tms9918)
   Uncle Bernie           Antonino "Nino" Porcino
```

So a beginner learns **one** text/keyboard API and reuses it whether the program
is plain text, GEN2 HGR colour, or TMS9918 sprites.

## Files

| File | Role |
|---|---|
| `apple1io.h`     | the public API (include this) |
| `apple1io_asm.s` | asm shim — the four routines that call the WOZ ROM directly |
| `apple1io.c`     | the small C layer on top (string + keyboard helpers) |

> **Why `apple1io_asm.s`, not `apple1io.s`?** cc65 compiles `apple1io.c`
> through an intermediate `apple1io.s`; a hand-written `apple1io.s` would be
> overwritten. The `_asm.s` suffix mirrors Nino's `apple1.c` + `apple1_asm.s`.

## API (`apple1io.h`)

| Function | Effect |
|---|---|
| `woz_putc(c)`          | print one character via `ECHO` |
| `woz_puts(s)`          | print a NUL-terminated string |
| `woz_print_hex(b)`     | print a byte as two hex digits |
| `woz_print_hexword(w)` | print a 16-bit word as four hex digits |
| `woz_mon()`            | return control to the WOZ Monitor (`$FF1F`) |
| `apple1_iskeypressed()`| nonzero (bit 7) if a key is waiting |
| `apple1_getkey()`      | **block** until a key, return `key & 0x7F` |
| `apple1_readkey()`     | `0` if no key, else `key & 0x7F` (no wait) |

## Minimal program

```c
#include "apple1io.h"

void main(void) {
    woz_puts((const unsigned char *)"\rHELLO WORLD\r");
    woz_mon();                       /* back to the '\' prompt */
}
```

Build (plain text Apple-1, run `0300R`):

```bash
cl65 -t none -Oirs -C ../../cc65/apple1_c.cfg -I . \
     hello.c apple1io.c apple1io_asm.s -o hello.bin
```

The **POM1 Bench** (*DevBench → POM1 Bench*) links this automatically for the
*Apple-1 text (C)* and *GEN2 HGR (C)* targets, so on GEN2 you can draw HIRES
**and** print to the terminal from the same program.

## Credit

Asm shim + keyboard helpers ported verbatim from
[nippur72/apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib)
(Antonino "Nino" Porcino). Upstream license unspecified at fork time (2026) —
preserve attribution.
