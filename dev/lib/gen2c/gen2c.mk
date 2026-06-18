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
GEN2C_TEXT_SRCS    := $(GEN2C)/gen2_text.c      # puts / puts_color / putu / putu_field / puti / putx / puts8 / putu8 + BBFont
GEN2C_SPRITES_SRCS := $(GEN2C)/gen2_sprites.c   # hgr_blit / hgr_blit7
GEN2C_GEOM_SRCS    := $(GEN2C)/gen2_geom.c      # hline / vline / line / rect / circle / ellipse (uses dev/lib/gfx)
GEN2C_LORES_SRCS   := $(GEN2C)/gen2_lores.c     # 40x48 colour-block runtime

# Umbrella — every family at once (existing demos). Order is link-order
# irrelevant for ld65 but kept stable for readability.
GEN2C_ALL_SRCS := $(GEN2C_CORE_SRCS) \
                  $(GEN2C_PIXEL_SRCS) \
                  $(GEN2C_RECT_SRCS) \
                  $(GEN2C_TEXT_SRCS) \
                  $(GEN2C_SPRITES_SRCS) \
                  $(GEN2C_GEOM_SRCS) \
                  $(GEN2C_LORES_SRCS)

# Include paths. GEN2C_GEOM_SRCS calls gfx_*, so the consumer must also link
# gfx-gen2.lib (built by `make -C dev/lib/gfx gen2`) when it includes it.
GEN2C_INCS := -I $(GEN2C)
