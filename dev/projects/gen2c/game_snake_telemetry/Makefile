# GEN2Snake — Snake on Uncle Bernie's GEN2 HGR card, with a telemetry tap.
#
# C / GEN2 HGR target: mirrors the cl65 invocation the POM1 Bench uses for the
# "Uncle Bernie GEN2 HGR (C)" target (src/Pom1BenchHost.cpp): the GEN2 C linker
# config + the gen2c runtime (gen2.c) + the shared apple1c text/keyboard base,
# all compiled at $6000. We additionally add the telemetry header dir.
#
#   make            -> software/Telemetry/GEN2Snake.bin (+ .txt via emit script)
#   make clean
#
# Run: DevBench -> POM1 Bench -> Examples -> Snake telemetry  (target = C/GEN2 HGR)
#  or  build/POM1 --headless --telemetry-port 6602 --preset 8 \
#                 --load 6000:software/Telemetry/GEN2Snake.bin --run 6000

PROJECT  := GEN2Snake
CC65DIR  := ../../../cc65
LIBDIR   := ../../../lib
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
TELEM    := $(LIBDIR)/telemetry
GFX      := $(LIBDIR)/gfx
GFXLIB   := $(GFX)/gfx-gen2.lib
OUT_DIR  := ../../../../software/Telemetry

CL65     := cl65
CFLAGS   := -t none -Oirs
INCS     := -I $(GEN2C) -I $(APPLE1C) -I $(TELEM) -I $(GFX)

# Sources: the sketch + the GEN2 C runtime (+ its asm glyph blitter) + the
# shared apple1c text base.
SRCS := $(PROJECT).c \
        $(GEN2C)/gen2.c \
        $(GEN2C)/gen2_blit.s \
        $(APPLE1C)/apple1io.c \
        $(APPLE1C)/apple1io_asm.s

.PHONY: all clean
.DEFAULT_GOAL := all

# Build the .bin locally, then run the emit script to land .bin + .txt under
# software/Telemetry/ (Woz-hex sidecar). The emit script copies the .bin too.
all: $(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

# gfx-gen2.lib: card-neutral geometry/numbers (gen2.c now forwards line/rect/
# circle/ellipse + putx there). Link-on-demand archive — see dev/lib/gfx/README.md.
$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.c $(GEN2C)/gen2.h $(GEN2C)/gen2_blit.s \
                $(APPLE1C)/apple1io.c $(APPLE1C)/apple1io.h \
                $(APPLE1C)/apple1io_asm.s $(TELEM)/telemetry.h $(GFX)/gfx.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin $(PROJECT).o gen2.o gen2_blit.o apple1io.o apple1io_asm.o
