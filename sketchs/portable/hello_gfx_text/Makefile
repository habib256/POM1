# hello_gfx_text — portable gfx_text façade demo (AXIS 3).
#
# ONE source (hello_gfx_text.c) builds for BOTH video cards: the drawing body is
# card-neutral (gfx.h cell cursor); the cell-glyph backend is chosen at LINK time
# by which gfx archive + runtime is on the line.
#
#   make            -> both ./hello_gfx_text_gen2.bin + ./hello_gfx_text_tms.bin
#   make gen2       -> GEN2 HGR variant  (DevBench: preset 11, Load Memory, 6000R)
#   make tms        -> TMS9918 variant   (DevBench: TMS9918 CodeTank ROM, 4000R)
#   make clean
#
# This doubles as the axis-3 "portable demo" pin: if either link breaks, a façade
# symbol stopped resolving against a backend.

PROJECT  := hello_gfx_text
CC65DIR  := ../../../dev/cc65
LIBDIR   := ../../../dev/lib

GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
TMS9918C := $(LIBDIR)/tms9918c
GFX      := $(LIBDIR)/gfx

include $(APPLE1C)/apple1c.mk      # APPLE1C_SRCS / APPLE1C_INCS
include $(GEN2C)/gen2c.mk          # GEN2C_ALL_SRCS / GEN2C_INCS
include $(TMS9918C)/tms9918c.mk    # TMS9918C_ALL_SRCS / TMS9918C_INCS

CL65    := cl65
CFLAGS  := -t none -Oirs
GEN2CFG := $(CC65DIR)/apple1_gen2_c.cfg
TMSCFG  := $(TMS9918C)/cc65/codetank_c.cfg

.PHONY: all gen2 tms clean
.DEFAULT_GOAL := all
all: gen2 tms

# --- GEN2 HGR variant: -DCARD_GEN2, links gfx-gen2.lib + the gen2c runtime ---
gen2: $(PROJECT)_gen2.bin
$(GFX)/gfx-gen2.lib:
	$(MAKE) -C $(GFX) gen2
$(PROJECT)_gen2.bin: $(PROJECT).c $(GFX)/gfx-gen2.lib $(GEN2CFG)
	$(CL65) $(CFLAGS) -DCARD_GEN2 -C $(GEN2CFG) \
	    $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX) \
	    $(PROJECT).c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS) $(GFX)/gfx-gen2.lib -o $@
	@ls -l $@

# --- TMS9918 bitmap variant: default (no -D), links gfx-tms.lib + tms9918c ---
tms: $(PROJECT)_tms.bin
$(GFX)/gfx-tms.lib:
	$(MAKE) -C $(GFX) tms
$(PROJECT)_tms.bin: $(PROJECT).c $(GFX)/gfx-tms.lib $(TMSCFG)
	$(CL65) $(CFLAGS) -C $(TMSCFG) \
	    $(TMS9918C_INCS) -I $(GFX) \
	    $(PROJECT).c $(TMS9918C_ALL_SRCS) $(GFX)/gfx-tms.lib -o $@
	@ls -l $@

clean:
	rm -f $(PROJECT)_gen2.bin $(PROJECT)_tms.bin *.o
