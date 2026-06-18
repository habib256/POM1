# GEN2Countdown — 20 -> 0 counter on Uncle Bernie's GEN2 HGR card.
#
# `make` runs emit_GEN2Countdown_txt.py — the single source of truth for the
# build: it builds dev/lib/gfx on demand, compiles via cl65 (-Oirs), and writes
# the canonical .bin + Woz-hex .txt under software/. See README for details.

PROJECT := GEN2Countdown

.PHONY: all clean
.DEFAULT_GOAL := all

all:
	python3 emit_$(PROJECT)_txt.py

clean:
	rm -f $(PROJECT).bin *.o
