# GEN2Snake — Snake on Uncle Bernie's GEN2 HGR card, with a telemetry tap.
# See README + emit_GEN2Snake_txt.py for the shipping flow (lands under
# software/Telemetry/ instead of the usual Graphic HGR/).

PROJECT  := GEN2Snake
CC65DIR  := ../../../cc65
LIBDIR   := ../../../lib
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
TELEM    := $(LIBDIR)/telemetry
GFX      := $(LIBDIR)/gfx
GFXLIB   := $(GFX)/gfx-gen2.lib

include $(APPLE1C)/apple1c.mk
include $(GEN2C)/gen2c.mk

CL65   := cl65
CFLAGS := -t none -Oirs
INCS   := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(TELEM) -I $(GFX)
SRCS   := $(PROJECT).c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS)

.PHONY: all clean
.DEFAULT_GOAL := all

all: $(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(TELEM)/telemetry.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin *.o
