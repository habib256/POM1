# TMS9918 Maze 3D
.DEFAULT_GOAL := all
PROJECT  := TMS_Maze3D
LOAD_CFG := apple1_maze3d.cfg
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $(LIB) $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

$(OUT_DIR)/$(PROJECT).txt: $(OUT_DIR)/$(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).txt

clean:
	rm -f $(PROJECT).o
