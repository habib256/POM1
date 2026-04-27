# HGR10 Life
.DEFAULT_GOAL := all
PROJECT  := HGR10_Life
LOAD_CFG := ../../cc65/apple1_gen2.cfg
OUT_DIR  := ../../../software/hgr
LIB      := -I ../../lib/apple1 -I ../../lib/hgr

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

$(OUT_DIR)/$(PROJECT).txt: $(OUT_DIR)/$(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).txt

clean:
	rm -f $(PROJECT).o
