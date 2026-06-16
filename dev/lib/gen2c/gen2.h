/*
 * gen2.h — minimal C for Uncle Bernie's GEN2 colour graphics card (cc65).
 *
 * The GEN2 is the Apple II video subsystem on the Apple-1 bus. Mode is driven
 * by READ-ONLY soft switches at $C250-$C257 — a 1:1 port of Apple II
 * $C050-$C057, so the EVEN address clears a switch and the ODD address sets it.
 * A read also returns HST0 (the H/V-blank flag) in bit 7; the low 7 bits are an
 * UNRELIABLE floating bus — never use them. There is no power-on default, so a
 * program MUST select the mode itself.
 *
 * Inspired by cc65's official apple2 HIRES handling (same $2000 interleaved
 * framebuffer layout), but with the GEN2 $C25x switches + HST0 vaporlock
 * replacement instead of the Apple II ROM/$C050.
 */
#ifndef GEN2_H
#define GEN2_H

/* Soft switches — each macro is a READ that selects that state. */
#define GEN2_SS         ((volatile unsigned char*)0xC250)
#define gen2_graphics() ((void)GEN2_SS[0])   /* $C250  TEXT off -> graphics    */
#define gen2_text()     ((void)GEN2_SS[1])   /* $C251  TEXT on                 */
#define gen2_full()     ((void)GEN2_SS[2])   /* $C252  MIXED off (full screen) */
#define gen2_mixed()    ((void)GEN2_SS[3])   /* $C253  MIXED on (4 text rows)  */
#define gen2_page1()    ((void)GEN2_SS[4])   /* $C254  page 1 (HIRES $2000)    */
#define gen2_page2()    ((void)GEN2_SS[5])   /* $C255  page 2 (HIRES $4000)    */
#define gen2_lores()    ((void)GEN2_SS[6])   /* $C256  RES latch = LORES       */
#define gen2_hires()    ((void)GEN2_SS[7])   /* $C257  RES latch = HIRES       */

/* HST0 (bit 7) = 1 while blanking. Reading $C257 re-asserts HIRES, so it is a
 * HIRES-safe poll. ALWAYS mask 0x80 — the low 7 bits are garbage. */
#define GEN2_HST0   0x80
#define gen2_hst0() (GEN2_SS[7] & GEN2_HST0)

#define GEN2_HGR1   ((unsigned char*)0x2000) /* HIRES page-1 framebuffer (8 KB) */

/* graphics + hires + page 1 + full screen. */
void gen2_hgr_init(void);

/* Fill the HIRES page-1 framebuffer ($2000-$3FFF) with `fill` (0 = black). */
void gen2_hgr_clear(unsigned char fill);

/* Fast byte-aligned rectangle fill of HIRES page 1, via a hand-written 6502
 * inner loop (gen2_blit.s). Fills `rows` scanlines from y0, byte columns
 * [col0, col0+ncols), with `val` (0 = erase). Horizontally byte-granular
 * (7px/byte, so col = x/7, ncols = how many 7px-wide bytes). This is the fast
 * way to erase the area behind text / a sprite without clearing the whole
 * screen — far cheaper than per-pixel gen2_hgr_unplot. The rectangle is clipped
 * to the screen. Example: erase a 16x16 digit drawn at x=123,y=88 ->
 * gen2_hgr_fill_rect(88, 16, 16, 8, 0). */
void gen2_hgr_fill_rect(unsigned char y0, unsigned char rows,
                        unsigned char col0, unsigned char ncols,
                        unsigned char val);

/* Base address of HIRES scanline y (0..191) in page 1 — Apple II interleave. */
unsigned char *gen2_hgr_row(unsigned char y);

/* Fill / erase a PIXEL-aligned rectangle [x, x+w) × [y, y+h) via a hand-written
 * 6502 inner loop (gen2_blit.s) — unlike gen2_hgr_fill_rect these take pixel x/w,
 * not byte columns, so partial edge bytes are handled. fill = white, clear =
 * black. This is the fast way to draw/erase solid blocks (game tiles, sprites,
 * bars): one call replaces a per-pixel gen2_hgr_plot double loop (e.g. a 6×6
 * Snake cell = 1 call vs 36 plots). x:0..279, y:0..191; w (≤255) is clipped to
 * the right edge, h to the bottom. For full-width spans prefer gen2_hgr_fill_rect
 * (byte-aligned) or gen2_hgr_clear. */
void gen2_hgr_fill_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h);
void gen2_hgr_clear_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h);

/* Set a white pixel. x: 0..279, y: 0..191. Apple II interleaved HIRES layout. */
void gen2_hgr_plot(unsigned x, unsigned char y);

/* Clear a pixel (the inverse of gen2_hgr_plot) — lets a program erase a small
 * region without clearing the whole framebuffer. x: 0..279, y: 0..191. */
void gen2_hgr_unplot(unsigned x, unsigned char y);

/* Draw an ASCII string at pixel (x, y) using the built-in Beautiful Boot 8x8
 * font, pixel-doubled so the text is solid white (no NTSC colour artifacts) in
 * 16x16 cells on an 18px pitch. Renders into HIRES page 1; call gen2_hgr_init
 * + gen2_hgr_clear first. Non-printable chars render as a space. */
void gen2_hgr_puts(unsigned x, unsigned char y, const char *s);

/* Draw `value` as unsigned decimal at (x, y), same 16x16 white cells / font as
 * gen2_hgr_puts (1-5 digits, no leading zeros). Handy for scores and counters. */
void gen2_hgr_putu(unsigned x, unsigned char y, unsigned value);

/* Draw a string in one of the four NTSC artifact COLOURS the GEN2 HIRES screen
 * can show (it has no per-pixel colour). The text is drawn white, then an asm
 * pass (gen2_blit.s gen2_colorize_asm) masks it to the colour's carrier. There
 * is NO red on HIRES — orange is the warm tone. Keep coloured labels clear of
 * other content (the whole text box is tinted). See gen2_hgr_puts for layout. */
#define GEN2_VIOLET  1u   /* mauve / purple */
#define GEN2_GREEN   2u
#define GEN2_ORANGE  3u   /* the closest thing HIRES has to "red" */
#define GEN2_BLUE    4u
void gen2_hgr_puts_color(unsigned x, unsigned char y, const char *s, unsigned char color);

/* Coarse spin until vertical blank (NOT cycle-exact — for tight beam-racing use
 * the ASM dev/lib/gen2 gen2_beam_lock). Double-samples HST0 to skip the colour-
 * burst notch and tells V-blank from H-blank by how long the blank lasts. */
void gen2_wait_vbl(void);

#endif /* GEN2_H */
