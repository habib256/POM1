# TMS_Maze3D — Wizardry-style 3D line maze, CodeTank full-bank program.
# Builds TMS_Maze3D.bin, a run-in-place $4000 bank image (main: is the first
# byte of CODE, so 4000R boots it). Links tms9918_pad.asm for the
# silicon-strict cushion (tms9918_pad12 legacy alias -> pad18).
#
# LOAD_CFG / EXTRA_ASM are read by the POM1 DevBench (probeAsmProject), so
# opening TMS_Maze3D.asm in the Bench builds with this exact cfg and flashes
# the CODETANKDEV cartridge bank from the UI.

INC      := -I ../../../lib/apple1 -I ../../../lib/tms9918
PAD      := ../../../lib/tms9918/tms9918_pad.asm
CFG      := ../bank_cfgs/apple1_maze3d_codetank_bank.cfg
LOAD_CFG := $(CFG)
EXTRA_ASM := $(PAD)

.PHONY: all clean test
all:
	ca65 $(INC) -o TMS_Maze3D.o TMS_Maze3D.asm
	ca65 $(INC) -o pad.o "$(PAD)"
	ld65 -C "$(CFG)" -m TMS_Maze3D.map TMS_Maze3D.o pad.o -o TMS_Maze3D.bin
	@echo "[Maze3D] built TMS_Maze3D.bin ($$(stat -c%s TMS_Maze3D.bin 2>/dev/null || wc -c < TMS_Maze3D.bin) B @ \$$4000)"

clean:
	rm -f TMS_Maze3D.o pad.o TMS_Maze3D.bin TMS_Maze3D.map

test:
	@echo "[Maze3D] gameplay verified via tools/verify_codetank_roms.py-style headless runs"
