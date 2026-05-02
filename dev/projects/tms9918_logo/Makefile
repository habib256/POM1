# TMS LOGO — links 3 modules: TMS_Logo + lib/m6502/math + lib/tms9918/tms9918m2.
# Link order matters so the entry point lands at $0280.
.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918

# --- V1.8 (8 KB, frozen) ---
OBJS := TMS_Logo.o math.o tms9918m2.o

TMS_Logo.o: TMS_Logo.asm
	ca65 $(LIB) -o $@ $<

math.o: ../../lib/m6502/math.asm
	ca65 $(LIB) -o $@ $<

tms9918m2.o: ../../lib/tms9918/tms9918m2.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/TMS_Logo.bin: $(OBJS)
	ld65 -C apple1_logo.cfg $^ -o $@

$(OUT_DIR)/TMS_Logo.txt: $(OUT_DIR)/TMS_Logo.bin
	python3 emit_TMS_Logo_txt.py

# --- V2.0 (16 KB, active dev) ---
OBJS_16K := TMS_Logo_16k.o math_16k.o tms9918m2_16k.o sprites_emotes_16k.o text_bitmap_16k.o bubble_16k.o buffer_editor_16k.o

TMS_Logo_16k.o: TMS_Logo_16k.asm
	ca65 $(LIB) -o $@ $<

math_16k.o: ../../lib/m6502/math.asm
	ca65 $(LIB) -o $@ $<

tms9918m2_16k.o: ../../lib/tms9918/tms9918m2.asm
	ca65 $(LIB) -o $@ $<

sprites_emotes_16k.o: ../../lib/tms9918/sprites_emotes.asm
	ca65 $(LIB) -o $@ $<

text_bitmap_16k.o: ../../lib/tms9918/text_bitmap.asm
	ca65 $(LIB) -o $@ $<

bubble_16k.o: ../../lib/tms9918/bubble.asm
	ca65 $(LIB) -o $@ $<

buffer_editor_16k.o: ../../lib/tms9918/buffer_editor.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/TMS_Logo_16k.bin: $(OBJS_16K)
	ld65 -C apple1_logo_16k.cfg $^ -o $@

$(OUT_DIR)/TMS_Logo_16k.txt: $(OUT_DIR)/TMS_Logo_16k.bin
	@python3 -c "import sys; \
data = open('$(OUT_DIR)/TMS_Logo_16k.bin','rb').read(); \
lines = []; addr = 0x0280; \
[ (lines.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in data[i:i+8])), None)[1] for i in range(0, len(data), 8) ]; \
lines.append('0280R'); \
open('$(OUT_DIR)/TMS_Logo_16k.txt','w').write('\n'.join(lines)+'\n'); \
print(f'Wrote $(OUT_DIR)/TMS_Logo_16k.txt ({len(data)} bytes)')"

.PHONY: all clean v18 v20
all: v18
v18: $(OUT_DIR)/TMS_Logo.txt
v20: $(OUT_DIR)/TMS_Logo_16k.txt

clean:
	rm -f $(OBJS) $(OBJS_16K)
