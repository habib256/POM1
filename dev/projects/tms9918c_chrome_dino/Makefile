# chrome_dino — cl65 CodeTank image @ $4000
LIBDIR  := ../../lib
CFG     := ../../cc65/codetank_c.cfg
EMIT    := ../../tools/emit_codetank_txt.py
GEN     := ../../../../tools/chrome_dino_sheet_to_tms.py
SPRITES := dino_chrome_tms.h
empty   :=
space   := $(empty) $(empty)
OUTDIR  := ../../../../software/Apple-1_TMS_CC65
OUTDIR_T := $(subst $(space),\$(space),$(OUTDIR))
NAME    := chrome_dino

SOURCES := main.c \
	$(LIBDIR)/apple1.c \
	$(LIBDIR)/apple1_asm.s \
	$(LIBDIR)/tms9918.c \
	$(LIBDIR)/screen1.c \
	$(LIBDIR)/sprites.c \
	$(LIBDIR)/c64font.c

.PHONY: all clean regen-sprites FORCE

all: $(OUTDIR_T)/$(NAME).bin $(OUTDIR_T)/$(NAME).txt

# Régénère les motifs depuis la planche Chromium (réseau + Pillow).
regen-sprites:
	@python3 "$(GEN)" --out "$(SPRITES)"

$(OUTDIR_T)/$(NAME).bin: $(SOURCES) $(SPRITES) $(CFG) Makefile FORCE
	@mkdir -p "$(OUTDIR)"
	cl65 -t none -Oirs -C $(CFG) -I $(LIBDIR) $(SOURCES) -o "$(OUTDIR)/$(NAME).bin"

$(OUTDIR_T)/$(NAME).txt: $(OUTDIR_T)/$(NAME).bin $(EMIT)
	@python3 "$(EMIT)" --bin "$(OUTDIR)/$(NAME).bin" --txt "$(OUTDIR)/$(NAME).txt" --start 0x4000

clean:
	rm -f "$(OUTDIR)/$(NAME).bin" "$(OUTDIR)/$(NAME).txt"

FORCE:
