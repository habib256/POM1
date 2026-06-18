# hello_screen1 — cl65 CodeTank image @ $4000
LIBDIR   := ../../../lib/tms9918c
CFG      := $(LIBDIR)/cc65/codetank_c.cfg
EMIT     := ../../../cc65/emit_codetank_txt.py
# Path with spaces (matches POM1 auto-enable folder layout)
OUT_DIR  := ../../../../software/Apple-1_TMS_CC65
include ../../../cc65/space.mk
NAME     := hello_screen1

SOURCES := main.c \
	$(LIBDIR)/apple1_asm.s \
	$(LIBDIR)/tms9918.c \
	$(LIBDIR)/screen1.c \
	$(LIBDIR)/c64font.c

.PHONY: all clean

all: $(OUT_DIR_T)/$(NAME).bin $(OUT_DIR_T)/$(NAME).txt

# Rebuild on any source / cfg / lib-or-local header / Makefile change. (Replaces
# the old FORCE prerequisite that recompiled unconditionally every invocation.)
$(OUT_DIR_T)/$(NAME).bin: $(SOURCES) $(CFG) $(wildcard *.h) $(wildcard $(LIBDIR)/*.h) Makefile
	@mkdir -p "$(OUT_DIR)"
	cl65 -t none -Oirs -C $(CFG) -I $(LIBDIR) $(SOURCES) -o "$(OUT_DIR)/$(NAME).bin"

$(OUT_DIR_T)/$(NAME).txt: $(OUT_DIR_T)/$(NAME).bin $(EMIT)
	@python3 "$(EMIT)" --bin "$(OUT_DIR)/$(NAME).bin" --txt "$(OUT_DIR)/$(NAME).txt" --start 0x4000

clean:
	rm -f "$(OUT_DIR)/$(NAME).bin" "$(OUT_DIR)/$(NAME).txt"
