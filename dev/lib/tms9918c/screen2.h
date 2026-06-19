/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef SCREEN2_H
#define SCREEN2_H

#include "utils.h"
#include "tms9918.h"

extern const unsigned char SCREEN2_TABLE[8];
#define SCREEN2_SIZE (32U * 24U)

extern unsigned char screen2_plot_mode;

#define PLOT_MODE_RESET  0U
#define PLOT_MODE_SET    1U
#define PLOT_MODE_INVERT 2U

void screen2_init_bitmap(unsigned char color);
void screen2_putc(unsigned char ch, unsigned char x, unsigned char y, unsigned char col);
void screen2_puts(const char *s, unsigned char x, unsigned char y, unsigned char col);
void screen2_plot(unsigned char x, unsigned char y);
unsigned char screen2_point(unsigned char x, unsigned char y);
void screen2_line(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1);
void screen2_ellipse_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1);
void screen2_circle(unsigned char xm, unsigned char ym, unsigned char r);
/* Rectangle OUTLINE through opposite corners (interior untouched). Gained from
 * the shared gfx layer (dev/lib/gfx); needs gfx-tms.lib at link time. */
void screen2_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1);

/* --- Extended helpers (screen_ext.c) ------------------------------------ */

/* Zero the entire pattern table (6144 bytes) via tms_fill_vram. Color table
 * is untouched: keep your foreground/background from screen2_init_bitmap. */
void screen2_clear(void);

/* Fill rectangle (x0,y0)-(x1,y1) inclusive using the current plot_mode.
 * Byte-aligned fast fill (left-partial / full-byte run / right-partial per
 * scanline) — the TMS analogue of GEN2's fill_pixrect; ~10x fewer VRAM port ops
 * than a per-pixel plot loop. SET/RESET write full bytes with no read; INVERT
 * still reads (XOR). */
void screen2_filled_rect(unsigned char x0, unsigned char y0,
                         unsigned char x1, unsigned char y1);

#endif
