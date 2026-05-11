# rogue_c — port C de TMS_Rogue (donjon + rendu), CodeTank @ $4000
LIBDIR  := ../../lib
CFG     := ../../cc65/codetank_c.cfg
EMIT    := ../../tools/emit_codetank_txt.py
empty   :=
space   := $(empty) $(empty)
OUTDIR  := ../../../../software/Apple-1_TMS_CC65
OUTDIR_T := $(subst $(space),\$(space),$(OUTDIR))
NAME    := rogue_c

SOURCES := main.c \
	rogue_map.c \
	rogue_render.c \
	rogue_gen.c \
	rogue_fov.c \
	rogue_play.c \
	rogue_sprites_data.c \
	rogue_gfx_data.c \
	$(LIBDIR)/apple1_asm.s \
	$(LIBDIR)/apple1.c \
	$(LIBDIR)/tms9918.c \
	$(LIBDIR)/sprites.c \
	$(LIBDIR)/screen1.c \
	$(LIBDIR)/c64font.c

.PHONY: all clean gfx FORCE

all: $(OUTDIR_T)/$(NAME).bin $(OUTDIR_T)/$(NAME).txt

gfx:
	@python3 ../../../../tools/rogue_tileset_inc_to_c.py

$(OUTDIR_T)/$(NAME).bin: $(SOURCES) $(CFG) Makefile FORCE
	@mkdir -p "$(OUTDIR)"
	cl65 -t none -Oirs -C $(CFG) -I . -I $(LIBDIR) $(SOURCES) -o "$(OUTDIR)/$(NAME).bin"

$(OUTDIR_T)/$(NAME).txt: $(OUTDIR_T)/$(NAME).bin $(EMIT)
	@python3 "$(EMIT)" --bin "$(OUTDIR)/$(NAME).bin" --txt "$(OUTDIR)/$(NAME).txt" --start 0x4000

clean:
	rm -f "$(OUTDIR)/$(NAME).bin" "$(OUTDIR)/$(NAME).txt"

FORCE:
