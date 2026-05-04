# TMS9918 Rogue — default = stock cassette ($0280) variant.
# Override at the command line: `make CFG=apple1_rogue_codetank.cfg`.
PROJECT  := TMS_Rogue
CFG      ?= apple1_rogue.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918m1.asm ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
