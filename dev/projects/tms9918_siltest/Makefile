# TMS_SilTest -- TMS9918 silicon validation suite for Parmigiani.
# Single 16 KB DRAM Apple-1 build. Loads at $0280 (Wozmon `280R`).

.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918

OBJS := TMS_SilTest.o tms9918_pad.o sprites_fauna.o

TMS_SilTest.o: TMS_SilTest.asm
	ca65 $(LIB) -o $@ $<

tms9918_pad.o: ../../lib/tms9918/tms9918_pad.asm
	ca65 $(LIB) -o $@ $<

sprites_fauna.o: ../../lib/tms9918/sprites_fauna.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/TMS_SilTest.bin: $(OBJS)
	ld65 -C apple1_siltest.cfg $^ -o $@

$(OUT_DIR)/TMS_SilTest.txt: $(OUT_DIR)/TMS_SilTest.bin
	python3 emit_TMS_SilTest_txt.py

all: $(OUT_DIR)/TMS_SilTest.bin $(OUT_DIR)/TMS_SilTest.txt

clean:
	rm -f *.o $(OUT_DIR)/TMS_SilTest.bin $(OUT_DIR)/TMS_SilTest.txt

.PHONY: all clean
