# Apple 1 Chess (TMS9918) — dual-bank build (lower 4K + upper 4K, per Parmegiani)
.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/chess -I ../../lib/tms9918

OBJS := TMS_Chess.o chess_engine.o chess_text_io.o tms9918m1.o

TMS_Chess.o: TMS_Chess.asm
	ca65 $(LIB) -o $@ $<

chess_engine.o: ../../lib/chess/chess_engine.asm
	ca65 $(LIB) -o $@ $<

chess_text_io.o: ../../lib/chess/chess_text_io.asm
	ca65 $(LIB) -o $@ $<

tms9918m1.o: ../../lib/tms9918/tms9918m1.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/TMS_Chess.bin.lo $(OUT_DIR)/TMS_Chess.bin.hi: $(OBJS)
	ld65 -C apple1_chess_tms.cfg $(OBJS) -o $(OUT_DIR)/TMS_Chess.bin
	@rm -f $(OUT_DIR)/TMS_Chess.bin   # ld65 leaves an empty placeholder; remove it

$(OUT_DIR)/TMS_Chess.txt: $(OUT_DIR)/TMS_Chess.bin.lo $(OUT_DIR)/TMS_Chess.bin.hi
	@python3 -c "lo=open('$(OUT_DIR)/TMS_Chess.bin.lo','rb').read(); \
hi=open('$(OUT_DIR)/TMS_Chess.bin.hi','rb').read(); \
out=[]; addr=0x0280; \
[(out.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in lo[i:i+8])), None)[1] for i in range(0, len(lo), 8)]; \
addr=0xE000; \
[(out.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in hi[i:i+8])), None)[1] for i in range(0, len(hi), 8)]; \
out.append('0280R'); \
open('$(OUT_DIR)/TMS_Chess.txt','w').write('\n'.join(out)+'\n'); \
print(f'Wrote $(OUT_DIR)/TMS_Chess.txt (lo={len(lo)} B at 0280, hi={len(hi)} B at E000)')"

.PHONY: all clean
all: $(OUT_DIR)/TMS_Chess.txt

clean:
	rm -f $(OBJS) $(OUT_DIR)/TMS_Chess.bin*
