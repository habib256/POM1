# _template_gen2c — minimal GEN2 HGR (C / cc65) starter.
#
# Build:    make            -> ./main.bin (and software/Graphic HGR/...)
# Clean:    make clean
# Load:     POM1 preset 11, Load Memory > main.bin, then 6000R

PROJECT  := main
CC65DIR  := ../../../dev/cc65
LIBDIR   := ../../../dev/lib
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

# Only the families we actually call. ld65 dead-strips at the .o granularity,
# so omitting GEN2C_PIXEL_SRCS / SPRITES / GEOM / LORES saves real bytes vs
# the GEN2C_ALL_SRCS line below — about 800 byte drop on this demo.
SRCS := $(PROJECT).c \
        $(GEN2C_CORE_SRCS) $(GEN2C_TEXT_SRCS) $(GEN2C_RECT_SRCS) \
        $(APPLE1C_SRCS)

# Need the full GEN2 runtime instead? Swap the SRCS line above for:
#     SRCS := $(PROJECT).c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS)

.PHONY: all clean
.DEFAULT_GOAL := all

$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

all: $(PROJECT).bin

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GFXLIB) -o $(PROJECT).bin
	@ls -l $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin *.o
