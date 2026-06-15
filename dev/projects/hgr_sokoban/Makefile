# HGR Sokoban — split-bank build (4 KB low + 4 KB high) for the
# Parmigiani 8 KB dual-bank Apple-1 + GEN2 HGR card.
#
# Pattern lifted from dev/projects/hgr_chess/Makefile: ld65 emits two .bin
# files (one per memory region), then a Python one-liner stitches both
# into a single Wozmon-hex .txt that loads both halves and runs E000R.
.DEFAULT_GOAL := all

PROJECT  := HGR_Sokoban
LOAD_CFG := ../games_sokoban/apple1_sok_hgr.cfg
OUT_DIR  := ../../../software/Graphic HGR
empty     :=
space     := $(empty) $(empty)
OUT_DIR_T := $(subst $(space),\$(space),$(OUT_DIR))
LIB      := -I ../../lib/apple1 -I ../../lib/hgr -I ../../lib/games/sokoban -I ../../lib/m6502

OBJS := $(PROJECT).o

$(PROJECT).o: $(PROJECT).asm
	ca65 $(LIB) -o "$@" $<

$(OUT_DIR_T)/$(PROJECT).bin.lo $(OUT_DIR_T)/$(PROJECT).bin.hi: $(OBJS)
	ld65 -C $(LOAD_CFG) $(OBJS) -o "$(OUT_DIR)"/$(PROJECT).bin
	@rm -f "$(OUT_DIR)"/$(PROJECT).bin   # ld65 leaves an empty placeholder; remove it

$(OUT_DIR_T)/$(PROJECT).txt: $(OUT_DIR_T)/$(PROJECT).bin.lo $(OUT_DIR_T)/$(PROJECT).bin.hi
	@python3 ../../cc65/emit_dualbank.py "$(OUT_DIR)"/$(PROJECT).bin.lo "$(OUT_DIR)"/$(PROJECT).bin.hi "$(OUT_DIR)"/$(PROJECT).txt --run E000 --header "// HGR Sokoban — Parmigiani 8 KB dual-bank + GEN2"

.PHONY: all clean test
all: $(OUT_DIR_T)/$(PROJECT).txt

clean:
	rm -f $(OBJS) "$(OUT_DIR)"/$(PROJECT).bin*

TEST_CMD ?= @echo "[$(PROJECT)] no smoke test wired"
test: all
	$(TEST_CMD)
