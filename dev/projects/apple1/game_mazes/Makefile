.DEFAULT_GOAL := all
# Maze generators (Sidewinder + Recursive Backtracker)
LOAD_CFG := ../../../cc65/apple1_4k.cfg
OUT_DIR  := ../../../../software/Apple-1 games
include ../../../cc65/space.mk    # OUT_DIR_T (escaped form for make targets)
LIB      := -I ../../../lib/apple1 -I ../../../lib/m6502

PROGS    := Maze_Sidewinder Maze2_Backtracker
BINS     := $(addprefix $(OUT_DIR_T)/,$(addsuffix .bin,$(PROGS)))

.PHONY: all clean txt
# .txt (Wozmon-hex) is the canonical committed artefact; regenerate it from
# source via emit_Mazes_txt.py (which also re-emits both .bin under software/).
all: txt

txt: $(BINS)
	python3 emit_Mazes_txt.py

$(OUT_DIR_T)/%.bin: %.asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $*.o -o "$@"

clean:
	rm -f $(addsuffix .o,$(PROGS))
