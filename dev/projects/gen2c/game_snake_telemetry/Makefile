# GEN2Snake — Snake on Uncle Bernie's GEN2 HGR card, with a telemetry tap.
#
# `make` runs emit_GEN2Snake_txt.py — the single source of truth for the build:
# it builds dev/lib/gfx on demand, compiles via cl65 (-Oirs), and writes the
# canonical .bin + Woz-hex .txt under software/Telemetry/ (not Graphic HGR/).
# See README for details.

PROJECT := GEN2Snake

.PHONY: all clean
.DEFAULT_GOAL := all

all:
	python3 emit_$(PROJECT)_txt.py

clean:
	rm -f $(PROJECT).bin *.o
