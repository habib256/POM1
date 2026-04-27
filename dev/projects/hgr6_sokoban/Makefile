# HGR6 Sokoban — re-uses the games_sokoban linker config + lib/sokoban data.
.DEFAULT_GOAL := all
PROJECT  := HGR6_Sokoban
LOAD_CFG := ../games_sokoban/apple1_sok_hgr.cfg
OUT_DIR  := ../../../software/hgr
LIB      := -I ../../lib/apple1 -I ../../lib/hgr -I ../../lib/sokoban -I ../../lib/m6502

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
