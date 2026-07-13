# game_maze3d (GEN2) — HGR port of the Wizardry-style 3D line maze,
# single-region image at $6000 (chess model).
#
#   make          -> "software/Graphic HGR"/HGR_Maze3D.{bin,txt}   (run 6000R)
#   make clean
#
# Links the SCROLL-O-SPRITES monster patterns from the TMS lib — they are
# pure pattern data (bit 7 = leftmost); the blitters convert to HGR bit
# order at draw time via rev7_tab, so no asset conversion step exists.

PROJECT  := HGR_Maze3D
OUT_DIR  := ../../../software/Graphic HGR
LIB      := -I ../../../dev/lib/apple1 -I ../../../dev/lib/gen2
CFG      := apple1_maze3d_hgr.cfg
SPRITES  := ../../../dev/lib/tms9918/sprites_trollkind.asm \
            ../../../dev/lib/tms9918/sprites_characters.asm

# OUT_DIR_T — space-escaped form for make target/prerequisite positions.
include ../../../dev/cc65/space.mk

CC65_BIN ?= $(shell command -v ca65 2>/dev/null)
ifeq ($(CC65_BIN),)
$(error cc65 not found on PATH. Install with: apt-get install cc65 / brew install cc65, or set CC65_BIN)
endif

OBJS := HGR_Maze3D.o sprites_trollkind.o sprites_characters.o

.PHONY: all clean
all: $(OUT_DIR_T)/$(PROJECT).txt

HGR_Maze3D.o: HGR_Maze3D.asm $(CFG)
	ca65 $(LIB) HGR_Maze3D.asm -o HGR_Maze3D.o
sprites_trollkind.o: ../../../dev/lib/tms9918/sprites_trollkind.asm
	ca65 $(LIB) $< -o $@
sprites_characters.o: ../../../dev/lib/tms9918/sprites_characters.asm
	ca65 $(LIB) $< -o $@

$(PROJECT).rawbin: $(OBJS) $(CFG)
	ld65 -C $(CFG) $(OBJS) -o $(PROJECT).rawbin

$(OUT_DIR_T)/$(PROJECT).txt: $(PROJECT).rawbin
	mkdir -p "$(OUT_DIR)"
	python3 ../../../tools/emit_gen2_txt.py \
	  $(PROJECT).rawbin "$(OUT_DIR)/$(PROJECT).bin" \
	  "$(OUT_DIR)/$(PROJECT).txt" --start 0x6000

clean:
	rm -f $(OBJS) $(PROJECT).rawbin "$(OUT_DIR)/$(PROJECT).bin" "$(OUT_DIR)/$(PROJECT).txt"
