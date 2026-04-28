# TMS9918 Galaga — default = stock cassette ($0280) variant.
# Override at the command line: `make CFG=apple1_galaga_codetank.cfg` etc.
PROJECT  := TMS_Galaga
CFG      ?= apple1_galaga.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/tms9918
EMIT_TXT := 1

include ../../cc65/Makefile.common
