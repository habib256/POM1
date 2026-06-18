# tms9918c.mk — Makefile fragment for the P-LAB TMS9918 C runtime
# (nippur72 port). Per-family variables so a project links ONLY what it calls
# (ld65 strips at .o granularity, not per-function).
#
# Set TMS9918C in the consumer BEFORE including this fragment. Example:
#
#     LIBDIR    := ../../../lib
#     TMS9918C  := $(LIBDIR)/tms9918c
#     include $(TMS9918C)/tms9918c.mk
#
#     # Minimal sprite program:
#     SRCS := main.c $(TMS9918C_CORE_SRCS) $(TMS9918C_SPRITES_SRCS) \
#             $(TMS9918C_APPLE1_SRCS)
#
#     # OR include the lot:
#     SRCS := main.c $(TMS9918C_ALL_SRCS)
#
#     INCS := $(TMS9918C_INCS)
#
# --- Per-family source sets -----------------------------------------------

# CORE: the VDP register / VRAM bus layer (tms_*), plus the byte-burst asm
# fast paths in tms_fast.s. Always link this. (utils.h is macros only — no .c.)
TMS9918C_CORE_SRCS    := $(TMS9918C)/tms9918.c $(TMS9918C)/tms_fast.s

# Graphics-I text mode helpers (screen1_*).
TMS9918C_SCREEN1_SRCS := $(TMS9918C)/screen1.c $(TMS9918C)/c64font.c

# Graphics-II bitmap mode helpers (screen2_*).
TMS9918C_SCREEN2_SRCS := $(TMS9918C)/screen2.c

# Extension helpers (screen1_putcharxy, screen1_fill_color_attr, screen2_clear,
# screen2_filled_rect). Optional. screen_ext.c references screen2_plot_mode, so
# adding it also pulls TMS9918C_SCREEN2_SRCS even in a screen1-only program.
TMS9918C_SCREEN_EXT_SRCS := $(TMS9918C)/screen_ext.c

# Sprite engine + shadow attribute table (tms_set_sprite, tms_shadow_*).
TMS9918C_SPRITES_SRCS := $(TMS9918C)/sprites.c $(TMS9918C)/sprite_shadow.c

# VBlank counter (vsync_*).
TMS9918C_VSYNC_SRCS   := $(TMS9918C)/vsync.c

# Printlib (pl_print_*).
TMS9918C_PRINTLIB_SRCS:= $(TMS9918C)/printlib.c

# Wozmon I/O + Apple-1 keyboard (the lib's own apple1.c — different from
# dev/lib/apple1c). Always linked for any practical program.
TMS9918C_APPLE1_SRCS  := $(TMS9918C)/apple1.c $(TMS9918C)/apple1_asm.s

# Random PRNG.
TMS9918C_RANDOM_SRCS  := $(TMS9918C)/random.c

# Interrupt vectors (only if you wire /INT -> /IRQ at the silicon level).
TMS9918C_INTERRUPT_SRCS:= $(TMS9918C)/interrupt.c

# Umbrella — every family at once. Matches what the existing demos link.
TMS9918C_ALL_SRCS := $(TMS9918C_CORE_SRCS) \
                     $(TMS9918C_SCREEN1_SRCS) \
                     $(TMS9918C_SCREEN2_SRCS) \
                     $(TMS9918C_SCREEN_EXT_SRCS) \
                     $(TMS9918C_SPRITES_SRCS) \
                     $(TMS9918C_VSYNC_SRCS) \
                     $(TMS9918C_PRINTLIB_SRCS) \
                     $(TMS9918C_APPLE1_SRCS) \
                     $(TMS9918C_RANDOM_SRCS)

TMS9918C_INCS := -I $(TMS9918C)
