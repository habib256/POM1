# TMS9918 Nyan Fantasy — full 12-frame Mode III port (POM1 Fantasy preset only).
PROJECT  := TMS_Nyan_Fantasy
CFG      ?= apple1_nyan_fantasy.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := nyan_frames.asm ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
