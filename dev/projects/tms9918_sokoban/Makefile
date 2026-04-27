# TMS9918 Sokoban — default = standalone CodeTank variant ($4000).
# Override at the command line: `make CFG=apple1_sokoban_codetank_bank.cfg`.
.DEFAULT_GOAL := all
PROJECT  := TMS_Sokoban
CFG      ?= apple1_sokoban_codetank.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/sokoban

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
