# CodeTank lower-bank menu (256 B at $4000)
.DEFAULT_GOAL := all
PROJECT  := codetank_menu
LOAD_CFG := apple1_codetank_menu.cfg
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
