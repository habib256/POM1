# TMS9918 Snake — default = stock cassette ($0280) variant.
# Override at the command line: `make CFG=apple1_snake_codetank_bank.cfg`.
.DEFAULT_GOAL := all
PROJECT  := TMS_Snake
CFG      ?= apple1_snake.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

$(OUT_DIR)/$(PROJECT).txt: $(OUT_DIR)/$(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).txt

clean:
	rm -f $(PROJECT).o
