# TMS9918 Sokoban — default = standalone CodeTank variant ($4000).
# Override at the command line: `make CFG=apple1_sokoban_codetank_bank.cfg`.
PROJECT  := TMS_Sokoban
CFG      ?= apple1_sokoban_codetank.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/sokoban -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918_pad.asm

include ../../cc65/Makefile.common
