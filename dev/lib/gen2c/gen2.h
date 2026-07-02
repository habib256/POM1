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

/* Pull in the Apple-1 text/keyboard base by default. The GEN2 card sits on
 * top of the Apple-1 bus, so most programs need woz_puts / apple1_getkey too.
 * #define GEN2_NO_APPLE1 before including gen2.h to skip (e.g. for a headless
 * VRAM dumper that does no I/O). Pure preprocessor — zero bytes added. */
#ifndef GEN2_NO_APPLE1
#include "apple1c.h"
#endif

/* ===========================================================================
 * Function chooser — pick the right helper for the job (no extra cost: the
 * decision is yours, the code that ships is just the one you call).
 * ---------------------------------------------------------------------------
 * Text:
 *   gen2_hgr_puts       — 16x16 doubled glyphs, OR-drawn (overdraws background)
 *   gen2_hgr_puts_color — same, in one of VIOLET/GREEN/ORANGE/BLUE
 *   gen2_hgr_puts8      — native 8x8, 3-4x denser, no doubling
 *   gen2_hgr_putu       — unsigned decimal, OR-drawn, variable width
 *   gen2_hgr_putu_field — unsigned decimal, FIXED-width HUD (erases its box)
 *   gen2_hgr_puti       — signed decimal (-32768..32767)
 *   gen2_hgr_putx       — hex (uppercase, no leading zeros)
 *   gen2_hgr_putu8      — small-font decimal twin of putu
 *
 * Rectangle fill / clear (HIRES):
 *   gen2_hgr_fill_pixrect    — fill pixels (x, y, w, h)         <- pick this
 *   gen2_hgr_clear_pixrect   — erase pixels (x, y, w, h)        <- pick this
 *   gen2_hgr_fill_rect       — byte-column fill (col0, ncols)   <- only if you
 *                              already think in 7-px columns
 *
 * Sprite blit (HIRES):
 *   gen2_hgr_blit            — pixel-precise position, ANY width  <- pick this
 *   gen2_hgr_blit7           — 7-px column snap, BYTE-aligned src <- only when
 *                              you control the sprite layout and want max speed
 *
 * Vector primitives forward to the shared dev/lib/gfx layer (link
 * gfx-gen2.lib): gen2_hgr_line / rect / circle / ellipse + hline / vline.
 *
 * Double buffering:
 *   gen2_set_draw_page(p)    — pick where the next draws WRITE (1 or 2)
 *   gen2_show_page()         — flip the card to DISPLAY the current draw page
 *   GEN2_FLIP_PAGE(p)        — macro: p = (p == 1) ? 2 : 1
 * =========================================================================== */

/* Zero-cost macro helpers (pure preprocessor, expand to nothing if unused). */
#define GEN2_FLIP_PAGE(p)     ((p) = ((p) == 1u ? 2u : 1u))
#define GEN2_PIX_TO_COL(x)    ((unsigned char)((x) / 7u))
#define GEN2_PIX_TO_BIT(x)    ((unsigned char)((x) % 7u))
/* (x, y, w, h, val) wrapper around gen2_hgr_fill_rect — restores the
 * standard (x, y, w, h) argument order for byte-aligned fills. Still
 * byte-granular: pixel x is rounded down to its byte column, pixel w is
 * rounded UP to whole byte columns. Use gen2_hgr_fill_pixrect for pixel-
 * precise white blocks. Zero cost (macro). */
#define GEN2_HGR_FILL_RECT_XY(x, y, w, h, val) \
    gen2_hgr_fill_rect((y), (h), GEN2_PIX_TO_COL(x), \
                       (unsigned char)(((w) + 6u) / 7u), (val))

/* "Init forgotten" trap (read once, save your day): every drawing function
 * silently calls gen2_build_tables() so the column / bit / row tables come
 * up on first use — but NONE of them set the card mode. If you call
 * gen2_hgr_clear() / gen2_hgr_plot() / etc. BEFORE gen2_hgr_init() (or
 * gen2_lores_init()), they happily write to the framebuffer, but the card
 * is still in TEXT mode (or whatever state the latch held at cold plug) —
 * and you see a blank screen. Same trap with gen2_set_draw_page(2):
 * primitives now write to page 2, but the display is still page 1 (no flip
 * until gen2_show_page()), and worse, the card mode hasn't moved. Pattern:
 * **always** call gen2_hgr_init() (or gen2_lores_init()) once before any
 * draw, even when you only mean to flip pages. */

/* Soft switches — each macro is a READ that selects that state (the read IS the
 * toggle; the value read is meaningless). The result is stored into gen2_ss_sink
 * rather than cast to void: cc65 -Oirs ELIDES a volatile read whose value is
 * discarded by (void), which silently turned these into no-ops. A store to a
 * global it cannot prove dead forces the load. (The full-mode initialisers
 * gen2_hgr_init / gen2_lores_init are in asm for the same reason.) */
extern volatile unsigned char gen2_ss_sink;
#define GEN2_SS         ((volatile unsigned char*)0xC250)
#define gen2_graphics() (gen2_ss_sink = GEN2_SS[0])  /* $C250 TEXT off->graphics */
#define gen2_text()     (gen2_ss_sink = GEN2_SS[1])  /* $C251 TEXT on            */
#define gen2_full()     (gen2_ss_sink = GEN2_SS[2])  /* $C252 MIXED off (full)   */
#define gen2_mixed()    (gen2_ss_sink = GEN2_SS[3])  /* $C253 MIXED on (4 rows)  */
#define gen2_page1()    (gen2_ss_sink = GEN2_SS[4])  /* $C254 page 1             */
#define gen2_page2()    (gen2_ss_sink = GEN2_SS[5])  /* $C255 page 2             */
#define gen2_lores()    (gen2_ss_sink = GEN2_SS[6])  /* $C256 RES latch = LORES  */
#define gen2_hires()    (gen2_ss_sink = GEN2_SS[7])  /* $C257 RES latch = HIRES  */

/* HST0 (bit 7) = 1 while blanking. Reading $C257 re-asserts HIRES, so it is a
 * HIRES-safe poll. ALWAYS mask 0x80 — the low 7 bits are garbage. */
#define GEN2_HST0   0x80
#define gen2_hst0() (GEN2_SS[7] & GEN2_HST0)

#define GEN2_HGR1   ((unsigned char*)0x2000) /* HIRES page-1 framebuffer (8 KB) */
#define GEN2_HGR2   ((unsigned char*)0x4000) /* HIRES page-2 framebuffer (8 KB) */
#define GEN2_LORES1 ((unsigned char*)0x0400) /* TEXT/LORES page-1 (shared, 1 KB) */
#define GEN2_LORES2 ((unsigned char*)0x0800) /* TEXT/LORES page-2 (shared, 1 KB) */

/* graphics + hires + page 1 + full screen. */
void gen2_hgr_init(void);

/* BLANK-FIRST variant of gen2_hgr_init: clears the page-1 framebuffer
 * ($2000-$3FFF) while the display is parked on TEXT, THEN flips to HIRES.
 * The card has no display-enable bit and the framebuffer SRAM is
 * indeterminate at power-on, so plain gen2_hgr_init shows SRAM garbage until
 * the program's own clear lands — use this when your program cannot clear
 * immediately after init. (Page 2 is untouched; clear it via
 * gen2_set_draw_page(2) + gen2_hgr_clear(0) before showing it.) */
void gen2_hgr_init_clear(void);

/* Polite exit: restore the monitor-visible state (TEXT + PAGE1) so whatever
 * runs next (Wozmon, BASIC) is visible on the GEN2 monitor. Toggles only —
 * follow it with woz_mon() (the $FF1A "\" prompt entry):
 *     gen2_text_restore();
 *     woz_mon();                                   /- no-return -/           */
void gen2_text_restore(void);

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
/* Fill (set=1) / erase (set=0) a 6x6 block in the 8x8 grid cell (cx, cy).
 * cx*8/cy*8 done in asm, no clip — for 8px-grid games drawing one cell/call. */
void gen2_hgr_cell(unsigned char cx, unsigned char cy, unsigned char set);

/* Set a white pixel. x: 0..279, y: 0..191. Apple II interleaved HIRES layout. */
void gen2_hgr_plot(unsigned x, unsigned char y);

/* Clear a pixel (the inverse of gen2_hgr_plot) — lets a program erase a small
 * region without clearing the whole framebuffer. x: 0..279, y: 0..191. */
void gen2_hgr_unplot(unsigned x, unsigned char y);

/* Blit a 1-bit-per-pixel sprite at pixel (x, y). The bitmap is MSB-first (bit 7
 * = leftmost pixel), w pixels wide x h rows, each row padded to (w+7)/8 bytes,
 * top to bottom. A '1' bit is a sprite pixel; '0' is transparent. Three modes:
 *   GEN2_SET   - OR  the pixels in (draw over black);
 *   GEN2_CLEAR - clear the pixels (erase a known shape);
 *   GEN2_XOR   - toggle the pixels: blit once to draw, blit AGAIN at the same
 *                spot to erase -> flicker-free moving sprites, no save/restore.
 * Clipped to the screen on the right/bottom (keep x >= 0; off-screen-left is not
 * supported). HIRES is 7px/byte so this walks pixel by pixel; for solid blocks
 * gen2_hgr_fill_pixrect is far faster. Example (an 8x8 ball):
 *   static const unsigned char ball[8] = {0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C};
 *   gen2_hgr_blit(px, py, 8, 8, ball, GEN2_XOR); */
#define GEN2_SET    0u
#define GEN2_CLEAR  1u
#define GEN2_XOR    2u
void gen2_hgr_blit(unsigned x, unsigned char y, unsigned char w, unsigned char h,
                   const unsigned char *bitmap, unsigned char mode);

/* FAST byte-aligned blit for big solid sprites. The bitmap is pre-packed in the
 * framebuffer's 7px/byte layout (byte j, bit k = pixel j*7+k, bit 0 = leftmost,
 * bit 7 = 0), so whole bytes are SET/CLEAR/XOR'd in directly — ~7x fewer ops
 * than gen2_hgr_blit. Cost: the sprite snaps to a 7px column grid (x floored to
 * x/7), so move it in steps of 7 to keep XOR-erase aligned. `wbytes` = source
 * bytes per row = ceil(width/7). Same SET/CLEAR/XOR modes. Use this for a large
 * ball / paddle / tile; use gen2_hgr_blit when you need 1px positioning or
 * transparency at an arbitrary x. */
void gen2_hgr_blit7(unsigned x, unsigned char y, unsigned char wbytes,
                    unsigned char h, const unsigned char *src, unsigned char mode);

/* --- Pre-shifted sprite engine (the "Buzzard Bait" method) -----------------
 * gen2_hgr_blit  is pixel-precise but walks pixel-by-pixel (slow).
 * gen2_hgr_blit7 is byte-fast but snaps x to a 7px column grid (jumpy).
 * gen2_hgr_sprite gives BOTH: byte-aligned blit speed AND 1px-precise x.
 *
 * The trick (lifted from Sirius' 1983 arcade engine, see
 * sketchs/gen2/a2port_buzzard_bait/DISASSEMBLY.md S3): the sprite is baked
 * OFFLINE into 7 PRE-SHIFTED copies, one per sub-byte phase (x % 7 == 0..6).
 * At runtime the engine just selects the phase for x%7 and does a byte-aligned
 * blit at column x/7 -- zero per-pixel shifting; the inner loop is gen2_blit7.
 *
 * Cost: 7x the sprite's bytes (static data -- "free"). Build the 7-phase bank
 * with tools/build_preshift_sprites.py (emits a gen2_sprite_t + its data, C or
 * asm). Same SET/CLEAR/XOR modes; XOR is the sweet spot (draw==erase, no
 * backbuffer, decor preserved). Example:
 *     #include "ball_ps.h"            // generated: gen2_sprite_t ball;
 *     gen2_hgr_sprite(px, py, &ball, GEN2_XOR);   // draw
 *     gen2_hgr_sprite(px, py, &ball, GEN2_XOR);   // erase (same x,y)
 *
 * Data ABI (what the generator emits and this reads):
 *   stride = ceil((w + 6) / 7) bytes per row (uniform across the 7 phases;
 *            phase 6 spills one extra byte to the right).
 *   bits   = 7 contiguous phase blocks, each h*stride bytes, row-major,
 *            7px/byte (bit 0 = leftmost, bit 7 = 0). Phase p starts at
 *            offset p*(h*stride). Pixel (row,col) of phase p lives at packed
 *            bit (col+p): byte (col+p)/7, bit (col+p)%7. */
typedef struct {
    const unsigned char *bits;   /* 7 phase blocks, contiguous (h*stride apart) */
    unsigned char        stride; /* bytes per row per phase = ceil((w+6)/7)     */
    unsigned char        h;      /* rows                                         */
} gen2_sprite_t;

/* Blit a pre-shifted sprite bank at pixel (x, y). x is 1px-precise; clipped to
 * the right/bottom edges (keep x >= 0). mode is GEN2_SET / GEN2_CLEAR / GEN2_XOR. */
void gen2_hgr_sprite(unsigned x, unsigned char y,
                     const gen2_sprite_t *spr, unsigned char mode);

/* XOR-only FAST path of gen2_hgr_sprite -- the whole draw (x/7, x%7, phase
 * offset, edge clipping, blit) is hand-asm, skipping the cc65 wrapper's software
 * divide/multiply/16-bit clip. ~3-4x less per-call overhead, which is what lets a
 * SINGLE-buffer erase+redraw pair finish inside V-blank (no beam-race flicker) --
 * the Buzzard-Bait way. Use this in tight animation loops; same data ABI as
 * gen2_hgr_sprite, mode is always XOR (draw==erase). */
void gen2_hgr_sprite_xor(unsigned x, unsigned char y, const gen2_sprite_t *spr);

/* --- Masked pre-shifted sprites + save-under (the SPRMASK family) ----------
 * gen2_hgr_sprite_xor is the fastest mover, but XOR is only self-erasing over
 * a background it also XORed -- over DECOR (text, coloured rects) the sprite
 * shows the background toggled through it. The masked engine draws OPAQUE
 * sprites over any background and puts the background back byte-exact:
 *
 *     dst[j] = (dst[j] & mask[j]) | data[j]
 *
 * Data ABI (what build_preshift_sprites.py --masked emits, and what the
 * gen2_sprmask.s kernels read):
 *   stride = ceil((w + 6) / 7) bytes per row, uniform across the 7 phases
 *            (same rule as gen2_sprite_t).
 *   data   = 7 phase blocks, each h*stride bytes, row-major, 7px/byte
 *            (bit 0 = leftmost, bit 7 CLEAR). Phase p at offset p*(h*stride).
 *            These are the sprite's lit pixels, pre-shifted like a
 *            gen2_sprite_t bank.
 *   mask   = 7 phase blocks with the SAME geometry. A 1-bit KEEPS the
 *            background; mask = ~coverage (coverage = the sprite's footprint,
 *            optionally dilated by a --halo for a black outline). Bit 7 of
 *            every mask byte is ALWAYS 1, so the background byte's HIRES
 *            palette-group bit survives the draw (and data bit 7 is always 0,
 *            so the sprite never flips it). A hand-authored bank that wants to
 *            OWN the palette bit can clear mask bit 7 + set data bit 7 -- the
 *            kernel maths allows it; the generator never does. */
typedef struct {
    const unsigned char *data;   /* 7 phase blocks: pre-shifted pixels, bit7=0 */
    const unsigned char *mask;   /* 7 phase blocks: ~coverage, bit7=1 (KEEP)   */
    unsigned char        stride; /* bytes per row per phase = ceil((w+6)/7)    */
    unsigned char        h;      /* rows                                       */
} gen2_mspr_t;

/* --- The sprite ENGINE (gen2_sprengine.c, needs SPRMASK + CORE) ------------
 * Up to GEN2_SPR_MAX masked sprites with automatic save-under/restore, in
 * either double-buffered (tear-free) or single-buffered (V-blank-raced) mode.
 * The engine owns the under-buffers (a static pool -- no dynamic allocation):
 * each sprite may cover at most GEN2_SPR_UNDER_BYTES = stride*h bytes (e.g.
 * 4x24, or a full 16x16 creature at stride 4 x 16 rows = 64). gen2_spr_define
 * silently rejects (deactivates) a shape over the cap.
 *
 * The loop:
 *     gen2_hgr_init();
 *     ...draw the background (BOTH pages if double buffering)...
 *     gen2_spr_init(1);                          // 1 = double-buffered
 *     gen2_spr_define(0, &wolf);
 *     for (;;) {
 *         gen2_spr_move(0, x, y);                // just records the target
 *         gen2_spr_update();                     // restore + draw + flip
 *     }
 *
 * gen2_spr_update does all the work; see gen2_sprengine.c for the mode
 * semantics (double-buffer: draw on the hidden page, then flip in V-blank;
 * single-buffer: restore+draw inside the V-blank window) and the cycle
 * budget math. */
#define GEN2_SPR_MAX          8u
#define GEN2_SPR_UNDER_BYTES  96u   /* per-sprite save-under cap (stride*h)   */

void gen2_spr_init(unsigned char double_buffered);
void gen2_spr_define(unsigned char id, const gen2_mspr_t *shape);
void gen2_spr_move(unsigned char id, unsigned x, unsigned char y);
void gen2_spr_hide(unsigned char id);
void gen2_spr_update(void);

/* Draw an ASCII string at pixel (x, y) using the built-in Beautiful Boot 8x8
 * font, pixel-doubled so the text is solid white (no NTSC colour artifacts) in
 * 16x16 cells on an 18px pitch. Renders into HIRES page 1; call gen2_hgr_init
 * + gen2_hgr_clear first. Non-printable chars render as a space. */
void gen2_hgr_puts(unsigned x, unsigned char y, const char *s);

/* Same Beautiful Boot font at its NATIVE 8x8 size (no pixel doubling): 7px glyph
 * cells on an 8px pitch, 8px tall. ~3-4x more text per line and faster than the
 * 16x16 gen2_hgr_puts — use it for dense HUDs / status lines. White into HIRES
 * page 1. Clips at y<=184 / x<=273. gen2_hgr_putu8 is the small-font number twin. */
void gen2_hgr_puts8(unsigned x, unsigned char y, const char *s);
void gen2_hgr_putu8(unsigned x, unsigned char y, unsigned value);

/* Draw `value` as unsigned decimal at (x, y), same 16x16 white cells / font as
 * gen2_hgr_puts (1-5 digits, no leading zeros). Handy for scores and counters. */
void gen2_hgr_putu(unsigned x, unsigned char y, unsigned value);

/* Fixed-width, right-aligned unsigned decimal — the flicker-free HUD number.
 * The field is `width` glyph cells wide (18px pitch); the call ERASES exactly
 * that box, then draws the digits flush-right in it. So an updating counter never
 * needs a separate clear_pixrect — a shrinking value leaves no stale digits and
 * the self-bounded wipe can't clip an adjacent label (the trap a hand-rolled
 * erase rectangle falls into). Values wider than the field overflow right, so
 * pick width >= the maximum digit count (e.g. width 5 for a 0..65535 score).
 * width is clamped to 1..14. Page-aware (works while double buffering). */
void gen2_hgr_putu_field(unsigned x, unsigned char y, unsigned value,
                         unsigned char width);

/* Signed decimal at (x, y): leading '-' then magnitude. OR-drawn (transparent,
 * no field erase) — use gen2_hgr_putu_field when you need flicker-free updates. */
void gen2_hgr_puti(unsigned x, unsigned char y, int value);

/* Unsigned hexadecimal at (x, y), uppercase, 1-4 digits, no leading zeros.
 * OR-drawn like gen2_hgr_putu (addresses, bit masks). */
void gen2_hgr_putx(unsigned x, unsigned char y, unsigned value);

/* --- Vector primitives (white, HIRES) -------------------------------------
 * Inclusive pixel endpoints (x:0..279, y:0..191), clipped to the screen. The
 * straight runs use the fast pixel-rectangle path; line/circle walk the LUT
 * plot. The Light Corridor demo hand-rolled all of these in raw asm — here they
 * are once, in the lib. */

/* Horizontal / vertical runs (inclusive). Fast: one STA run per scanline. */
void gen2_hgr_hline(unsigned x0, unsigned x1, unsigned char y);
void gen2_hgr_vline(unsigned x, unsigned char y0, unsigned char y1);

/* Bresenham line between two endpoints (both drawn). Horizontal/vertical lines
 * auto-shortcut to hline/vline. */
void gen2_hgr_line(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1);

/* Rectangle OUTLINE through opposite corners (inclusive); interior untouched
 * (fill it with gen2_hgr_fill_pixrect for a solid box). */
void gen2_hgr_rect(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1);

/* Midpoint circle OUTLINE, centre (xc, yc), radius r; off-screen arcs clipped. */
void gen2_hgr_circle(unsigned xc, unsigned char yc, unsigned char r);

/* Ellipse OUTLINE inscribed in the (x0,y0)-(x1,y1) box (64-segment polyline).
 * Gained from the shared gfx layer (dev/lib/gfx); needs gfx-gen2.lib at link. */
void gen2_hgr_ellipse(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1);

/* Draw a string in one of the four NTSC artifact COLOURS the GEN2 HIRES screen
 * can show (it has no per-pixel colour). Drawn in ONE tinted pass (gen2_blit.s
 * gen2_blit_glyph_color ORs the colour's carrier bit per pixel directly — no
 * white-then-recolorize round trip), and only the glyph itself is touched, so a
 * coloured label can sit right next to other content without bleeding a tint
 * over it. There is NO red on HIRES — orange is the warm tone. See gen2_hgr_puts
 * for layout. */
#define GEN2_VIOLET  1u   /* mauve / purple */
#define GEN2_GREEN   2u
#define GEN2_ORANGE  3u   /* the closest thing HIRES has to "red" */
#define GEN2_BLUE    4u
void gen2_hgr_puts_color(unsigned x, unsigned char y, const char *s, unsigned char color);

/* Tint an arbitrary PIXEL rectangle to one of the four artifact colours (the
 * graphics analogue of gen2_hgr_puts_color): draw a shape white, then recolour
 * [x..x+w) x [y..y+h). HIRES colour is byte-granular, so keep coloured shapes
 * ~1 empty cell apart. Black stays black, so an isolated shape tints cleanly. */
void gen2_hgr_colorize(unsigned x, unsigned char y, unsigned char w,
                       unsigned char h, unsigned char color);

/* ===========================================================================
 * LORES — 40×48 blocks of 16 colours (GEN2 low-resolution graphics)
 * ===========================================================================
 * Unlike HIRES, LORES has REAL per-block colour (it is not an NTSC artifact):
 * a 40-wide × 48-tall grid of 7px×4px coloured blocks. It shares the TEXT
 * page memory ($0400 page 1, Apple II row interleave): each text byte holds
 * TWO vertically-stacked blocks — the LOW nibble is the upper block (even
 * block-row), the HIGH nibble the lower one (odd block-row). So block (x, y)
 * with x:0..39, y:0..47 lives in nibble (y&1) of text-page byte
 * row(y>>1)+x. Colour is a 4-bit index 0..15 into the card's fixed palette
 * (the GEN2_LO_* names below; same 16 colours as Apple II LORES). Mode is
 * selected by gen2_lores_init(). Renderer truth: GraphicsCard::renderLoRes. */

/* The fixed 16-colour LORES palette (GraphicsCard kApple2Palette order). */
#define GEN2_LO_BLACK      0u
#define GEN2_LO_MAGENTA    1u   /* dark red / magenta */
#define GEN2_LO_DARKBLUE   2u
#define GEN2_LO_PURPLE     3u
#define GEN2_LO_DARKGREEN  4u
#define GEN2_LO_GRAY1      5u   /* dark gray  */
#define GEN2_LO_MEDBLUE    6u
#define GEN2_LO_LIGHTBLUE  7u
#define GEN2_LO_BROWN      8u
#define GEN2_LO_ORANGE     9u
#define GEN2_LO_GRAY2      10u  /* light gray */
#define GEN2_LO_PINK       11u
#define GEN2_LO_GREEN      12u  /* light green */
#define GEN2_LO_YELLOW     13u
#define GEN2_LO_AQUA       14u  /* aquamarine */
#define GEN2_LO_WHITE      15u

/* graphics + LORES + page 1 + full screen (call before drawing). */
void gen2_lores_init(void);

/* Fill the whole 40×48 LORES screen with one colour (0..15). Fast: writes the
 * text page ($0400-$07FF) a page at a time with an 8-bit index — NOT a naïve
 * 16-bit pointer loop (see gen2_hgr_clear's note). */
void gen2_lores_clear(unsigned char color);

/* Set / read one block. x:0..39, y:0..47, color:0..15 (a GEN2_LO_* index).
 * Out-of-range plots are dropped; gen2_lores_getblock returns 0 off-screen. */
void gen2_lores_setblock(unsigned char x, unsigned char y, unsigned char color);
unsigned char gen2_lores_getblock(unsigned char x, unsigned char y);

/* Horizontal / vertical runs of blocks, both endpoints INCLUSIVE (Apple II
 * Applesoft HLIN/VLIN convention). Clipped to the 40×48 grid; an empty span
 * (x0>x1 / y0>y1) draws nothing. */
void gen2_lores_hlin(unsigned char x0, unsigned char x1, unsigned char y,
                     unsigned char color);
void gen2_lores_vlin(unsigned char x, unsigned char y0, unsigned char y1,
                     unsigned char color);

/* Fill a w×h block rectangle whose top-left is (x, y) with `color`. Clipped to
 * the grid (w/h past the edge are trimmed). */
void gen2_lores_fill_rect(unsigned char x, unsigned char y,
                          unsigned char w, unsigned char h, unsigned char color);

/* Coarse spin until vertical blank (NOT cycle-exact — for tight beam-racing use
 * the ASM dev/lib/gen2 gen2_beam_lock). Double-samples HST0 to skip the colour-
 * burst notch and tells V-blank from H-blank by how long the blank lasts. */
void gen2_wait_vbl(void);

/* ===========================================================================
 * Double buffering — draw page vs display page (PAGE2)
 * ===========================================================================
 * The card has TWO framebuffers: page 1 (HIRES $2000 / LORES $0400) and page 2
 * (HIRES $4000 / LORES $0800). For flicker/tear-free full-screen animation you
 * draw the next frame into the HIDDEN page while the card shows the other, then
 * flip — the viewer never sees a half-drawn frame.
 *
 *   gen2_set_draw_page(page)  picks where EVERY drawing primitive writes (HIRES
 *                             and LORES alike); page is 1 or 2. Cheap but not
 *                             free — it re-derives the scanline tables, so set it
 *                             ONCE per frame, not per primitive. The per-pixel
 *                             plot/blit/fill hot paths are unchanged.
 *   gen2_show_page()          flips the card to display the CURRENT draw page
 *                             (the $C254/$C255 soft switch).
 *
 * Both pages share one mode — select it once (gen2_hgr_init / gen2_lores_init,
 * which also display page 1). The classic loop:
 *
 *     unsigned char draw = 2;
 *     gen2_hgr_init();
 *     for (;;) {
 *         gen2_set_draw_page(draw);
 *         gen2_hgr_clear(0);
 *         ...draw the frame...
 *         gen2_show_page();                  // freshly drawn page goes live
 *         draw = (draw == 1u) ? 2u : 1u;     // next frame -> the other page
 *     }
 */
void gen2_set_draw_page(unsigned char page);   /* 1 or 2 (out-of-range -> 1) */
void gen2_show_page(void);                      /* display the current draw page */

#endif /* GEN2_H */
