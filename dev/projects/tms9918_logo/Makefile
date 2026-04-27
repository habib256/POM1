# TMS LOGO — links 3 modules: TMS_Logo + lib/m6502/math + lib/tms9918/tms9918m2.
# Link order matters so the entry point lands at $0280.
.DEFAULT_GOAL := all
PROJECT  := TMS_Logo
LOAD_CFG := apple1_logo.cfg
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918

OBJS := TMS_Logo.o math.o tms9918m2.o

TMS_Logo.o: TMS_Logo.asm
	ca65 $(LIB) -o $@ $<

math.o: ../../lib/m6502/math.asm
	ca65 $(LIB) -o $@ $<

tms9918m2.o: ../../lib/tms9918/tms9918m2.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/$(PROJECT).bin: $(OBJS)
	ld65 -C $(LOAD_CFG) $^ -o $@

$(OUT_DIR)/$(PROJECT).txt: $(OUT_DIR)/$(PROJECT).bin
	python3 emit_$(PROJECT)_txt.py

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).txt

clean:
	rm -f $(OBJS)
