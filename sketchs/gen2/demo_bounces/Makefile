# demo_bounces — GEN2 HGR XOR double-buffer showcase (C / cc65).
#
# Build:    make            -> ./GEN2Bounces.bin (+ software/Graphic HGR/*.bin,.txt)
# Clean:    make clean
# Load:     POM1 preset 11, Load Memory > GEN2Bounces.bin, then 6000R

PROJECT  := GEN2Bounces
CC65DIR  := ../../../dev/cc65
LIBDIR   := ../../../dev/lib
TOOLS    := ../../../tools
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
GFX      := $(LIBDIR)/gfx
GFXLIB   := $(GFX)/gfx-gen2.lib
OUTDIR   := ../../../software/Graphic HGR

include $(APPLE1C)/apple1c.mk      # APPLE1C_SRCS / APPLE1C_INCS
include $(GEN2C)/gen2c.mk          # GEN2C_*_SRCS / GEN2C_INCS

CL65   := cl65
CFLAGS := -t none -Oirs
INCS   := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX)

# CORE + TEXT (puts/puts8/putu_field) + GEOM (hgr_rect/line) + SPRITES (blit7).
SRCS := $(PROJECT).c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS)

.PHONY: all clean
.DEFAULT_GOAL := all

$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

all: $(PROJECT).bin

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin
	@ls -l $(PROJECT).bin
	@python3 $(TOOLS)/emit_gen2_txt.py $(PROJECT).bin "$(OUTDIR)/$(PROJECT).bin" "$(OUTDIR)/$(PROJECT).txt" 2>/dev/null || \
	 cp $(PROJECT).bin "$(OUTDIR)/$(PROJECT).bin"

clean:
	rm -f $(PROJECT).bin *.o
