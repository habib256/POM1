# demo — CodeTank @ $4000 (minimal TMS9918 menu)
LIBDIR   := ../../../lib/tms9918c
GFXDIR   := ../../../lib/gfx
GFXLIB   := $(GFXDIR)/gfx-tms.lib
CFG      := $(LIBDIR)/cc65/codetank_c.cfg
EMIT     := ../../../cc65/emit_codetank_txt.py
OUT_DIR  := ../../../../software/Apple-1_TMS_CC65
include ../../../cc65/space.mk
NAME     := demo

SOURCES := main.c \
	$(LIBDIR)/apple1_asm.s \
	$(LIBDIR)/apple1.c \
	$(LIBDIR)/tms9918.c \
	$(LIBDIR)/screen1.c \
	$(LIBDIR)/screen2_init.c \
	$(LIBDIR)/screen2_text.c \
	$(LIBDIR)/screen2_pixel.c \
	$(LIBDIR)/screen2_geom.c \
	$(LIBDIR)/c64font.c

.PHONY: all clean

all: $(OUT_DIR_T)/$(NAME).bin $(OUT_DIR_T)/$(NAME).txt

# Card-neutral gfx layer (dev/lib/gfx) as a link-on-demand ar65 archive: screen2
# forwards line/circle/ellipse there, so the lib must be on the link line.
$(GFXLIB):
	$(MAKE) -C $(GFXDIR) tms

# Rebuild on any source / cfg / lib-or-local header / Makefile change. (Replaces
# the old FORCE prerequisite that recompiled unconditionally every invocation.)
$(OUT_DIR_T)/$(NAME).bin: $(SOURCES) $(GFXLIB) $(CFG) $(wildcard *.h) $(wildcard $(LIBDIR)/*.h) $(wildcard $(GFXDIR)/*.h) Makefile
	@mkdir -p "$(OUT_DIR)"
	cl65 -t none -Oirs -C $(CFG) -I $(LIBDIR) -I $(GFXDIR) $(SOURCES) $(GFXLIB) -o "$(OUT_DIR)/$(NAME).bin"

$(OUT_DIR_T)/$(NAME).txt: $(OUT_DIR_T)/$(NAME).bin $(EMIT)
	@python3 "$(EMIT)" --bin "$(OUT_DIR)/$(NAME).bin" --txt "$(OUT_DIR)/$(NAME).txt" --start 0x4000

clean:
	rm -f "$(OUT_DIR)/$(NAME).bin" "$(OUT_DIR)/$(NAME).txt"
