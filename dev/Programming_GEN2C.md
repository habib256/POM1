# GEN2 HGR Programming in C (cc65)

The `gen2c` runtime layers a 280×192 colour API on top of the shared
`apple1c` text/keyboard base, so a single `#include "gen2.h"` brings drawing,
blitting, text rendering, line/circle primitives and the soft-switch helpers
into your cc65 program.

**Related docs**

| Doc | Use |
|-----|-----|
| [`Programming_Apple1_C.md`](Programming_Apple1_C.md) | Base cc65 / Apple-1 text + keyboard layer |
| [`Programming_GEN2.md`](Programming_GEN2.md) | 6502 assembly version of this guide |
| [`dev/lib/gen2c/README.md`](lib/gen2c/README.md) | Full `gen2c` API reference + gotchas |
| [`doc/GEN2_RELEASE.md`](../doc/GEN2_RELEASE.md) | Card hardware, soft-switches, beam-race renderer |

---

## 1. GEN2 HGR colour (`gen2c`)

`#include "gen2.h"` — 280×192 HIRES. Full API + gotchas: [lib/gen2c/README.md](lib/gen2c/README.md).

```c
#include "gen2.h"
#include "apple1io.h"
void main(void) {
    gen2_hgr_init();                       /* graphics+hires+page1+full */
    gen2_hgr_clear(0);                      /* black */
    gen2_hgr_puts(42, 80, "SCORE");
    gen2_hgr_putu(120, 80, 1234u);          /* a number */
    for (;;) { }
}
```

Build with `apple1_gen2_c.cfg`, run `6000R`. The two GEN2 rules: never *write*
the `$C25x` soft switches, and always clear with `gen2_hgr_clear()` (a naïve
16-bit loop is ~20× slower and blanks the screen).

## 2. High-level API summary

The `gen2c` header exposes a card-wide set of entry points. The most useful
ones for application code:

| Function | Role |
|----------|------|
| `gen2_hgr_init()` | One-shot card setup (graphics, hires, page 1, full window) |
| `gen2_hgr_clear(fill)` | Fast framebuffer clear (use this — never roll your own loop) |
| `gen2_hgr_fill_pixrect(x,y,w,h)` / `gen2_hgr_clear_pixrect(...)` | Pixel-precise rectangle fill / clear |
| `gen2_hgr_plot(x,y)` / `gen2_hgr_unplot(x,y)` | Single pixel on / off |
| `gen2_hgr_hline` / `gen2_hgr_vline` / `gen2_hgr_line` | Straight-line primitives |
| `gen2_hgr_rect` / `gen2_hgr_circle` / `gen2_hgr_ellipse` | Outlined shapes |
| `gen2_hgr_blit` / `gen2_hgr_blit7` | Copy a byte-block sprite into the framebuffer |
| `gen2_hgr_puts(x,y,s)` / `gen2_hgr_puts8` | Print a string at pixel coordinates |
| `gen2_hgr_puts_color(x,y,s,color)` | Print a string with a colour attribute |
| `gen2_hgr_putu(x,y,v)` / `gen2_hgr_putu_field(...)` / `gen2_hgr_puti(x,y,v)` / `gen2_hgr_putx(x,y,v)` | Print decimal / signed / hexadecimal numbers |
| `gen2_hgr_colorize(x,y,w,...)` | Recolour an existing area (NTSC group flip) |
| `gen2_lores_init` / `gen2_lores_clear` / `gen2_lores_setblock` / `gen2_lores_hlin` / `gen2_lores_vlin` / `gen2_lores_fill_rect` | LORES 40×48 colour blocks |
| `gen2_wait_vbl` | Block until vertical blank |
| `gen2_set_draw_page(p)` / `gen2_show_page()` | Double-buffer flip helpers |
