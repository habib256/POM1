/*
 * gfx.h — card-NEUTRAL 2D graphics primitives for Apple-1 video cards (cc65).
 *
 * AXIS 1 of the GEN2/TMS9918 library factoring (see dev/lib/gfx/README.md).
 *
 * The vector geometry (line / circle / rect / ellipse) and the integer->ASCII
 * number conversions are IDENTICAL across the GEN2 HGR card and the P-LAB
 * TMS9918 card — only the per-pixel store and the screen width differ. This
 * header declares ONE implementation of that shared logic plus the small
 * "backend contract" each card fills in.
 *
 * BINDING = LINK TIME, NOT a function pointer. Parmigiani's "one board at a
 * time" rule (CLAUDE.md) guarantees a single binary ever talks to exactly ONE
 * video card, so the backend symbols (gfx_plot, gfx_hline, ...) are resolved by
 * the linker to that card's implementation — a direct JSR, no per-pixel
 * indirection, no runtime dispatch cost. A GEN2 program links gfx_backend_gen2;
 * a TMS9918 program links gfx_backend_tms. They never coexist.
 *
 * COORDINATES. x is `unsigned` so the same prototype covers GEN2 HIRES
 * (0..279, needs 9 bits) and TMS9918 bitmap (0..255). y is `unsigned char`
 * (0..191 on both). Endpoints handed to gfx_line/gfx_rect are assumed on-screen
 * (the H/V fast paths still clip); gfx_circle clips every plotted point itself.
 */
#ifndef GFX_H
#define GFX_H

/* ===========================================================================
 * Backend contract — each card supplies these (gfx_backend_<card>.c).
 * ===========================================================================
 * The shared algorithms below call ONLY these. Keep the set minimal so a new
 * card (a future framebuffer board) is a handful of one-line wrappers. */

/* Set one pixel (already clipped to the screen by the caller / by gfx_plot
 * itself — both existing backends bounds-check). x:0..width-1, y:0..191. */
void gfx_plot(unsigned x, unsigned char y);

/* Inclusive horizontal / vertical runs. A card with a fast span primitive
 * (GEN2: one STA run per scanline) wires these to it; a card without one
 * (TMS9918) implements them as a plot loop. gfx_line / gfx_rect route straight
 * runs through here so the fast path is never lost to the generic walker. */
void gfx_hline(unsigned x0, unsigned x1, unsigned char y);
void gfx_vline(unsigned x, unsigned char y0, unsigned char y1);

/* Filled rectangle through opposite corners. Each card wires this to its
 * byte-aligned fast path (GEN2: gen2_hgr_fill_pixrect, TMS9918:
 * screen2_filled_rect). Coordinates are inclusive on both ends; both
 * backends sort / bounds-check internally. */
void gfx_filled_rect(unsigned x0, unsigned char y0,
                     unsigned x1, unsigned char y1);

/* Clear the active draw surface. `color` is the per-pixel fill byte the
 * card uses to wipe the framebuffer — GEN2 takes 0x00 (off) or 0x7F (on,
 * all 7 visible bits of a HIRES byte); TMS9918 bitmap mode ignores the
 * value (its hardware clear is always 0). Pass 0 for portable behaviour. */
void gfx_clear(unsigned char color);

/* Screen extent, in pixels. Compile-time constants on each card (GEN2 280x192,
 * TMS9918 256x192); gfx_circle reads them to clip in signed space BEFORE the
 * cast to unsigned (a negative coord cast to unsigned would alias on-screen). */
extern const unsigned      gfx_width;     /* 280 (GEN2) / 256 (TMS9918) */
extern const unsigned char gfx_height;    /* 192 on both                */

/* ===========================================================================
 * Shared geometry (gfx_draw.c) — implemented ONCE, used by both cards.
 * ===========================================================================
 * All endpoints inclusive. */

/* Bresenham line. Pure horizontal / vertical lines shortcut to gfx_hline /
 * gfx_vline (the card's fast span); the diagonal case walks gfx_plot. */
void gfx_line(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1);

/* Rectangle OUTLINE through opposite corners (interior untouched). Four spans
 * via gfx_hline / gfx_vline. */
void gfx_rect(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1);

/* Midpoint circle OUTLINE, centre (xc, yc), radius r; 8-way symmetry, every
 * point clipped to [0,gfx_width) x [0,gfx_height) before plotting. */
void gfx_circle(unsigned xc, unsigned char yc, unsigned char r);

/* Ellipse inscribed in the (x0,y0)-(x1,y1) bounding box, drawn as a 64-segment
 * polyline (the upstream apple1-videocard-lib method). GEN2 gains this for free
 * — it previously had circle only. */
void gfx_ellipse(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1);

/* ===========================================================================
 * Shared integer -> ASCII (gfx_num.c) — STRING BUILDERS only.
 * ===========================================================================
 * These produce a NUL-terminated string; they do NOT draw. Each card draws the
 * result its own way (GEN2: gen2_hgr_puts at a pixel; TMS9918: screen_putc at a
 * char cell) because the text-positioning model is genuinely card-specific
 * (pixel-addressed graphics font vs 8px char cells) — that part is deliberately
 * NOT unified here (see README "what stays per-card").
 *
 * This removes the THREE-way duplication of the conversion itself
 * (gen2_utoa asm + gen2_hgr_putx + printlib pl_print_dec/hex).
 *
 * NOTE on speed: gfx_utoa is portable C (/10, %10). GEN2's hot HUD path
 * (gen2_hgr_putu_field) keeps its hand-written asm gen2_utoa — see README. */

/* Unsigned decimal, no leading zeros. `buf` must hold >= 6 bytes (65535 + NUL).
 * Returns the digit count written (excluding the NUL). */
unsigned char gfx_utoa(char *buf, unsigned value);

/* Signed decimal: leading '-' then magnitude. `buf` >= 7 bytes (-32768 + NUL). */
unsigned char gfx_itoa(char *buf, int value);

/* Unsigned hex, uppercase, no leading zeros (1..4 digits). `buf` >= 5 bytes. */
unsigned char gfx_hexstr(char *buf, unsigned value);

#endif /* GFX_H */
