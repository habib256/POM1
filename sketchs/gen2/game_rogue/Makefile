# game_rogue (GEN2) — HGR port of the TMS9918 roguelike, single-region
# image at $6000 (chess model).
#
# The GEN2 "HGR Color" machine has 48 KB RAM ($0000-$BFFF); the card carves
# its two HGR pages out at $2000-$5FFF. The game lives in the contiguous RAM
# above page 2 ($6000+) and draws into HGR page 1 ($2000-$3FFF).
#
#   make          -> "software/Graphic HGR"/HGR_Rogue.{bin,txt}   (run 6000R)
#   make assets   -> regenerate rogue_assets_hgr.inc from the TMS art
#   make clean
#
# The .txt lands under software/Graphic HGR/, so POM1's auto-enable-by-
# directory plugs the GEN2 card when you File > Load it. In the in-app
# DevBench, just open HGR_Rogue.asm (the .sketch.json wires the cfg).

PROJECT  := HGR_Rogue
OUT_DIR  := ../../../software/Graphic HGR
LIB      := -I ../../../dev/lib/apple1 -I ../../../dev/lib/gen2 \
            -I ../../../dev/lib/m6502 -I ../../../dev/lib/games/rogue
CFG      := apple1_rogue_hgr.cfg

# OUT_DIR_T — space-escaped form for make target/prerequisite positions.
include ../../../dev/cc65/space.mk

CC65_BIN ?= $(shell command -v ca65 2>/dev/null)
ifeq ($(CC65_BIN),)
$(error cc65 not found on PATH. Install with: apt-get install cc65 / brew install cc65, or set CC65_BIN)
endif

.PHONY: all assets clean
all: $(OUT_DIR_T)/$(PROJECT).txt

$(PROJECT).o: $(PROJECT).asm rogue_assets_hgr.inc $(CFG)
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
