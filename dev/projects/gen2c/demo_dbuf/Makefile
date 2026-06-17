# GEN2DBuf — démo double buffering (PAGE2) de la carte GEN2 d'Uncle Bernie, en C
# avec le runtime gen2c.
#
# Cible C / GEN2 HGR : reprend l'invocation cl65 du POM1 Bench pour la cible
# "Uncle Bernie GEN2 HGR (C)" (src/Pom1BenchHost.cpp) : le linker config GEN2 C
# + le runtime gen2c (gen2.c, qui gère le double buffering) + la base
# texte/clavier apple1c, compilés à $6000.
#
#   make            -> "software/Graphic HGR/GEN2DBuf.bin" (+ .txt)
#   make clean
#
# Run: build/POM1 --preset 12 \
#          --load 6000:"software/Graphic HGR/GEN2DBuf.bin" --run 6000

PROJECT  := GEN2DBuf
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
        $(GEN2C)/gen2.c \
        $(GEN2C)/gen2_blit.s \
        $(APPLE1C)/apple1io.c \
        $(APPLE1C)/apple1io_asm.s

.PHONY: all clean
.DEFAULT_GOAL := all

# Construit le .bin localement, puis lance le script d'émission qui dépose
# .bin + .txt (sidecar Woz-hex) sous "software/Graphic HGR/".
all: $(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

# gfx-gen2.lib: card-neutral geometry/numbers (gen2.c now forwards line/rect/
# circle/ellipse + putx there). Link-on-demand archive — see dev/lib/gfx/README.md.
$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.c $(GEN2C)/gen2.h $(GEN2C)/gen2_blit.s \
                $(APPLE1C)/apple1io.c $(APPLE1C)/apple1io.h \
                $(APPLE1C)/apple1io_asm.s $(GFX)/gfx.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin $(PROJECT).o gen2.o gen2_blit.o apple1io.o apple1io_asm.o
