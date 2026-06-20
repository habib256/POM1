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
 * Shared geometry (gfx_line/rect/circle/ellipse.c) — implemented ONCE, used by both cards.
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
 * Shared integer -> ASCII (gfx_num_dec/hex.c) — STRING BUILDERS only.
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

/* ===========================================================================
 * Shared positioned-text façade (gfx_text.c) — AXIS 3 of the factoring.
 * ===========================================================================
 * A card-NEUTRAL cell-cursor model: an 8x8 character grid with a single
 * cursor, so a program can `gfx_gotoxy` + `gfx_text` / `gfx_putu` and compile
 * for GEN2 *or* TMS9918 by backend choice alone. The shared layer here owns the
 * cursor, advance/wrap and the number formatting; each card supplies the two
 * one-line cell primitives below (gfx_text_backend_<card>.c) that map a cell to
 * its native glyph blit (GEN2 8x8 gen2_hgr_puts8 / TMS9918 screen2_putc).
 *
 * This is ADDITIVE and deliberately NEUTRAL — it does NOT replace or "level
 * down" the rich per-card text APIs. GEN2's 16x16 doubled glyphs + NTSC artifact
 * colour (gen2_hgr_puts_color) and TMS's per-cell colour attributes remain the
 * way to get card-specific richness; the façade is the lowest common denominator
 * (monospaced 8x8 white-or-default cells) for portable HUDs / menus / demos. */

/* Cell grid extent in CHARACTERS — compile-time constants per card
 * (GEN2 35x24 over 280x192; TMS9918 32x24 over 256x192; 24 rows on both). */
extern const unsigned char gfx_text_cols;
extern const unsigned char gfx_text_rows;

/* Backend cell primitives (gfx_text_backend_<card>.c). gfx_cell_glyph draws ONE
 * printable 8x8 glyph at cell (col,row) in the current text colour; the shared
 * layer guarantees col<gfx_text_cols, row<gfx_text_rows and ch>=0x20. */
void gfx_cell_glyph(char ch, unsigned char col, unsigned char row);

/* Set the text colour for subsequent cells. The byte is card-specific:
 * 0 = GFX_TEXT_DEFAULT (each card's readable default — white). On TMS9918 pass
 * FG_BG(fg,bg) for a coloured cell; on GEN2 the 8x8 cell path is white-only
 * (the value is ignored — use gen2_hgr_puts_color for NTSC artifact colour). */
#define GFX_TEXT_DEFAULT 0u
void gfx_cell_color(unsigned char color);

/* Move the text cursor to cell (col,row); clamped into the grid. */
void gfx_gotoxy(unsigned char col, unsigned char row);

/* Draw one character at the cursor and advance. '\n' = next row / column 0,
 * '\r' = column 0, other control chars (<0x20) are skipped. Advancing past the
 * right margin wraps to column 0 of the next row; the last row does not scroll
 * (further glyphs stay clamped on it). */
void gfx_putc(char ch);

/* Draw a NUL-terminated string from the cursor (gfx_putc per character). */
void gfx_text(const char *s);

/* Numbers AT THE CURSOR — build via gfx_utoa / gfx_itoa / gfx_hexstr then
 * gfx_text. Decimal has no leading zeros; hex is uppercase, no leading zeros. */
void gfx_putu(unsigned value);
void gfx_puti(int value);
void gfx_putx(unsigned value);

#endif /* GFX_H */
