# TMS Asteroids -- 16 KB DRAM Apple-1 build.
# Links: TMS_Asteroids + lib/m6502/math + lib/tms9918/tms9918m2
#                     + lib/tms9918/sprite_triangle
.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918

OBJS := TMS_Asteroids.o math.o tms9918m2.o sprite_triangle.o tms9918_pad.o

TMS_Asteroids.o: TMS_Asteroids.asm
	ca65 $(LIB) -o $@ $<

math.o: ../../lib/m6502/math.asm
	ca65 $(LIB) -o $@ $<

tms9918m2.o: ../../lib/tms9918/tms9918m2.asm
	ca65 $(LIB) -o $@ $<

sprite_triangle.o: ../../lib/tms9918/sprite_triangle.asm
	ca65 $(LIB) -o $@ $<

tms9918_pad.o: ../../lib/tms9918/tms9918_pad.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/TMS_Asteroids.bin: $(OBJS)
	ld65 -C apple1_asteroids.cfg $^ -o $@

$(OUT_DIR)/TMS_Asteroids.txt: $(OUT_DIR)/TMS_Asteroids.bin
	python3 emit_TMS_Asteroids_txt.py

.PHONY: all clean
all: $(OUT_DIR)/TMS_Asteroids.txt

clean:
	rm -f $(OBJS)
