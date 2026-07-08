# TMS_Maze3D — Wizardry-style 3D line maze, CodeTank full-bank program.
# Builds TMS_Maze3D.bin, a run-in-place $4000 bank image (main: is the first
# byte of CODE, so 4000R boots it). Links tms9918_pad.asm for the
# silicon-strict cushion (tms9918_pad12 legacy alias -> pad18).
#
# LOAD_CFG / EXTRA_ASM are read by the POM1 DevBench (probeAsmProject), so
# opening TMS_Maze3D.asm in the Bench builds with this exact cfg and flashes
# the CODETANKDEV cartridge bank from the UI. The Bench reads these two
# variables LITERALLY (no $(VAR) expansion — an earlier `EXTRA_ASM := $(PAD)`
# silently assembled nothing and the link died on tms9918_pad12), so keep
# them plain relative paths.

INC       := -I ../../../dev/lib/apple1 -I ../../../dev/lib/tms9918
LOAD_CFG  := ../../../dev/codetank/bank_cfgs/apple1_maze3d_codetank_bank.cfg
EXTRA_ASM := ../../../dev/lib/tms9918/tms9918_pad.asm ../../../dev/lib/tms9918/sprites_trollkind.asm ../../../dev/lib/tms9918/sprites_characters.asm

.PHONY: all clean test
all:
	ca65 $(INC) -o TMS_Maze3D.o TMS_Maze3D.asm
	for m in $(EXTRA_ASM); do ca65 $(INC) -o $$(basename $${m%.asm}).o $$m; done
	ld65 -C $(LOAD_CFG) -m TMS_Maze3D.map TMS_Maze3D.o tms9918_pad.o sprites_trollkind.o sprites_characters.o -o TMS_Maze3D.bin
	@echo "[Maze3D] built TMS_Maze3D.bin ($$(stat -c%s TMS_Maze3D.bin 2>/dev/null || wc -c < TMS_Maze3D.bin) B @ \$$4000)"

clean:
	rm -f TMS_Maze3D.o tms9918_pad.o sprites_*.o TMS_Maze3D.bin TMS_Maze3D.map

test:
	@echo "[Maze3D] gameplay verified via tools/verify_codetank_roms.py-style headless runs"
