# gen2c.mk — Makefile fragment for the GEN2 HGR C runtime.
#
# Per-family variables so a project can link ONLY what it calls (ld65 strips
# at .o granularity, not per-function — splitting the runtime into per-family
# files is what lets a text-only demo skip the pixel/blit/lores code). Set
# GEN2C, APPLE1C in the consumer BEFORE including this fragment. Example:
#
#     LIBDIR  := ../../../lib
#     GEN2C   := $(LIBDIR)/gen2c
#     APPLE1C := $(LIBDIR)/apple1c
#     include $(APPLE1C)/apple1c.mk
#     include $(GEN2C)/gen2c.mk
#
#     # Minimal text-only program (smallest possible HIRES binary):
#     SRCS := main.c $(GEN2C_CORE_SRCS) $(GEN2C_TEXT_SRCS) $(GEN2C_RECT_SRCS) \
#             $(APPLE1C_SRCS)
#
#     # OR include the lot (matches existing demos):
#     SRCS := main.c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS)
#
#     INCS := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX)
#
# --- Family-specific source sets (each module is its own .c so ld65 dead-strips
# whole families when the consumer omits them) -----------------------------

# CORE: tables, soft-switch sink, draw-page setter, wait_vbl, hgr_row,
# the shared asm fast paths (gen2_blit.s). Always link this.
GEN2C_CORE_SRCS    := $(GEN2C)/gen2_init.c $(GEN2C)/gen2_blit.s

# Drawing families — pick the ones your program actually calls.
GEN2C_PIXEL_SRCS   := $(GEN2C)/gen2_pixel.c     # gen2_hgr_plot/unplot
GEN2C_RECT_SRCS    := $(GEN2C)/gen2_rect.c      # fill_rect / fill_pixrect / clear_pixrect / colorize
# TEXT = two modules: gen2_text.c (glyph core: puts / puts_color / puts8 +
# BBFont) + gen2_text_num.c (putu / putu_field / puti / putx / putu8). Split so
# an ARCHIVE link (rt.lib — the DevBench, or a Makefile linking a .lib) drops
# the formatters + their pulls (gen2_rect via putu_field's erase, gfx_num_hex
# via putx, cc65 soft multiply) from a string-only program. Direct-object
# consumers of this variable link both — exactly the bytes they linked before.
GEN2C_TEXT_SRCS    := $(GEN2C)/gen2_text.c $(GEN2C)/gen2_text_num.c
GEN2C_SPRITES_SRCS := $(GEN2C)/gen2_sprites.c   # hgr_blit / hgr_blit7
GEN2C_X2_SRCS      := $(GEN2C)/gen2_hgr_x2.s    # gen2_hgr_inflate_x2 — hand-asm (the .c miscompiles under -Oirs; .c kept as host-only ref)
GEN2C_X2BLIT_SRCS  := $(GEN2C)/gen2_hgr_blit_x2.c # gen2_hgr_blit_x2 (au-vol; needs X2 + CORE, owns a 256 B buffer)
GEN2C_PRESHIFT_SRCS:= $(GEN2C)/gen2_preshift.c  # hgr_sprite (Buzzard-Bait 7-phase pre-shift; needs only CORE -- reuses gen2_blit7_run)
GEN2C_SPRMASK_SRCS := $(GEN2C)/gen2_sprmask.s   # masked pre-shifted blit + save-under/restore kernels (needs only CORE)
GEN2C_SPRENGINE_SRCS:= $(GEN2C)/gen2_sprengine.c # gen2_spr_* sprite engine (needs SPRMASK + CORE; owns a 1.5 KB BSS under-buffer pool)
GEN2C_GEOM_SRCS    := $(GEN2C)/gen2_geom.c      # hline / vline / line / rect / circle / ellipse (uses dev/lib/gfx)
GEN2C_LORES_SRCS   := $(GEN2C)/gen2_lores.c     # 40x48 colour-block runtime

# Umbrella — every family at once (existing demos). Order is link-order
# irrelevant for ld65 but kept stable for readability.
GEN2C_ALL_SRCS := $(GEN2C_CORE_SRCS) \
                  $(GEN2C_PIXEL_SRCS) \
                  $(GEN2C_RECT_SRCS) \
                  $(GEN2C_TEXT_SRCS) \
                  $(GEN2C_SPRITES_SRCS) \
                  $(GEN2C_X2_SRCS) \
                  $(GEN2C_X2BLIT_SRCS) \
                  $(GEN2C_PRESHIFT_SRCS) \
                  $(GEN2C_SPRMASK_SRCS) \
                  $(GEN2C_SPRENGINE_SRCS) \
                  $(GEN2C_GEOM_SRCS) \
                  $(GEN2C_LORES_SRCS)

# Include paths. GEN2C_GEOM_SRCS *and* GEN2C_TEXT_SRCS call gfx_*
# (gen2_text_num.c uses gfx_hexstr for putx), so the consumer must also link
# gfx-gen2.lib (built by `make -C dev/lib/gfx gen2`) when it includes either.
GEN2C_INCS := -I $(GEN2C)
