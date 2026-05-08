# TMS9918 Nyan — Mode III multicolor demo, 6502 port.
PROJECT  := TMS_Nyan
CFG      ?= apple1_nyan.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := nyan_data.asm ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
