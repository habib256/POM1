# picshow — CodeTank @ $4000 (Screen 2 image)
LIBDIR  := ../../../lib/tms9918c
GFXDIR  := ../../../lib/gfx
GFXLIB  := $(GFXDIR)/gfx-tms.lib
CFG     := ../../../lib/tms9918c/cc65/codetank_c.cfg
EMIT    := ../../../cc65/emit_codetank_txt.py
empty   :=
space   := $(empty) $(empty)
OUTDIR  := ../../../../software/Apple-1_TMS_CC65
OUTDIR_T := $(subst $(space),\$(space),$(OUTDIR))
NAME    := picshow

SOURCES := main.c \
	$(LIBDIR)/apple1_asm.s \
	$(LIBDIR)/tms9918.c \
	$(LIBDIR)/screen2.c \
	$(LIBDIR)/c64font.c

.PHONY: all clean FORCE

all: $(OUTDIR_T)/$(NAME).bin $(OUTDIR_T)/$(NAME).txt

# Card-neutral gfx layer (dev/lib/gfx) as a link-on-demand ar65 archive. screen2.c
# now forwards line/circle/ellipse there, so this lib must be on the link line.
$(GFXLIB):
	$(MAKE) -C $(GFXDIR) tms

$(OUTDIR_T)/$(NAME).bin: $(SOURCES) $(GFXLIB) $(CFG) Makefile FORCE
	@mkdir -p "$(OUTDIR)"
	cl65 -t none -Oirs -C $(CFG) -I $(LIBDIR) -I $(GFXDIR) $(SOURCES) $(GFXLIB) -o "$(OUTDIR)/$(NAME).bin"

$(OUTDIR_T)/$(NAME).txt: $(OUTDIR_T)/$(NAME).bin $(EMIT)
	@python3 "$(EMIT)" --bin "$(OUTDIR)/$(NAME).bin" --txt "$(OUTDIR)/$(NAME).txt" --start 0x4000

clean:
	rm -f "$(OUTDIR)/$(NAME).bin" "$(OUTDIR)/$(NAME).txt"

FORCE:

