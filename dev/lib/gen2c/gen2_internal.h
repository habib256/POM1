/* gen2_internal.h — private declarations shared by the per-family gen2c
 * modules (gen2_init.c, gen2_pixel.c, gen2_rect.c, gen2_text.c,
 * gen2_sprites.c, gen2_geom.c, gen2_lores.c). NOT included by gen2.h, NOT
 * intended for consumer code.
 *
 * The gen2c runtime was historically one ~5 KB `gen2.c`. cc65's ld65 links a
 * whole .o file if any one symbol is referenced, so a text-only demo dragged
 * in pixel + rect + blit + lores. Splitting per family lets ld65 dead-strip
 * the unused families. This header carries:
 *   1. The `extern` declarations of the zero-page parameter blocks owned by
 *      `gen2_blit.s` (every module that touches a $gen2_*$ ZP var needs the
 *      matching `#pragma zpsym` so cc65 emits a zp store).
 *   2. The cross-module globals (lookup tables, page bases) defined in
 *      `gen2_init.c`.
 *   3. The cross-module helpers (table-builder) defined in `gen2_init.c`. */

#ifndef GEN2_INTERNAL_H
#define GEN2_INTERNAL_H

/* --- Zero-page parameter blocks (defined in gen2_blit.s) -------------------- */
extern const unsigned char *gen2_g_glyph;
extern unsigned char gen2_g_col, gen2_g_mask, gen2_g_y;
extern void gen2_blit_glyph(void);
extern void gen2_blit_glyph_color(void);

extern unsigned char gen2_f_y0, gen2_f_rows, gen2_f_col0, gen2_f_cols, gen2_f_val;
extern void gen2_fill_rect_asm(void);

extern unsigned gen2_p_x;
extern unsigned char gen2_p_y;
extern void gen2_plot_asm(void);
extern void gen2_unplot_asm(void);

extern unsigned gen2_r_x, gen2_r_xr;
extern unsigned char gen2_r_y0, gen2_r_rows, gen2_r_mode;
extern void gen2_pixrect_asm(void);

/* gen2_hgr_cell parameter block (8x8-grid cell blitter, see gen2_rect.c). */
extern unsigned char gen2_c_cx, gen2_c_cy, gen2_c_set;
extern void gen2_cell_asm(void);

extern unsigned char gen2_z_col0, gen2_z_ncols, gen2_z_y0, gen2_z_rows;
extern unsigned char gen2_z_ce, gen2_z_co, gen2_z_hi;
extern void gen2_colorize_asm(void);

extern unsigned char gen2_t_col, gen2_t_bit, gen2_t_n, gen2_t_color;
extern const unsigned char *gen2_t_s;
extern const unsigned char *gen2_t_font;
extern void gen2_puts_run(void);
extern void gen2_puts_run8(void);

extern unsigned char gen2_u_lo, gen2_u_hi;
extern char *gen2_u_ptr;
extern void gen2_utoa(void);

extern unsigned char gen2_b_col, gen2_b_mask, gen2_b_w, gen2_b_h;
extern unsigned char gen2_b_stride, gen2_b_y, gen2_b_mode;
extern const unsigned char *gen2_b_src;
extern void gen2_blit_run(void);
extern void gen2_blit7_run(void);
extern void gen2_preshift_xor_run(void);   /* dedicated XOR fast path (no mode dispatch) */

/* gen2_hgr_sprite_xor's all-asm worker: the C entry only stores these zp args. */
extern unsigned gen2_xs_x;
extern unsigned char gen2_xs_y;
extern const gen2_sprite_t *gen2_xs_spr;
extern void gen2_xs_run(void);

#pragma zpsym("gen2_g_glyph")
#pragma zpsym("gen2_g_col")
#pragma zpsym("gen2_g_mask")
#pragma zpsym("gen2_g_y")
#pragma zpsym("gen2_f_y0")
#pragma zpsym("gen2_f_rows")
#pragma zpsym("gen2_f_col0")
#pragma zpsym("gen2_f_cols")
#pragma zpsym("gen2_f_val")
#pragma zpsym("gen2_p_x")
#pragma zpsym("gen2_p_y")
#pragma zpsym("gen2_r_x")
#pragma zpsym("gen2_r_xr")
#pragma zpsym("gen2_r_y0")
#pragma zpsym("gen2_r_rows")
#pragma zpsym("gen2_r_mode")
#pragma zpsym("gen2_c_cx")
#pragma zpsym("gen2_c_cy")
#pragma zpsym("gen2_c_set")
#pragma zpsym("gen2_z_col0")
#pragma zpsym("gen2_z_ncols")
#pragma zpsym("gen2_z_y0")
#pragma zpsym("gen2_z_rows")
#pragma zpsym("gen2_z_ce")
#pragma zpsym("gen2_z_co")
#pragma zpsym("gen2_z_hi")
#pragma zpsym("gen2_t_col")
#pragma zpsym("gen2_t_bit")
#pragma zpsym("gen2_t_n")
#pragma zpsym("gen2_t_color")
#pragma zpsym("gen2_t_s")
#pragma zpsym("gen2_t_font")
#pragma zpsym("gen2_u_lo")
#pragma zpsym("gen2_u_hi")
#pragma zpsym("gen2_u_ptr")
#pragma zpsym("gen2_b_col")
#pragma zpsym("gen2_b_mask")
#pragma zpsym("gen2_b_w")
#pragma zpsym("gen2_b_h")
#pragma zpsym("gen2_b_stride")
#pragma zpsym("gen2_b_y")
#pragma zpsym("gen2_b_mode")
#pragma zpsym("gen2_b_src")
#pragma zpsym("gen2_xs_x")
#pragma zpsym("gen2_xs_y")
#pragma zpsym("gen2_xs_spr")

/* --- Cross-module globals (defined in gen2_init.c) -------------------------- */
extern unsigned char gen2_rowlo[192];          /* HIRES scanline base low byte  */
extern unsigned char gen2_rowhi[192];          /* HIRES scanline base high byte */
extern unsigned char gen2_col7[280];           /* x / 7 (byte column)           */
extern unsigned char gen2_mask7[280];          /* 1 << (x % 7) (bit mask)       */
extern unsigned char gen2_phase7[280];         /* x % 7 (sub-byte phase 0..6)    */
extern unsigned char gen2_lo_rowlo[24];        /* LORES text-row base low byte  */
extern unsigned char gen2_lo_rowhi[24];        /* LORES text-row base high byte */
extern unsigned char gen2_lo_base;             /* LORES page base ($04 or $08)  */
extern unsigned char gen2_lo_ready;            /* LORES tables built once       */

/* --- Cross-module helpers (defined in gen2_init.c) -------------------------- */
extern void gen2_build_tables(void);           /* idempotent table build        */

#endif /* GEN2_INTERNAL_H */
