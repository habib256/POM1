# GEN2Bounces — vitrine primitives vectorielles + nombres HUD + double buffering
# de gen2c, sur la carte GEN2 d'Uncle Bernie, en C.
#
# Cible C / GEN2 HGR : reprend l'invocation cl65 du POM1 Bench pour la cible
# "Uncle Bernie GEN2 HGR (C)" (src/Pom1BenchHost.cpp) : le linker config GEN2 C
# + the gen2c runtime (per-family modules) + apple1c text/keyboard base, compiled at $6000.
#
#   make            -> "software/Graphic HGR/GEN2Bounces.bin" (+ .txt)
#   make clean
#
# Run: build/POM1 --preset 11 \
#          --load 6000:"software/Graphic HGR/GEN2Bounces.bin" --run 6000

PROJECT  := GEN2Bounces
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

# Sources : le sketch + le runtime C GEN2 (+ son blitter asm) + apple1c (woz_mon).
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

all: $(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

# gfx-gen2.lib: card-neutral geometry/numbers (gen2_geom.c forwards line/rect/
# circle/ellipse + putx there). Link-on-demand archive — see dev/lib/gfx/README.md.
$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(GEN2C)/gen2_internal.h $(GEN2C)/gen2_blit.s \
                $(APPLE1C)/apple1io.c $(APPLE1C)/apple1io.h \
                $(APPLE1C)/apple1io_asm.s $(GFX)/gfx.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin $(PROJECT).o gen2.o gen2_blit.o apple1io.o apple1io_asm.o
