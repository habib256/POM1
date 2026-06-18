# GEN2DBuf — double-buffering (PAGE2) demo for Uncle Bernie's GEN2 card, in C
# with the gen2c runtime.
#
# `make` runs emit_GEN2DBuf_txt.py — the single source of truth for the build:
# it builds dev/lib/gfx on demand, compiles via cl65 (-Oirs), and writes the
# canonical .bin + Woz-hex .txt under "software/Graphic HGR/". See README.

PROJECT := GEN2DBuf

.PHONY: all clean
.DEFAULT_GOAL := all

all:
	python3 emit_$(PROJECT)_txt.py

clean:
	rm -f $(PROJECT).bin *.o
