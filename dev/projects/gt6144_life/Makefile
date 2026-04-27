# GT-6144 Life
.DEFAULT_GOAL := all
PROJECT  := GT1_Life
LOAD_CFG := gt6144.cfg
OUT_DIR  := ../../../software/gt-6144
LIB      := -I ../../lib/apple1

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
