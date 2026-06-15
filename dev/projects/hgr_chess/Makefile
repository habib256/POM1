# Apple 1 Chess (GEN2 HGR) — dual-bank build (lower 4K + upper 4K, per Parmegiani)
.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/Graphic HGR
empty     :=
space     := $(empty) $(empty)
OUT_DIR_T := $(subst $(space),\$(space),$(OUT_DIR))
LIB      := -I ../../lib/apple1 -I ../../lib/games/chess

OBJS := HGR_Chess.o chess_engine.o chess_text_io.o

HGR_Chess.o: HGR_Chess.asm
	ca65 $(LIB) -o "$@" $<

chess_engine.o: ../../lib/games/chess/chess_engine.asm
	ca65 $(LIB) -o "$@" $<

chess_text_io.o: ../../lib/games/chess/chess_text_io.asm
	ca65 $(LIB) -o "$@" $<

$(OUT_DIR_T)/HGR_Chess.bin.lo $(OUT_DIR_T)/HGR_Chess.bin.hi: $(OBJS)
	ld65 -C apple1_chess_hgr.cfg $(OBJS) -o "$(OUT_DIR)"/HGR_Chess.bin
	@rm -f "$(OUT_DIR)"/HGR_Chess.bin   # ld65 leaves an empty placeholder; remove it

$(OUT_DIR_T)/HGR_Chess.txt: $(OUT_DIR_T)/HGR_Chess.bin.lo $(OUT_DIR_T)/HGR_Chess.bin.hi
	@python3 ../../cc65/emit_dualbank.py "$(OUT_DIR)"/HGR_Chess.bin.lo "$(OUT_DIR)"/HGR_Chess.bin.hi "$(OUT_DIR)"/HGR_Chess.txt --run E000

.PHONY: all clean
all: $(OUT_DIR_T)/HGR_Chess.txt

clean:
	rm -f $(OBJS) "$(OUT_DIR)"/HGR_Chess.bin*
