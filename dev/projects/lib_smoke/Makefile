# LibSmoke — validation of the unified ZP convention for lib/apple1 + lib/m6502.
# Multi-object link: LibSmoke.o (with textual lib includes) + math.o
# (separately compiled, exercising the .importzp / .exportzp cross-object path).
.DEFAULT_GOAL := all
PROJECT  := LibSmoke
LOAD_CFG := apple1_smoke.cfg
OUT_DIR  := ../../../software/dev
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/text40

OBJS := $(PROJECT).o math.o

$(PROJECT).o: $(PROJECT).asm
	ca65 $(LIB) -o $@ $<

math.o: ../../lib/m6502/math.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/$(PROJECT).bin: $(OBJS)
	ld65 -C $(LOAD_CFG) $^ -o $@

$(OUT_DIR)/$(PROJECT).txt: $(OUT_DIR)/$(PROJECT).bin
	@python3 -c "import sys; \
data = open('$(OUT_DIR)/$(PROJECT).bin','rb').read(); \
lines = []; addr = 0x0280; \
[ (lines.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in data[i:i+16])), None)[1] for i in range(0, len(data), 16) ]; \
lines.append('0280R'); \
open('$(OUT_DIR)/$(PROJECT).txt','w').write('\n'.join(lines)+'\n'); \
print(f'Wrote $(OUT_DIR)/$(PROJECT).txt ({len(data)} bytes)')"

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).txt

clean:
	rm -f $(OBJS)
