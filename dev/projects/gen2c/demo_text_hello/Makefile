# GEN2TextHello — small demo of the 40x24 TEXT mode on Uncle Bernie's GEN2
# colour card, written in C with the gen2c runtime.
#
# `make` runs emit_GEN2TextHello_txt.py — the single source of truth for the
# build: it builds dev/lib/gfx on demand, compiles via cl65 (-Oirs), and writes
# the canonical .bin + Woz-hex .txt under software/. See README for details.

PROJECT := GEN2TextHello

.PHONY: all clean
.DEFAULT_GOAL := all

all:
	python3 emit_$(PROJECT)_txt.py

clean:
	rm -f $(PROJECT).bin *.o
