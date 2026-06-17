# TMS9918 Rogue — cartridge-only ($4000-$7FFF, 16 kB CodeTank slot).
# The 8 KB Parmigiani DRAM target was abandoned: the game outgrew the
# 3.5 kB low-bank CODE budget. Distribution path = roms/codetank/Codetank_GAMES2.rom
# via tools/build_games2_rom.py.
PROJECT  := TMS_Rogue
LOAD_CFG := apple1_rogue.cfg
OUT_DIR  := ../../../../software/Graphic TMS9918
LIB      := -I ../../../lib/apple1 -I ../../../lib/m6502 -I ../../../lib/tms9918 -I ../../../lib/games/rogue
EXTRA_ASM := ../../../lib/tms9918/tms9918m1.asm ../../../lib/tms9918/tms9918_pad.asm sprites_boss.asm

include ../../../cc65/Makefile.common
