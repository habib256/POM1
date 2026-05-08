# TMS_SilBench — TMS9918 silicon benchmark suite (29 tests).
#
# DEFAULT BUILD = CodeTank ROM image at $4000-$7FFF (jumper down,
#                 Wozmon 4000R). The 29-test suite overflows the stock
#                 4 KB Apple-1 layout (~4.5 KB image).
# Build:
#   make           — emits CodeTank-targeted .bin/.txt under
#                    software/tms9918/ (load via Hardware → CodeTank
#                    Library or paste TMS_SilBench.txt + 4000R).
#   make stock     — attempt stock 4 KB build (FAILS today by ~1 KB
#                    overflow — reserved for a future trimmed variant).
PROJECT  := TMS_SilBench
LOAD_CFG ?= apple1_silbench_codetank.cfg
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := ../../lib/tms9918/tms9918_pad.asm \
             ../../lib/tms9918/tms9918m1.asm \
             ../../lib/tms9918/tms9918_5strigger.asm
EMIT_TXT := 1

include ../../cc65/Makefile.common

stock:
	$(MAKE) LOAD_CFG=apple1_silbench.cfg

.PHONY: stock
