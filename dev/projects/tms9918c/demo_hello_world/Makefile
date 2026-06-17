# hello_world — CodeTank @ $4000 (Wozmon seulement)
LIBDIR  := ../../../lib/tms9918c
CFG     := ../../../lib/tms9918c/cc65/codetank_c.cfg
EMIT    := ../../../cc65/emit_codetank_txt.py
empty   :=
space   := $(empty) $(empty)
OUTDIR  := ../../../../software/Apple-1_TMS_CC65
OUTDIR_T := $(subst $(space),\$(space),$(OUTDIR))
NAME    := hello_world

SOURCES := main.c $(LIBDIR)/apple1_asm.s $(LIBDIR)/apple1.c

.PHONY: all clean FORCE

all: $(OUTDIR_T)/$(NAME).bin $(OUTDIR_T)/$(NAME).txt

$(OUTDIR_T)/$(NAME).bin: $(SOURCES) $(CFG) Makefile FORCE
	@mkdir -p "$(OUTDIR)"
	cl65 -t none -Oirs -C $(CFG) -I $(LIBDIR) $(SOURCES) -o "$(OUTDIR)/$(NAME).bin"

$(OUTDIR_T)/$(NAME).txt: $(OUTDIR_T)/$(NAME).bin $(EMIT)
	@python3 "$(EMIT)" --bin "$(OUTDIR)/$(NAME).bin" --txt "$(OUTDIR)/$(NAME).txt" --start 0x4000

clean:
	rm -f "$(OUTDIR)/$(NAME).bin" "$(OUTDIR)/$(NAME).txt"

FORCE:
