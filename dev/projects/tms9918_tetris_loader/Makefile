# CodeTank upper-bank Tetris loader (128 B at $4000)
.DEFAULT_GOAL := all
PROJECT  := tetris_loader
LOAD_CFG := apple1_tetris_loader.cfg
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
