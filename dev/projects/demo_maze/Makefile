.DEFAULT_GOAL := all
# Maze generators (Sidewinder + Recursive Backtracker)
LOAD_CFG := ../../cc65/apple1_4k.cfg
OUT_DIR  := ../../../software/games
LIB      := -I ../../lib/apple1 -I ../../lib/m6502

PROGS    := Maze_Sidewinder Maze2_Backtracker
BINS     := $(addprefix $(OUT_DIR)/,$(addsuffix .bin,$(PROGS)))

.PHONY: all clean
all: $(BINS)

$(OUT_DIR)/%.bin: %.asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $*.o -o $@

clean:
	rm -f $(addsuffix .o,$(PROGS))
