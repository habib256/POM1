# TMS9918 Mandelbrot — bitmap-mode escape-time renderer.
PROJECT  := TMS_Mandel
CFG      ?= apple1_mandel.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918m2.asm ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
