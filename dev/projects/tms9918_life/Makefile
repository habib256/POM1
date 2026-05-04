# TMS9918 Life — default = stock cassette variant.
# Override at the command line: `make CFG=apple1_life_codetank_bank.cfg`.
PROJECT  := TMS_Life
CFG      ?= ../../cc65/apple1.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
