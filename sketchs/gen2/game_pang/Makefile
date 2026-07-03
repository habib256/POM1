# game_pang — PANG / Buster Bros clone for GEN2 HGR (C / cc65).
#
# Build:    make            -> ./GEN2Pang.bin  (+ copy to software/Graphic HGR/)
# Clean:    make clean
# Run:      build/POM1 --preset 11 \
#               --load 6000:"software/Graphic HGR/GEN2Pang.bin" --run 6000

PROJECT  := GEN2Pang
CC65DIR  := ../../../dev/cc65
LIBDIR   := ../../../dev/lib
SOFTWARE := ../../../software/Graphic HGR
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
GFX      := $(LIBDIR)/gfx
GFXLIB   := $(GFX)/gfx-gen2.lib

include $(APPLE1C)/apple1c.mk      # APPLE1C_SRCS / APPLE1C_INCS
include $(GEN2C)/gen2c.mk          # GEN2C_*_SRCS / GEN2C_INCS

CL65   := cl65
CFLAGS := -t none -Oirs
INCS   := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX)

# Families this game calls: CORE (always) + SPRITES (blit7) + TEXT (HUD) +
# RECT (fill/clear pixrect) + GEOM (gen2_hgr_rect border, needs gfx-gen2.lib).
SRCS := $(PROJECT).c \
        $(GEN2C_CORE_SRCS) $(GEN2C_SPRITES_SRCS) $(GEN2C_TEXT_SRCS) \
        $(GEN2C_RECT_SRCS) $(GEN2C_GEOM_SRCS) $(GEN2C_PIXEL_SRCS) \
        $(APPLE1C_SRCS)

.PHONY: all clean
.DEFAULT_GOAL := all

$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

all: $(PROJECT).bin

TOOLS    := ../../../tools

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin
	@ls -l $(PROJECT).bin
	@python3 $(TOOLS)/emit_gen2_txt.py $(PROJECT).bin "$(SOFTWARE)/$(PROJECT).bin" "$(SOFTWARE)/$(PROJECT).txt"

clean:
	rm -f $(PROJECT).bin *.o
