# graphs — CodeTank @ $4000 (screen2 bitmap)
LIBDIR  := ../../lib
CFG     := ../../cc65/codetank_c.cfg
EMIT    := ../../tools/emit_codetank_txt.py
empty   :=
space   := $(empty) $(empty)
OUTDIR  := ../../../../software/Apple-1_TMS_CC65
OUTDIR_T := $(subst $(space),\$(space),$(OUTDIR))
NAME    := graphs

SOURCES := main.c \
	$(LIBDIR)/tms9918.c \
	$(LIBDIR)/screen2.c \
	$(LIBDIR)/c64font.c

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
