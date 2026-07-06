# game_chess (GEN2) — graphical HGR Chess, single-region image at $6000.
#
# The GEN2 "HGR Color" machine has 48 KB RAM ($0000-$BFFF); the card carves its
# two HGR pages out at $2000-$5FFF. This game therefore lives in the contiguous
# RAM ABOVE HGR page 2 ($6000+), one single binary (no dual-bank split), and
# draws into HGR page 1 ($2000-$3FFF). Links the shared chess engine.
#
#   make          -> "software/Graphic HGR"/GEN2_Chess.{bin,txt}   (run 6000R)
#   make clean
#
# The .txt lands under software/Graphic HGR/, so POM1's auto-enable-by-directory
# plugs the GEN2 card when you File > Load it. In the in-app DevBench, just open
# GEN2_Chess.asm (the .sketch.json wires the cfg + engine + CHESS_SMART_EVAL).

PROJECT  := GEN2_Chess
ENGINE   := ../../../dev/lib/games/chess
OUT_DIR  := ../../../software/Graphic HGR
LIB      := -I ../../../dev/lib/apple1 -I ../../../dev/lib/games/chess -I ../../../dev/lib/gen2
CFG      := apple1_gen2_chess.cfg
DEFS     := -D CHESS_SMART_EVAL

# OUT_DIR_T — space-escaped form for make target/prerequisite positions.
include ../../../dev/cc65/space.mk

CC65_BIN ?= $(shell command -v ca65 2>/dev/null)
ifeq ($(CC65_BIN),)
$(error cc65 not found on PATH. Install with: apt-get install cc65 / brew install cc65, or set CC65_BIN)
endif

OBJS := GEN2_Chess.o chess_engine.o

.PHONY: all clean
all: $(OUT_DIR_T)/$(PROJECT).txt

GEN2_Chess.o: GEN2_Chess.asm apple1_gen2_chess.cfg
	ca65 $(LIB) $(DEFS) GEN2_Chess.asm -o GEN2_Chess.o
chess_engine.o: $(ENGINE)/chess_engine.asm
	ca65 $(LIB) $(DEFS) $(ENGINE)/chess_engine.asm -o chess_engine.o

$(PROJECT).rawbin: $(OBJS) $(CFG)
	ld65 -C $(CFG) $(OBJS) -o $(PROJECT).rawbin

$(OUT_DIR_T)/$(PROJECT).txt: $(PROJECT).rawbin
	mkdir -p "$(OUT_DIR)"
	python3 ../../../tools/emit_gen2_txt.py \
	  $(PROJECT).rawbin "$(OUT_DIR)/$(PROJECT).bin" \
	  "$(OUT_DIR)/$(PROJECT).txt" --start 0x6000

clean:
	rm -f $(OBJS) $(PROJECT).rawbin "$(OUT_DIR)/$(PROJECT).bin" "$(OUT_DIR)/$(PROJECT).txt"
