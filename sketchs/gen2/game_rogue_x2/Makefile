# game_rogue_x2 (GEN2) — x2 COLOUR variant of the HGR Rogue port:
# 8x5-cell dead-zone camera viewport, coloured 28x32 assets, identical
# HUD. Single-region image at $6000 (chess model).
#
#   make          -> "software/Graphic HGR"/HGR_RogueX2.{bin,txt}  (run 6000R)
#   make assets   -> regenerate both asset packs from the TMS art
#   make clean

PROJECT  := HGR_RogueX2
OUT_DIR  := ../../../software/Graphic HGR
LIB      := -I ../../../dev/lib/apple1 -I ../../../dev/lib/gen2 \
            -I ../../../dev/lib/m6502 -I ../../../dev/lib/games/rogue \
            -I ../game_rogue
CFG      := apple1_rogue_x2_hgr.cfg

include ../../../dev/cc65/space.mk

CC65_BIN ?= $(shell command -v ca65 2>/dev/null)
ifeq ($(CC65_BIN),)
$(error cc65 not found on PATH. Install with: apt-get install cc65 / brew install cc65, or set CC65_BIN)
endif

.PHONY: all assets clean
all: $(OUT_DIR_T)/$(PROJECT).txt

$(PROJECT).o: $(PROJECT).asm rogue_assets_hgr_x2.inc $(CFG)
	ca65 $(LIB) $(PROJECT).asm -o $(PROJECT).o

$(PROJECT).rawbin: $(PROJECT).o $(CFG)
	ld65 -C $(CFG) $(PROJECT).o -o $(PROJECT).rawbin

$(OUT_DIR_T)/$(PROJECT).txt: $(PROJECT).rawbin
	mkdir -p "$(OUT_DIR)"
	python3 ../../../tools/emit_gen2_txt.py \
	  $(PROJECT).rawbin "$(OUT_DIR)/$(PROJECT).bin" \
	  "$(OUT_DIR)/$(PROJECT).txt" --start 0x6000

assets:
	python3 ../../../tools/build_rogue_hgr_assets.py

clean:
	rm -f $(PROJECT).o $(PROJECT).rawbin "$(OUT_DIR)/$(PROJECT).bin" "$(OUT_DIR)/$(PROJECT).txt"
