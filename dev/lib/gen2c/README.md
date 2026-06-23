# gen2c/ — C runtime for Uncle Bernie's GEN2 HGR card (cc65)

*[← dev/lib index](../README.md)*

Minimal C over the GEN2 colour graphics card's **280×192 HIRES** framebuffer.
Pairs with the shared [`apple1c`](../apple1c/) text base, so a GEN2 C program can
draw HIRES *and* print to the WOZ terminal / read the keyboard.

Siblings: the asm support for the same card (equates, beam-sync, HGR tables) is
[`../gen2/`](../gen2/); the card-neutral 2D + numbers + cell-text layer this
runtime forwards to is [`../gfx/`](../gfx/). **Tutorial:** [step-by-step GEN2 HGR C guide](../../../sketchs/doc/Programming_GEN2C.md).

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
refresh at the flip, never a cycle per pixel). Demo: `sketchs/gen2/demo_dbuf`.

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
`gen2_init.c` builds once. **Any project that compiles any of the per-family C
modules (see `gen2c.mk`) must also assemble `gen2_blit.s`** (the per-project
Makefile / emit script and the POM1 Bench already do). Parameters travel
through a zero-page block (`#pragma zpsym`), so no cc65 stack juggling.

### Per-family split (ld65 dead-strip)

The runtime is split into 7 small `.c` modules (`gen2_init.c`, `gen2_pixel.c`,
`gen2_rect.c`, `gen2_text.c`, `gen2_sprites.c`, `gen2_geom.c`, `gen2_lores.c`)
plus a private `gen2_internal.h`. cc65's ld65 strips at the `.o`-file
granularity, not per function — splitting per family lets a text-only demo skip
the pixel + sprite + lores code. `gen2c.mk` exposes the matching `GEN2C_*_SRCS`
variables so a project picks only what it calls. The new `sketchs/gen2/_template_gen2c/`
shows a ~6 KB program using only CORE + TEXT + RECT — about 5 KB smaller than
the same demo with `GEN2C_ALL_SRCS`.

### Build integration (`gen2c.mk`)

A project's Makefile pulls the family source sets from `gen2c.mk` rather than
hand-listing the `.c` modules, so it stays in lockstep with the split. Point
`GEN2C` (and `APPLE1C`) at the lib dirs, `include` the fragment, then compose
`SRCS` from the `GEN2C_*_SRCS` variables for exactly the families you call:

```make
LIBDIR  := ../../../lib
GEN2C   := $(LIBDIR)/gen2c
APPLE1C := $(LIBDIR)/apple1c
GFX     := $(LIBDIR)/gfx
include $(APPLE1C)/apple1c.mk
include $(GEN2C)/gen2c.mk

# Smallest HIRES binary — CORE + TEXT + RECT only:
SRCS := main.c $(GEN2C_CORE_SRCS) $(GEN2C_TEXT_SRCS) $(GEN2C_RECT_SRCS) $(APPLE1C_SRCS)
# …or every family (matches the existing demos):
# SRCS := main.c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS)

INCS := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX)
```

`gen2c.mk` exports: `GEN2C_CORE_SRCS` (always link — tables + soft-switch sink +
draw-page setter + `gen2_blit.s`), `GEN2C_PIXEL_SRCS`, `GEN2C_RECT_SRCS`,
`GEN2C_TEXT_SRCS`, `GEN2C_SPRITES_SRCS`, `GEN2C_GEOM_SRCS`, `GEN2C_LORES_SRCS`,
the `GEN2C_ALL_SRCS` umbrella, and `GEN2C_INCS`. **`GEN2C_GEOM_SRCS` and
`GEN2C_TEXT_SRCS` call `gfx_*`**, so a project that includes either must also
link `gfx-gen2.lib` (`make -C ../gfx gen2`) — see [`../gfx/`](../gfx/). The
generic asm/C build pattern (Makefile skeleton, emit script, linker cfgs) is in
[`../README.md`](../README.md); this section covers only the gen2c-specific
`.mk` knobs.

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

Build (run `6000R`). The `text` and `geom` families call `gfx_*`, so build the
GEN2 gfx archive once and link it (and add `-I ../gfx` for `gfx.h`):

```bash
make -C ../gfx gen2          # builds ../gfx/gfx-gen2.lib (once)

cl65 -t none -Oirs -C ../../cc65/apple1_gen2_c.cfg -I . -I ../apple1c -I ../gfx \
     hello.c gen2_init.c gen2_pixel.c gen2_rect.c gen2_text.c gen2_sprites.c \
     gen2_geom.c gen2_lores.c gen2_blit.s \
     ../apple1c/apple1io.c ../apple1c/apple1io_asm.s ../gfx/gfx-gen2.lib -o hello.bin
```

For minimal binaries, list only the families you actually call (see
`gen2c.mk`). Or just pick **GEN2 HGR (C)** in the *POM1 Bench* — it wires
everything for you.

## Two gotchas (read these)

1. **Never write the soft switches** (`STA $C25x`). A *read* toggles them and
   returns the H/V-blank flag (HST0) in bit 7; the low 7 bits are an unreliable
   floating bus. The `gen2_*()` macros read, never write.
2. **Don't clear the framebuffer with a naïve 16-bit pointer loop.**
   `for (i = 0; i < 8192; i++) p[i] = 0;` is ~20× slower (it drags in cc65's
   16-bit pointer-increment helper) and blanks the screen for seconds. Use
   `gen2_hgr_clear()` (asm in `gen2_blit.s`), which fills a page at a time
   with an 8-bit index.

Card hardware reference (soft switches, HST0, beam timing) lives in the POM1
documentation set; for asm-level detail and the canonical equates see the asm
sibling [`../gen2/`](../gen2/). The shared geometry / number / cell-text logic
this runtime forwards to is documented in [`../gfx/`](../gfx/).

## Source of truth (asm ↔ C)

The soft-switch / framebuffer addresses in `gen2.h` (`GEN2_SS` = `$C250`,
`GEN2_HGR1` = `$2000`, `GEN2_HGR2` = `$4000`) **mirror** the canonical asm
equates in [`../gen2/gen2.inc`](../gen2/gen2.inc) (`GEN2_TEXTOFF`, `GEN2_HGR1/2`).
Edit the `.inc` first; this header follows. Pinned by
`tools/check_lib_equates.py` (`make -C dev/lib check`). The shared font is
likewise generated from the asm master — see Credit below.

## Credit

Beautiful Boot 8×8 font extracted from `dev/lib/gen2/bbfont_cp437.inc` (the shared
font master) by `tools/build_shared_font.py` → `gen2_bbfont.inc`. The same tool
emits the TMS9918 pattern tables from that master, so both cards share one font.
GEN2 card by Uncle Bernie. HIRES interleave modelled on cc65's apple2 target.
