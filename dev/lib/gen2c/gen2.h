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

/* Base address of HIRES scanline y (0..191) in page 1 — Apple II interleave. */
unsigned char *gen2_hgr_row(unsigned char y);

/* Set a white pixel. x: 0..279, y: 0..191. Apple II interleaved HIRES layout. */
void gen2_hgr_plot(unsigned x, unsigned char y);

/* Draw an ASCII string at pixel (x, y) using the built-in Beautiful Boot 8x8
 * font, pixel-doubled so the text is solid white (no NTSC colour artifacts) in
 * 16x16 cells on an 18px pitch. Renders into HIRES page 1; call gen2_hgr_init
 * + gen2_hgr_clear first. Non-printable chars render as a space. */
void gen2_hgr_puts(unsigned x, unsigned char y, const char *s);

/* Draw `value` as unsigned decimal at (x, y), same 16x16 white cells / font as
 * gen2_hgr_puts (1-5 digits, no leading zeros). Handy for scores and counters. */
void gen2_hgr_putu(unsigned x, unsigned char y, unsigned value);

/* Coarse spin until vertical blank (NOT cycle-exact — for tight beam-racing use
 * the ASM dev/lib/gen2 gen2_beam_lock). Double-samples HST0 to skip the colour-
 * burst notch and tells V-blank from H-blank by how long the blank lasts. */
void gen2_wait_vbl(void);

#endif /* GEN2_H */
