# HGR Sokoban — split-bank build (4 KB low + 4 KB high) for the
# Parmigiani 8 KB dual-bank Apple-1 + GEN2 HGR card.
#
# Pattern lifted from dev/projects/hgr_chess/Makefile: ld65 emits two .bin
# files (one per memory region), then a Python one-liner stitches both
# into a single Wozmon-hex .txt that loads both halves and runs E000R.
.DEFAULT_GOAL := all

PROJECT  := HGR_Sokoban
LOAD_CFG := ../games_sokoban/apple1_sok_hgr.cfg
OUT_DIR  := ../../../software/hgr
LIB      := -I ../../lib/apple1 -I ../../lib/hgr -I ../../lib/games/sokoban -I ../../lib/m6502

OBJS := $(PROJECT).o

$(PROJECT).o: $(PROJECT).asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/$(PROJECT).bin.lo $(OUT_DIR)/$(PROJECT).bin.hi: $(OBJS)
	ld65 -C $(LOAD_CFG) $(OBJS) -o $(OUT_DIR)/$(PROJECT).bin
	@rm -f $(OUT_DIR)/$(PROJECT).bin   # ld65 leaves an empty placeholder; remove it

$(OUT_DIR)/$(PROJECT).txt: $(OUT_DIR)/$(PROJECT).bin.lo $(OUT_DIR)/$(PROJECT).bin.hi
	@python3 -c "lo=open('$(OUT_DIR)/$(PROJECT).bin.lo','rb').read(); \
hi=open('$(OUT_DIR)/$(PROJECT).bin.hi','rb').read(); \
out=['// HGR Sokoban — Parmigiani 8 KB dual-bank + GEN2']; addr=0x0280; \
[(out.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in lo[i:i+8])), None)[1] for i in range(0, len(lo), 8)]; \
addr=0xE000; \
[(out.append(f'{addr+i:04X}: ' + ' '.join(f'{b:02X}' for b in hi[i:i+8])), None)[1] for i in range(0, len(hi), 8)]; \
out.append('E000R'); \
open('$(OUT_DIR)/$(PROJECT).txt','w').write('\n'.join(out)+'\n'); \
print(f'Wrote $(OUT_DIR)/$(PROJECT).txt (lo={len(lo)} B at 0280, hi={len(hi)} B at E000)')"

.PHONY: all clean test
all: $(OUT_DIR)/$(PROJECT).txt

clean:
	rm -f $(OBJS) $(OUT_DIR)/$(PROJECT).bin*

TEST_CMD ?= @echo "[$(PROJECT)] no smoke test wired"
test: all
	$(TEST_CMD)
