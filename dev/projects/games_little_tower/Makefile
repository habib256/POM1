# Little Tower v1.0
.DEFAULT_GOAL := all
PROJECT  := LittleTower-1.0
LOAD_CFG := ../../cc65/apple1.cfg
OUT_DIR  := ../../../software/games
LIB      := -I ../../lib/apple1

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
