# Sokoban (text mode) — default = stock 4K variant.
# Override at the command line: `make CFG=apple1_sok_8k.cfg` etc.
.DEFAULT_GOAL := all
PROJECT  := Sokoban
CFG      ?= apple1_sok_4k.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/games
LIB      := -I ../../lib/apple1 -I ../../lib/sokoban

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
