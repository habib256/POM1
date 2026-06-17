# Apple 1 Chess (text) — dual-bank build (lower 4K + upper 4K, per Parmegiani)
.DEFAULT_GOAL := all
OUT_DIR  := ../../../../software/Apple-1 games
empty     :=
space     := $(empty) $(empty)
OUT_DIR_T := $(subst $(space),\$(space),$(OUT_DIR))
LIB      := -I ../../../lib/apple1 -I ../../../lib/games/chess

OBJS := Chess.o chess_engine.o chess_text_io.o

Chess.o: Chess.asm
	ca65 $(LIB) -o "$@" $<

chess_engine.o: ../../../lib/games/chess/chess_engine.asm
	ca65 $(LIB) -o "$@" $<

chess_text_io.o: ../../../lib/games/chess/chess_text_io.asm
	ca65 $(LIB) -o "$@" $<

# ld65 emits two binaries (Chess.bin.lo for $0280-$0FFF, Chess.bin.hi for $E000-$EFFF)
# plus Chess.sym (VICE-format label file) consumed by tests/chess_engine_perft_smoke_test.
$(OUT_DIR_T)/Chess.bin.lo $(OUT_DIR_T)/Chess.bin.hi $(OUT_DIR_T)/Chess.sym: $(OBJS)
	ld65 -C apple1_chess_text.cfg -Ln "$(OUT_DIR)"/Chess.sym $(OBJS) -o "$(OUT_DIR)"/Chess.bin
	@rm -f "$(OUT_DIR)"/Chess.bin       # ld65 leaves an empty placeholder; remove it

# Combined Wozmon-hex .txt with both blocks (low at $0280, high at $E000).
# Wozmon's address-prefix syntax handles the 53 KB jump cleanly — it just
# resets its write pointer when it sees a new "AAAA:" prefix.
$(OUT_DIR_T)/Chess.txt: $(OUT_DIR_T)/Chess.bin.lo $(OUT_DIR_T)/Chess.bin.hi
	@python3 ../../../cc65/emit_dualbank.py "$(OUT_DIR)"/Chess.bin.lo "$(OUT_DIR)"/Chess.bin.hi "$(OUT_DIR)"/Chess.txt --run 0280

.PHONY: all clean
all: $(OUT_DIR_T)/Chess.txt

clean:
	rm -f $(OBJS) "$(OUT_DIR)"/Chess.bin*
