# HGR Maze (Recursive Backtracker)
.DEFAULT_GOAL := all
PROJECT  := HGR_Maze
LOAD_CFG := ../../cc65/apple1_gen2.cfg
OUT_DIR  := ../../../software/hgr
LIB      := -I ../../lib/apple1 -I ../../lib/m6502

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
