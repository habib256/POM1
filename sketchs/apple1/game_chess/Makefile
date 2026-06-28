# game_chess — Apple-1 text-mode Chess (dual-bank: CODELOW @ $0280, ENGINE @ $E000).
#
# Unlike the single-bank template, the link config splits the image into two
# files (%O.lo / %O.hi), so this Makefile links them and then folds both address
# zones into one Wozmon-hex Chess.txt (emit_Chess_txt.py). Self-play telemetry
# instrumentation ($C440-$C443) lets tools/test_chess_selfplay.py watch an
# AI-vs-AI game headlessly — see doc/TELEMETRY_SIDE_CHANNEL.md.
#
#   make            -> "software/Apple-1 games"/Chess.{bin.lo,bin.hi,txt}
#   make test       -> python3 tools/test_chess_selfplay.py (skips w/o cc65/POM1)
#   make clean

PROJECT  := Chess
ENGINE   := ../../../dev/lib/games/chess
OUT_DIR  := ../../../software/Apple-1 games
LIB      := -I ../../../dev/lib/apple1 -I ../../../dev/lib/games/chess -I ../../../dev/lib/telemetry
CFG      := apple1_chess_text.cfg

# OUT_DIR_T — space-escaped form for make target/prerequisite positions.
include ../../../dev/cc65/space.mk

CC65_BIN ?= $(shell command -v ca65 2>/dev/null)
ifeq ($(CC65_BIN),)
$(error cc65 not found on PATH. Install with: apt-get install cc65 / brew install cc65, or set CC65_BIN)
endif

OBJS := Chess.o chess_engine.o chess_text_io.o

.PHONY: all clean test
all: $(OUT_DIR_T)/$(PROJECT).txt

Chess.o: Chess.asm
	ca65 $(LIB) Chess.asm -o Chess.o
chess_engine.o: $(ENGINE)/chess_engine.asm
	ca65 $(LIB) $(ENGINE)/chess_engine.asm -o chess_engine.o
chess_text_io.o: $(ENGINE)/chess_text_io.asm
	ca65 $(LIB) $(ENGINE)/chess_text_io.asm -o chess_text_io.o

# ld65 honours the cfg's `file = "%O.lo"/"%O.hi"` directives, so -o <base>.bin
# actually writes <base>.bin.lo + <base>.bin.hi (and an empty <base>.bin). The
# .txt rule depends on the .bin marker; both banks are (re)built alongside it.
$(OUT_DIR_T)/$(PROJECT).bin: $(OBJS) $(CFG)
	mkdir -p "$(OUT_DIR)"
	ld65 -C $(CFG) $(OBJS) -o "$(OUT_DIR)/$(PROJECT).bin"

$(OUT_DIR_T)/$(PROJECT).txt: $(OUT_DIR_T)/$(PROJECT).bin emit_Chess_txt.py
	python3 emit_Chess_txt.py \
	  --lo "$(OUT_DIR)/$(PROJECT).bin.lo" \
	  --hi "$(OUT_DIR)/$(PROJECT).bin.hi" \
	  --txt "$(OUT_DIR)/$(PROJECT).txt"
	@rm -f "$(OUT_DIR)/$(PROJECT).bin"   # empty ld65 base file (banks are .lo/.hi)

test: all
	cd ../../.. && python3 tools/test_chess_selfplay.py

# Deeper engine-quality gate: validate self-play moves against python-chess
# (legality + move-generation perft + mate/stalemate). Skips (77) without the
# oracle. Run more games with: make quality GAMES=40
GAMES ?= 20
quality: all
	cd ../../.. && python3 tools/test_chess_quality.py $(GAMES)

clean:
	rm -f $(OBJS) \
	      "$(OUT_DIR)/$(PROJECT).bin" "$(OUT_DIR)/$(PROJECT).bin.lo" \
	      "$(OUT_DIR)/$(PROJECT).bin.hi" "$(OUT_DIR)/$(PROJECT).txt"
