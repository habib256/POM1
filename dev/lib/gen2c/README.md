# gen2c/ — C runtime for Uncle Bernie's GEN2 HGR card (cc65)

Minimal C over the GEN2 colour graphics card's **280×192 HIRES** framebuffer.
Pairs with the shared [`apple1c`](../apple1c/) text base, so a GEN2 C program can
draw HIRES *and* print to the WOZ terminal / read the keyboard.

The GEN2 is the Apple II video subsystem on the Apple-1 bus. Mode is driven by
**READ-ONLY** soft switches at `$C250-$C257` (a 1:1 port of Apple II
`$C050-$C057`) — there is **no power-on default**, so a program must select the
mode itself (`gen2_hgr_init()` does this).

## API (`gen2.h`)

| Function | Effect |
|---|---|
| `gen2_hgr_init()`            | graphics + HIRES + page 1 + full screen (call first) |
| `gen2_hgr_clear(fill)`       | fill the `$2000-$3FFF` page (`0` = black) |
| `gen2_hgr_plot(x, y)`        | set a white pixel, `x:0..279 y:0..191` |
| `gen2_hgr_row(y)`            | base address of scanline `y` (Apple II interleave) |
| `gen2_hgr_puts(x, y, s)`     | draw a white string (Beautiful Boot 8×8 font, 16×16 cells) |
| `gen2_hgr_putu(x, y, value)` | draw `value` as unsigned decimal (scores/counters) |
| `gen2_wait_vbl()`            | coarse spin until vertical blank |
| `gen2_text()` … `gen2_hires()` | the eight `$C25x` soft-switch macros |

## Minimal program

```c
#include "gen2.h"
#include "apple1io.h"          /* optional: terminal I/O on GEN2 too */

void main(void) {
    gen2_hgr_init();
    gen2_hgr_clear(0);                       /* black */
    gen2_hgr_puts(42, 80, "HELLO WORLD");
    gen2_hgr_putu(42, 100, 12345u);          /* a number */
    woz_puts((const unsigned char *)"drawn on GEN2\r");
    for (;;) { }
}
```

Build (run `6000R`):

```bash
cl65 -t none -Oirs -C ../../cc65/apple1_gen2_c.cfg -I . -I ../apple1c \
     hello.c gen2.c ../apple1c/apple1io.c ../apple1c/apple1io_asm.s -o hello.bin
```

Or just pick **GEN2 HGR (C)** in the *POM1 Bench* — it wires all of this for you.

## Two gotchas (read these)

1. **Never write the soft switches** (`STA $C25x`). A *read* toggles them and
   returns the H/V-blank flag (HST0) in bit 7; the low 7 bits are an unreliable
   floating bus. The `gen2_*()` macros read, never write.
2. **Don't clear the framebuffer with a naïve 16-bit pointer loop.**
   `for (i = 0; i < 8192; i++) p[i] = 0;` is ~20× slower (it drags in cc65's
   16-bit pointer-increment helper) and blanks the screen for seconds. Use
   `gen2_hgr_clear()`, which fills a page at a time with an 8-bit index (see the
   comment in `gen2.c`).

Full card reference (soft switches, HST0, beam timing): [`doc/GEN2_RELEASE.md`](../../../doc/GEN2_RELEASE.md).
Full C guide: [`dev/Programming_Apple1_C.md`](../../Programming_Apple1_C.md).

## Credit

Beautiful Boot 8×8 font extracted from `dev/lib/hgr/bbfont_cp437.inc`. GEN2 card
by Uncle Bernie. HIRES interleave modelled on cc65's apple2 target.
