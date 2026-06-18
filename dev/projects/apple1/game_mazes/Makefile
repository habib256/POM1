.DEFAULT_GOAL := all
# Maze generators (Sidewinder + Recursive Backtracker)
LOAD_CFG := ../../../cc65/apple1_4k.cfg
OUT_DIR  := ../../../../software/Apple-1 games
include ../../../cc65/space.mk    # OUT_DIR_T (escaped form for make targets)
LIB      := -I ../../../lib/apple1 -I ../../../lib/m6502

PROGS    := Maze_Sidewinder Maze2_Backtracker
BINS     := $(addprefix $(OUT_DIR_T)/,$(addsuffix .bin,$(PROGS)))

.PHONY: all clean
all: $(BINS)

$(OUT_DIR_T)/%.bin: %.asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $*.o -o "$@"

clean:
	rm -f $(addsuffix .o,$(PROGS))
