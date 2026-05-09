# TMS9918 Split — palette split mid-frame via 5th-sprite-overflow trigger.
PROJECT  := TMS_Split
CFG      ?= apple1_split.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/Graphic TMS9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918m1.asm \
             ../../lib/tms9918/tms9918_5strigger.asm \
             ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common
