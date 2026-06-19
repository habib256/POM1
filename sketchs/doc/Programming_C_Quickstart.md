# Apple-1 C Programming — Quickstart cheat sheet

You write C. The **cc65** cross-compiler turns it into a 6502 binary. POM1
runs it. This page gives you the smallest path to a running program for
each target, the function-chooser tables you need most days, and the
pitfalls that bite hardest. Reference docs (Programming_Apple1_C.md,
Programming_GEN2C.md, Programming_TMS9918C.md) are linked at the end.

---

## 30-second decision

| You want… | Copy this folder | Linker preset | Read this header |
|---|---|---|---|
| Plain text I/O (Wozmon) | `sketchs/apple1/_template/` | `apple1_c.cfg` | `apple1c.h` |
| GEN2 HGR colour graphics | `sketchs/gen2/_template_gen2c/` | `apple1_gen2_c.cfg` | `gen2.h` |
| TMS9918 sprites / colour | `sketchs/tms9918/_template_tms9918c/` | `codetank_c.cfg` | `tms9918c.h` |

Then `make` in the copy. The Makefile already has the right cc65 flags,
linker config and the **per-family** library variables (only the .o files
you call are linked).

> Note: `sketchs/apple1/_template/`'s default `make` builds the **asm** `Hello.asm`
> (linked with `apple1_4k.cfg`). For a C program use `apple1_c.cfg` (the C
> runtime config) — see the `cl65` line in [`doc/SKETCHS.md`](SKETCHS.md).

---

## Hello, world — three programs side by side

### Text (apple1c)

```c
#include "apple1c.h"     /* umbrella: woz_puts, apple1_getkey, puts_apple1 macro */

void main(void)
{
    puts_apple1("\rHELLO WORLD (C)\r");
    woz_mon();
}
```

### GEN2 HGR colour

```c
#include "gen2.h"        /* pulls in apple1c.h automatically */

void main(void)
{
    gen2_hgr_init();                                /* graphics + hires + page 1 */
    gen2_hgr_clear(0);
    gen2_hgr_puts_color(20, 30, "HELLO GEN2", GEN2_VIOLET);
    gen2_hgr_fill_pixrect(40, 80, 100, 20);
    gen2_hgr_colorize(40, 80, 100, 20, GEN2_GREEN);
    (void)apple1_getkey();
    woz_mon();
}
```

### TMS9918 sprite (shadow workflow)

```c
#include "tms9918c.h"    /* umbrella: screen1/screen2/sprites/shadow/utils/apple1 */

static const unsigned char pat[8] = { 0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0xFF };

void main(void)
{
    tms_sprite s;
    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_CYAN);
    screen1_prepare();  screen1_load_font();
    tms_set_vram_write_addr(TMS_SPRITE_PATTERNS);
    tms_copy_to_vram(pat, sizeof pat, TMS_SPRITE_PATTERNS);
    tms_shadow_init();
    s.y = 80; s.x = 120; s.name = 0; s.color = COLOR_WHITE;
    tms_shadow_set(0, &s);  tms_shadow_set_terminator(1);  tms_shadow_flush();
    (void)apple1_getkey();
    woz_mon();
}
```

---

## Function-chooser — pick the right helper

### `apple1c` (text + keyboard)

| Need | Call |
|---|---|
| Print a string | `puts_apple1(s)` (macro; no cast needed) |
| Print + newline | `println_apple1(s)` |
| Print one char | `woz_putc(c)` |
| Print byte as hex | `woz_print_hex(c)` |
| Print word as hex | `woz_print_hexword(w)` |
| Block and wait for a key | `apple1_getkey()` or `getchar_apple1()` |
| Poll without waiting | `apple1_readkey()` returns 0 if none |
| Just test "is a key ready?" | `apple1_iskeypressed()` (bit-7 strobe) |
| Drop back to Wozmon prompt | `woz_mon()` |

### `gen2c` (GEN2 HGR colour graphics)

| Need | Call | Notes |
|---|---|---|
| Init | `gen2_hgr_init()` | graphics + hires + page 1 + full |
| Clear / fill all | `gen2_hgr_clear(fill)` | fill = 0 black, 0x7F white |
| One pixel | `gen2_hgr_plot(x, y)` / `gen2_hgr_unplot(x, y)` | x = 0..279, y = 0..191 |
| Rectangle fill (pixels) | `gen2_hgr_fill_pixrect(x, y, w, h)` | the default choice |
| Rectangle clear (pixels) | `gen2_hgr_clear_pixrect(x, y, w, h)` | |
| Rectangle fill (byte cols) | `gen2_hgr_fill_rect(y0, rows, col0, ncols, val)` | only if you already think in 7-px byte columns |
| Text 16x16 doubled | `gen2_hgr_puts(x, y, s)` | OR-drawn |
| Coloured text | `gen2_hgr_puts_color(x, y, s, GEN2_VIOLET\|GREEN\|ORANGE\|BLUE)` | NTSC artefact colours |
| Text 8x8 native | `gen2_hgr_puts8(x, y, s)` | 3-4x denser, faster |
| HUD number, fixed width | `gen2_hgr_putu_field(x, y, value, width)` | erases its own field — flicker-free |
| Number, transparent | `gen2_hgr_putu(x, y, v)` / `gen2_hgr_puti` (signed) / `gen2_hgr_putx` (hex) | |
| Tint a region | `gen2_hgr_colorize(x, y, w, h, GEN2_GREEN)` | call AFTER drawing in white |
| Sprite blit, any width | `gen2_hgr_blit(x, y, w, h, bitmap, mode)` | mode = GEN2_SET / CLEAR / XOR |
| Sprite blit, 7-px snap, fast | `gen2_hgr_blit7(x, y, wbytes, h, src, mode)` | only when sprite is pre-packed |
| Line / circle / rectangle outline / ellipse | `gen2_hgr_line(...)`, `gen2_hgr_rect`, `gen2_hgr_circle`, `gen2_hgr_ellipse` | shared `gfx_*` layer |
| Double-buffer | `gen2_set_draw_page(p)` then draw, then `gen2_show_page()`, then `GEN2_FLIP_PAGE(p)` | |
| Wait for VBlank | `gen2_wait_vbl()` | |

### `tms9918c` (P-LAB TMS9918)

| Need | Call | Notes |
|---|---|---|
| Mode init (text/Graphics I) | `tms_init_regs(SCREEN1_TABLE); tms_set_color(...); screen1_prepare(); screen1_load_font();` | always this 4-step sequence |
| Mode init (bitmap/Graphics II) | `tms_init_regs(SCREEN2_TABLE); tms_set_color(...); screen2_init_bitmap(...);` | |
| Set fg/bg | `tms_set_color(FG_BG(COLOR_WHITE, COLOR_BLACK))` | `FG_BG` macro packs nibbles |
| Print text | `screen1_puts(s)`, `screen1_putc(c)`, `screen1_locate(x, y)` | |
| Plot a pixel (G II) | `screen2_plot(x, y)` | |
| Draw line / circle / ellipse / rect (G II) | `screen2_line`, `screen2_circle`, `screen2_ellipse_rect`, `screen2_rect` | |
| Sprites — **always** | `tms_shadow_init()` once, then per frame: `tms_shadow_set/move/clear`, finish with `tms_shadow_flush()` | NEVER call `tms_set_sprite()` per frame |
| Read collision status | `tms_clear_collisions()` after a `tms_read_status()` poll | |
| VBlank wait | `vsync_wait()` | |
| Burst-fill VRAM | `tms_fill_vram(addr, val, count)` (from `tms_fast.s`) | skips per-byte IO delay |

---

## Memory + speed budget

| Preset | Total RAM | User code + data ceiling (approx) |
|---|---|---|
| 4 KB plain | 4096 B | ~2.8 KB (after cc65 runtime + ZP + stack) |
| 8 KB | 8192 B | ~7 KB |
| 16 / 32 / 48 KB HGR | up to 49152 B | most generous; bound by the linker config |

### How cc65 / ld65 interact with size

- **No automatic inlining** of regular C functions. Every helper that JSRs
  costs bytes (the JSR/RTS) AND cycles. Use `static` `#define` macros for
  tiny shims.
- **ld65 strips at `.o` granularity, not per-function.** If you call ONE
  symbol from a `.c` file, the WHOLE `.c` file links. Split big shared
  files so you only link what you call. `gen2c.mk` and `tms9918c.mk`
  expose per-family `*_SRCS` variables for exactly this — list only the
  families you actually call.
- **No `printf`, no `<stdio.h>`** in beginner code. The formatter eats
  ~3 KB. Use `woz_puts`, `woz_print_hex*` or the in-game helpers
  (`gen2_hgr_putu*`, `pl_print_dec_*`).
- **No floats.** cc65 emulates them in software — slow AND big.
- **`unsigned` is 16-bit on cc65**, not 32. Use `unsigned long` if you
  truly need a 32-bit counter (it's bigger AND slower).

---

## Top pitfalls

1. **String literals are `const char *`**, but the raw API (`woz_puts`,
   `screen1_puts`) takes `const unsigned char *`. Use the `puts_apple1`
   macro (or its `screen1_*` equivalent) to dodge the cast.
2. **`printf` is bloat.** Don't include `<stdio.h>` in a 4 KB target —
   the formatter alone exceeds your budget. Use `woz_print_hex*` or the
   `gen2_hgr_putu*` family.
3. **cc65 doesn't auto-inline regular functions.** Every wrapper that
   JSRs adds cycles and bytes. Prefer `#define` macros or `static`
   helpers (cc65 may inline tiny `static`s under `-Oirs`).
4. **ld65 strips by `.o`, not by function.** A text-only demo that drags
   in the old monolithic `gen2.c` used to pull pixel + blit + lores.
   That runtime is now split into per-family modules — `gen2_init.c`,
   `gen2_pixel.c`, `gen2_rect.c`, `gen2_text.c`, `gen2_sprites.c`,
   `gen2_geom.c`, `gen2_lores.c`, plus the hot `gen2_blit.s` (see
   `dev/lib/gen2c/gen2c.mk`) — so you link only the families you use.
   Use the per-family Makefile variables.
5. **Soft-switch reads must STORE, not (void)-cast.** cc65 `-Oirs`
   elides a `(void)volatile_read`. `gen2.h`'s macros use `gen2_ss_sink`
   so the read survives. Roll your own macro the wrong way and you'll
   silently fail to switch mode.
6. **Coordinates: pixel vs byte-column vs cell.** `gen2_hgr_plot(x, y)`
   uses pixels (0..279, 0..191); `gen2_hgr_fill_rect(y0, rows, col0,
   ncols, val)` uses byte columns (0..39) AND a `(y, w, x, w, val)`
   order — use the macro `GEN2_HGR_FILL_RECT_XY(x, y, w, h, val)` to
   stay in the standard `(x, y, w, h)` order. Names are subtle — read
   the chooser table above.
7. **Sprite tearing**: never call `tms_set_sprite()` per frame on the
   TMS9918. Use the `tms_shadow_*` API: mutate RAM, then burst-flush at
   VBlank. See `sketchs/tms9918/_template_tms9918c/main.c`.
8. **One graphics card at a time** (Parmigiani's rule). Don't enable
   GEN2 + TMS9918 simultaneously. Silicon Strict mode auto-evicts the
   conflict; Multiplexing Fantasy doesn't (and isn't real hardware).
9. **The bit-7 palette trap in GEN2 HGR**: a coloured glyph drawn over
   an old colour-cycle pixel can carry the old palette bit. Always call
   `gen2_hgr_clear_pixrect` (or its self-bounded sibling
   `gen2_hgr_putu_field`) before re-drawing in a new colour. The fix
   went into `_gen2_pixrect_asm` so a clear now zeroes bit 7 on the
   edge bytes too.
10. **Measure**: after every change, `ls -l software/<area>/*.bin` and
    track drift. Run `make` on `sketchs/gen2/_template_gen2c` and `sketchs/tms9918/_template_tms9918c`
    to see the per-family size win in action — about 5 KB savings on a
    text + rect program vs linking the full runtime.
11. **`(w+7)/8` codegen bug.** cc65 computes `(w+7)/8` as a 16-bit
    divide whose high byte is junk for some `w` values: `(8+7)=$010F`
    /8 came out **33** instead of 1 because the `(unsigned char)` cast
    truncates only after the divide. Workaround in 8-bit-only ops:
    `(w >> 3) + ((w & 7u) != 0u)`. The receipt is in
    `dev/lib/gen2c/gen2_sprites.c` lines ~31-33; the same trap bites
    any sprite blit you roll by hand.
12. **"Init forgotten" on GEN2.** Every drawing function silently calls
    `gen2_build_tables()` (the lookup tables come up on first use), but
    none of them set the card mode. Calling `gen2_hgr_clear(0)` or
    `gen2_set_draw_page(2)` BEFORE `gen2_hgr_init()` writes the
    framebuffer but the card is still in TEXT mode — silent black
    screen. Always init once at the top of `main()`.

---

## Cookbook

### GEN2 double-buffer loop (flicker-free)

The card has two HIRES framebuffers (page 1 `$2000`, page 2 `$4000`). Always
draw the next frame into the **hidden** page, then flip. `gen2_set_draw_page`
redirects every subsequent primitive; `gen2_show_page` makes the just-drawn
page the visible one. `GEN2_FLIP_PAGE(p)` is the zero-cost swap.

```c
#include "gen2.h"

void main(void)
{
    unsigned char draw = 2u;
    gen2_hgr_init();                            /* must run once before any draw */
    for (;;) {
        gen2_set_draw_page(draw);               /* every primitive writes the hidden page */
        gen2_hgr_clear(0);                      /* erase the page we're drawing onto */
        /* … render this frame … */
        gen2_show_page();                       /* the freshly drawn page goes live */
        GEN2_FLIP_PAGE(draw);                   /* swap for the next frame */
    }
}
```

### TMS9918 sprite loop (no tearing, shadow + VBlank flush)

`tms_set_sprite` writes the SAT live and tears when the beam reads mid-write.
The shadow API mutates a 128-byte RAM mirror, then `tms_shadow_flush()` (in
`tms_fast.s`) burst-writes the SAT inside VBlank. `vsync_wait()` blocks until
the next end-of-frame.

```c
#include "tms9918c.h"

void main(void)
{
    unsigned char i;
    tms_sprite s;
    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(FG_BG(COLOR_WHITE, COLOR_BLACK));
    screen1_prepare(); screen1_load_font();
    tms_shadow_init();                          /* RAM mirror = all sprites OFF */
    /* (load patterns into VRAM here — once) */

    for (;;) {
        for (i = 0; i < 4u; ++i) {
            s.y = 60u + i;                      /* mutate the RAM mirror, freely */
            s.x = 40u + i * 16u;
            s.name = i * 4u;                    /* pattern slot in VRAM */
            s.color = COLOR_WHITE;
            tms_shadow_set(i, &s);
        }
        tms_shadow_set_terminator(4u);          /* mark sprites >= 4 inactive */
        vsync_wait();                           /* park outside the live raster */
        tms_shadow_flush();                     /* one 128-byte burst to VRAM */
    }
}
```

---

## Where to dig deeper

| Document | When |
|---|---|
| [`Programming_Apple1_C.md`](Programming_Apple1_C.md) | Reference for the Apple-1 C base (apple1c) |
| [`Programming_GEN2C.md`](Programming_GEN2C.md) | GEN2 HGR C runtime reference |
| [`Programming_TMS9918C.md`](Programming_TMS9918C.md) | TMS9918 C runtime + silicon handling |
| [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) | Drop down to 6502 asm |
| [`Programming_GEN2.md`](Programming_GEN2.md) | GEN2 in assembly |
| [`Programming_TMS9918.md`](Programming_TMS9918.md) | TMS9918 in assembly (+ silicon bugs) |
| [`APPLE1DEV.md`](APPLE1DEV.md) | Agent playbook (presets, CLI, file layout) |
| [`TODO6502.md`](../../dev/TODO6502.md) | Backlog of 6502-side work |
