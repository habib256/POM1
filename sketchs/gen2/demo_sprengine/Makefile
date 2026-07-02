# demo_sprengine -- GEN2 HGR masked sprite engine (save-under over decor).
#
# Build:    make            -> ./main.bin (4 masked creatures over coloured decor)
# Sprites:  make sprites    -> regenerate sprites_msk.h from sprites.txt (--masked)
# Test:     make selftest   -> ./main_test.bin (phased binary for the ctest
#                              harness tools/test_sprengine_gen2.py)
# Clean:    make clean
# Load:     POM1 GEN2 preset, Load Memory > main.bin, then 6000R

PROJECT  := main
CC65DIR  := ../../../dev/cc65
LIBDIR   := ../../../dev/lib
TOOLS    := ../../../tools
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
GFX      := $(LIBDIR)/gfx
GFXLIB   := $(GFX)/gfx-gen2.lib

include $(APPLE1C)/apple1c.mk      # APPLE1C_SRCS / APPLE1C_INCS
include $(GEN2C)/gen2c.mk          # GEN2C_*_SRCS / GEN2C_INCS

CL65   := cl65
CFLAGS := -t none -Oirs
INCS   := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX) -I .

# CORE + SPRMASK + SPRENGINE is the masked engine; RECT + TEXT only for the
# demo's coloured background (TEXT pulls gfx_hexstr -> link gfx-gen2.lib).
SRCS := $(GEN2C_CORE_SRCS) $(GEN2C_SPRMASK_SRCS) $(GEN2C_SPRENGINE_SRCS) \
        $(GEN2C_RECT_SRCS) $(GEN2C_TEXT_SRCS) \
        $(APPLE1C_SRCS)

.PHONY: all clean sprites selftest
.DEFAULT_GOAL := all

$(GFXLIB):
	$(MAKE) -C $(GFX) gen2

sprites: sprites_msk.h
sprites_msk.h: sprites.txt $(TOOLS)/build_preshift_sprites.py
	python3 $(TOOLS)/build_preshift_sprites.py sprites.txt --masked --halo 1 \
	        -o sprites_msk.h

all: $(PROJECT).bin

$(PROJECT).bin: $(PROJECT).c sprites_msk.h $(GEN2C)/gen2.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(PROJECT).c $(SRCS) $(GFXLIB) -o $(PROJECT).bin
	@ls -l $(PROJECT).bin

selftest: $(PROJECT)_test.bin
$(PROJECT)_test.bin: $(PROJECT).c sprites_msk.h $(GEN2C)/gen2.h $(GFXLIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -DSPRENGINE_SELFTEST -C $(GEN2CFG) $(INCS) \
	        $(PROJECT).c $(SRCS) $(GFXLIB) -o $(PROJECT)_test.bin
	@ls -l $(PROJECT)_test.bin

clean:
	rm -f $(PROJECT).bin $(PROJECT)_test.bin *.o
