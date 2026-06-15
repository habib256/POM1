# Apple 1 Chess (TMS9918) — dual-bank build (lower 4K + upper 4K, per Parmegiani)
.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/Graphic TMS9918
empty     :=
space     := $(empty) $(empty)
OUT_DIR_T := $(subst $(space),\$(space),$(OUT_DIR))
LIB      := -I ../../lib/apple1 -I ../../lib/games/chess -I ../../lib/tms9918

OBJS := TMS_Chess.o chess_engine.o chess_text_io.o tms9918m1.o tms9918_pad.o

TMS_Chess.o: TMS_Chess.asm
	ca65 $(LIB) -o "$@" $<

chess_engine.o: ../../lib/games/chess/chess_engine.asm
	ca65 $(LIB) -o "$@" $<

chess_text_io.o: ../../lib/games/chess/chess_text_io.asm
	ca65 $(LIB) -o "$@" $<

tms9918m1.o: ../../lib/tms9918/tms9918m1.asm
	ca65 $(LIB) -o "$@" $<

tms9918_pad.o: ../../lib/tms9918/tms9918_pad.asm
	ca65 $(LIB) -o "$@" $<

$(OUT_DIR_T)/TMS_Chess.bin.lo $(OUT_DIR_T)/TMS_Chess.bin.hi: $(OBJS)
	ld65 -C apple1_chess_tms.cfg $(OBJS) -o "$(OUT_DIR)"/TMS_Chess.bin
	@rm -f "$(OUT_DIR)"/TMS_Chess.bin   # ld65 leaves an empty placeholder; remove it

$(OUT_DIR_T)/TMS_Chess.txt: $(OUT_DIR_T)/TMS_Chess.bin.lo $(OUT_DIR_T)/TMS_Chess.bin.hi
	@python3 ../../cc65/emit_dualbank.py "$(OUT_DIR)"/TMS_Chess.bin.lo "$(OUT_DIR)"/TMS_Chess.bin.hi "$(OUT_DIR)"/TMS_Chess.txt --run 0280

.PHONY: all clean
all: $(OUT_DIR_T)/TMS_Chess.txt

clean:
	rm -f $(OBJS) "$(OUT_DIR)"/TMS_Chess.bin*
