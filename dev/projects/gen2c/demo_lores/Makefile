# GEN2Lores — LORES mode demo (40x48, 16 colours) on Uncle Bernie's GEN2 card,
# in C with the gen2c runtime.
#
# `make` runs emit_GEN2Lores_txt.py — the single source of truth for the build:
# it builds dev/lib/gfx on demand, compiles via cl65 (-Oirs), and writes the
# canonical .bin + Woz-hex .txt under software/. See README for details.

PROJECT := GEN2Lores

.PHONY: all clean
.DEFAULT_GOAL := all

all:
	python3 emit_$(PROJECT)_txt.py

clean:
	rm -f $(PROJECT).bin *.o
