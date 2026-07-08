# TMSLOAD / TMSLOADM -- single-image TMS9918 VRAM loader, two register variants
# from one source. Emits the SD-CARD-OS-tagged binaries straight into
# sdcard/TMS/ (name = raw code, tag "#060300" = type 06 BIN @ exec addr $0300).
#
# Paths are relative to this dir (sketchs/tms9918/tool_tmsload):
#   ../../../dev/lib = dev/lib   ../../../dev/cc65 = dev/cc65
#   ../../../sdcard = repo sdcard/
#
# `#` is a Make comment even in filenames, so build the literal via H.
H    := \#
INC  := -I ../../../dev/lib/tms9918
PAD  := ../../../dev/lib/tms9918/tms9918_pad.asm
CFG  := ../../../dev/cc65/apple1_tmsutil.cfg
OUT  := ../../../sdcard/TMS

.PHONY: all clean test
all:
	ca65 $(INC) -o TMSLOAD.o  TMSLOAD.asm
	ca65 $(INC) -D MULTICOLOR -o TMSLOADM.o TMSLOAD.asm
	ca65 $(INC) -o pad.o "$(PAD)"
	@mkdir -p "$(OUT)"
	ld65 -C "$(CFG)" TMSLOAD.o  pad.o -o "$(OUT)/TMSLOAD$(H)060300"
	ld65 -C "$(CFG)" TMSLOADM.o pad.o -o "$(OUT)/TMSLOADM$(H)060300"
	@echo "[TMSLOAD] built TMSLOAD$(H)060300 + TMSLOADM$(H)060300 in $(OUT)"

clean:
	rm -f TMSLOAD.o TMSLOADM.o pad.o

test:
	@echo "[TMSLOAD] no smoke test wired -- verified via manual @L/@R on-device"
