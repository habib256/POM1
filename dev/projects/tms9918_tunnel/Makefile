# TMS_Tunnel -- concentric rotating tunnel for P-LAB TMS9918 Graphic Card.
PROJECT  := TMS_Tunnel
CFG      ?= apple1_tunnel.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/Graphic TMS9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918m1.asm ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
