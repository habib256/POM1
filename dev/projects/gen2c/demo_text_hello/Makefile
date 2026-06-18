# GEN2TextHello — small demo of the 40x24 TEXT mode on Uncle Bernie's GEN2
# colour card, written in C with the gen2c runtime.
#
# C / GEN2 HGR target: reuses the cl65 invocation the POM1 Bench fires for the
# "Uncle Bernie GEN2 HGR (C)" target (src/Pom1BenchHost.cpp) — the GEN2 C
# linker config + the gen2c runtime (per-family modules, driving TEXT / LORES
# / HIRES via the soft switches) + the apple1c text/keyboard base, all at $6000.
#
#   make            -> "software/Graphic HGR/GEN2TextHello.bin" (+ .txt)
#   make clean
#
# Run: build/POM1 --preset 11 \
#          --load 6000:"software/Graphic HGR/GEN2TextHello.bin" --run 6000

PROJECT  := GEN2TextHello
CC65DIR  := ../../../cc65
LIBDIR   := ../../../lib
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
GFX      := $(LIBDIR)/gfx
GFXLIB   := $(GFX)/gfx-gen2.lib
OUT_DIR  := ../../../../software/Graphic HGR

CL65     := cl65
CFLAGS   := -t none -Oirs
INCS     := -I $(GEN2C) -I $(APPLE1C) -I $(GFX)

# Sources: the sketch + the GEN2 C runtime (+ its asm blitter) + apple1c (woz_mon).
SRCS := $(PROJECT).c \
        $(GEN2C)/gen2_init.c \
        $(GEN2C)/gen2_pixel.c \
        $(GEN2C)/gen2_rect.c \
        $(GEN2C)/gen2_text.c \
        $(GEN2C)/gen2_sprites.c \
        $(GEN2C)/gen2_geom.c \
        $(GEN2C)/gen2_lores.c \
        $(GEN2C)/gen2_blit.s \
        $(APPLE1C)/apple1io.c \
        $(APPLE1C)/apple1io_asm.s

.PHONY: all clean
.DEFAULT_GOAL := all

# Build the .bin locally, then run the emitter script that drops .bin + .txt
# (Woz-hex sidecar) under "software/Graphic HGR/".
all: $(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

# gfx-gen2.lib: card-neutral geometry/numbers (gen2_geom.c forwards line/rect/circle
# /ellipse + putx there). Link-on-demand archive — see dev/lib/gfx/README.md.
$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(GEN2C)/gen2_internal.h \
                $(GEN2C)/gen2_init.c $(GEN2C)/gen2_pixel.c $(GEN2C)/gen2_rect.c \
                $(GEN2C)/gen2_text.c $(GEN2C)/gen2_sprites.c $(GEN2C)/gen2_geom.c \
                $(GEN2C)/gen2_lores.c $(GEN2C)/gen2_blit.s \
                $(APPLE1C)/apple1io.c $(APPLE1C)/apple1io.h \
                $(APPLE1C)/apple1io_asm.s $(GFX)/gfx.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin $(PROJECT).o \
	      gen2_init.o gen2_pixel.o gen2_rect.o gen2_text.o \
	      gen2_sprites.o gen2_geom.o gen2_lores.o gen2_blit.o \
	      apple1io.o apple1io_asm.o
