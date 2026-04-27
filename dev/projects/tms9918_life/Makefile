# TMS9918 Life — default = stock cassette variant.
# Override at the command line: `make CFG=apple1_life_codetank_bank.cfg`.
.DEFAULT_GOAL := all
PROJECT  := TMS_Life
CFG      ?= ../../cc65/apple1_gen2.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

$(OUT_DIR)/$(PROJECT).txt: $(OUT_DIR)/$(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).txt

clean:
	rm -f $(PROJECT).o
