# GEN2DBuf — double-buffering (PAGE2) demo for Uncle Bernie's GEN2 card, in C
# with the gen2c runtime. See README + emit_GEN2DBuf_txt.py for the shipping
# (.bin + Woz-hex .txt under "software/Graphic HGR/") flow.

PROJECT  := GEN2DBuf
CC65DIR  := ../../../cc65
LIBDIR   := ../../../lib
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
SRCS   := $(PROJECT).c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS)

.PHONY: all clean
.DEFAULT_GOAL := all

# Local compile sanity-check + canonical .bin / .txt under software/ via emit script.
all: $(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin *.o
