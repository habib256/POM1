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
| `gen2_hgr_fill_rect(y0,rows,col0,ncols,val)` | **asm** byte-fill a rectangle (fast targeted erase) |
| `gen2_hgr_fill_pixrect(x,y,w,h)` / `gen2_hgr_clear_pixrect(...)` | **asm** fill / erase a PIXEL rectangle (solid blocks/sprites) |
| `gen2_hgr_plot(x, y)` / `gen2_hgr_unplot(x, y)` | set / clear a white pixel, `x:0..279 y:0..191` — **asm** |
| `gen2_hgr_row(y)`            | base address of scanline `y` (Apple II interleave) |
| `gen2_hgr_blit(x,y,w,h,bmp,mode)` | 1bpp sprite at any x, `GEN2_SET/CLEAR/XOR` (XOR twice = flicker-free erase) — pixel-walk |
| `gen2_hgr_blit7(x,y,wbytes,h,bmp,mode)` | **fast** byte-aligned blit (sprite pre-packed 7px/byte; x snaps to 7px) — ~7× fewer ops for big solid sprites |
| `gen2_hgr_puts(x, y, s)`     | draw a white string (Beautiful Boot 8×8 font, 16×16 doubled cells) — **asm** |
| `gen2_hgr_puts8(x, y, s)` / `gen2_hgr_putu8(...)` | same font at **native 8×8** (dense HUDs / status lines) — **asm** |
| `gen2_hgr_puts_color(x,y,s,c)` | draw a string in an NTSC artifact colour (`GEN2_VIOLET/GREEN/ORANGE/BLUE`) — **asm** |
| `gen2_hgr_putu(x, y, value)` | draw `value` as unsigned decimal (scores/counters) — **asm** |
| `gen2_hgr_putu_field(x,y,value,width)` | fixed-width, right-aligned decimal that **self-erases its field** (flicker-free HUD counter) |
| `gen2_hgr_puti(x, y, value)` / `gen2_hgr_putx(x, y, value)` | signed decimal / uppercase hex |
| `gen2_hgr_hline(x0,x1,y)` / `gen2_hgr_vline(x,y0,y1)` | horizontal / vertical run (inclusive, fast pixrect path) |
| `gen2_hgr_line(x0,y0,x1,y1)` | Bresenham line (auto-shortcuts straight runs) |
| `gen2_hgr_rect(x0,y0,x1,y1)` / `gen2_hgr_circle(xc,yc,r)` | rectangle outline / midpoint circle |
| `gen2_wait_vbl()`            | coarse spin until vertical blank |
| `gen2_set_draw_page(p)` / `gen2_show_page()` | **double buffering**: pick the draw page (1/2), then flip the display to it |
| `gen2_text()` … `gen2_hires()` | the eight `$C25x` soft-switch macros |

### Double buffering (PAGE2)

The card has two framebuffers — page 1 (HIRES `$2000` / LORES `$0400`) and page 2
(HIRES `$4000` / LORES `$0800`). For tear-free full-screen animation, draw the
next frame into the hidden page, then flip:

```c
unsigned char draw = 2u;
gen2_hgr_init();
for (;;) {
    gen2_set_draw_page(draw);     /* every primitive now writes the hidden page */
    gen2_hgr_clear(0u);
    /* ...render the frame... */
    gen2_show_page();             /* the freshly-drawn page goes live ($C254/5) */
    draw = (draw == 1u) ? 2u : 1u;
}
```

`gen2_set_draw_page` redirects **all** primitives (HIRES + LORES) by re-deriving
the scanline tables they index — so it is set **once per frame**, not per call,
and the per-pixel hot paths stay byte-for-byte identical (the page costs a table
refresh at the flip, never a cycle per pixel). Demo: `dev/projects/gen2_dbuf_demo`.

### Assembly fast paths (`gen2_blit.s`)

The text and erase hot paths run in hand-written 6502 (the C alone computed
`x/7` / `x%7` software divisions per pixel — no hardware DIV on the 6502 — which
cost millions of cycles for one line of text):

- **`gen2_blit_glyph`** — the inner loop of `gen2_hgr_puts`/`putu`. The C wrapper
  passes the glyph's starting byte column + bit mask (one division per *glyph*),
  then the asm walks the 16 doubled pixels of each row advancing the column/mask
  incrementally — zero per-pixel division. ~10× faster text.
- **`gen2_fill_rect_asm`** — the inner loop of `gen2_hgr_fill_rect`: stores `val`
  into a rectangle of whole framebuffer bytes. Erasing the band behind a digit
  this way beats per-pixel `gen2_hgr_unplot` by a wide margin.
- **`gen2_plot_asm` / `gen2_unplot_asm`** — the bodies of `gen2_hgr_plot` /
  `gen2_hgr_unplot`. The byte column (`x/7`) and bit mask (`1<<x%7`) come from the
  `gen2_col7` / `gen2_mask7` lookup tables instead of per-pixel division — ~4×
  faster per pixel.
- **`gen2_pixrect_asm`** — behind `gen2_hgr_fill_pixrect` / `clear_pixrect`. Draws
  or erases a whole PIXEL rectangle in one call: a left partial byte, a tight
  `STA` run of full bytes, and a right partial byte, per scanline. The C side just
  clips and passes `x/xr/y/h/mode`; the asm derives columns + edge masks. **~10×
  faster than a per-pixel `gen2_hgr_plot` double loop** — a 6×6 block is one call
  instead of 36. This is the primitive for block/tile/sprite games like Snake.
- **`gen2_colorize_asm`** — behind `gen2_hgr_puts_color`. GEN2 HIRES has NO
  per-pixel colour; colour is an NTSC artifact of the bit pattern + the byte's
  high bit. So colour text is drawn WHITE, then this pass rewrites each byte
  `b = (b & carrier[col&1]) | hibit`, masking it down to the colour's carrier.
  The 4 reachable colours (verified against the renderer): **violet/mauve**
  (`even=$55 odd=$2A`), **green** (`$2A/$55`), **orange** (green `| $80`), **blue**
  (violet `| $80`). There is no red — orange is the warm tone.

These read scanline base addresses from the `gen2_rowlo`/`gen2_rowhi` tables —
and `plot`/`unplot` also the `gen2_col7`/`gen2_mask7` x-lookup tables — that
`gen2.c` builds once. **Any project that compiles `gen2.c` must also assemble
`gen2_blit.s`** (the per-project Makefile / emit script and the POM1 Bench
already do). Parameters travel through a zero-page block (`#pragma zpsym`), so
no cc65 stack juggling.

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
     hello.c gen2.c gen2_blit.s ../apple1c/apple1io.c ../apple1c/apple1io_asm.s -o hello.bin
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
