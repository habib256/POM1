# TMS9918 Snake — default = stock cassette ($0280) variant.
# Override at the command line: `make CFG=apple1_snake_codetank_bank.cfg`.
PROJECT  := TMS_Snake
CFG      ?= apple1_snake.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/Graphic TMS9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
