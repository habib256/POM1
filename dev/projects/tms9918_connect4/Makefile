# TMS9918 Connect 4
.DEFAULT_GOAL := all
PROJECT  := TMS_Connect4
LOAD_CFG := ../../cc65/apple1.cfg
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
