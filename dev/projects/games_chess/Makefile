# Apple 1 Chess (text) — dual-bank build (lower 4K + upper 4K, per Parmegiani)
.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/games
LIB      := -I ../../lib/apple1 -I ../../lib/chess

OBJS := Chess.o chess_engine.o chess_text_io.o

Chess.o: Chess.asm
	ca65 $(LIB) -o $@ $<

chess_engine.o: ../../lib/chess/chess_engine.asm
	ca65 $(LIB) -o $@ $<

chess_text_io.o: ../../lib/chess/chess_text_io.asm
	ca65 $(LIB) -o $@ $<

# ld65 emits two binaries (Chess.bin.lo for $0280-$0FFF, Chess.bin.hi for $E000-$EFFF)
# plus Chess.sym (VICE-format label file) consumed by tests/chess_engine_perft_smoke_test.
$(OUT_DIR)/Chess.bin.lo $(OUT_DIR)/Chess.bin.hi $(OUT_DIR)/Chess.sym: $(OBJS)
	ld65 -C apple1_chess_text.cfg -Ln $(OUT_DIR)/Chess.sym $(OBJS) -o $(OUT_DIR)/Chess.bin
	@rm -f $(OUT_DIR)/Chess.bin       # ld65 leaves an empty placeholder; remove it

# Combined Wozmon-hex .txt with both blocks (low at $0280, high at $E000).
# Wozmon's address-prefix syntax handles the 53 KB jump cleanly — it just
# resets its write pointer when it sees a new "AAAA:" prefix.
$(OUT_DIR)/Chess.txt: $(OUT_DIR)/Chess.bin.lo $(OUT_DIR)/Chess.bin.hi
	@python3 -c "lo=open('$(OUT_DIR)/Chess.bin.lo','rb').read(); \
hi=open('$(OUT_DIR)/Chess.bin.hi','rb').read(); \
out=[]; addr=0x0280; \
[(out.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in lo[i:i+8])), None)[1] for i in range(0, len(lo), 8)]; \
addr=0xE000; \
[(out.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in hi[i:i+8])), None)[1] for i in range(0, len(hi), 8)]; \
out.append('0280R'); \
open('$(OUT_DIR)/Chess.txt','w').write('\n'.join(out)+'\n'); \
print(f'Wrote $(OUT_DIR)/Chess.txt (lo={len(lo)} B at \$\$0280, hi={len(hi)} B at \$\$E000)')"

.PHONY: all clean
all: $(OUT_DIR)/Chess.txt

clean:
	rm -f $(OBJS) $(OUT_DIR)/Chess.bin*
