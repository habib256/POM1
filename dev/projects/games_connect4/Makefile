# Connect 4 (text mode)
.DEFAULT_GOAL := all
PROJECT  := Connect4
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
