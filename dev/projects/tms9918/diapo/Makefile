# DIAPO -- automatic TMS9918 slideshow (Graphics II) from sdcard/TMS.
# Drives the microSD MCU directly (sd.asm is .include'd — it has no .export)
# and links tms9918_pad.asm for the silicon-strict cushion. Emits the
# SD-CARD-OS-tagged binary into sdcard/TMS/ (tag "#060300" = BIN @ $0300).
#
# `#` is a Make comment even in filenames, so build the literal via H.
H    := \#
INC  := -I ../../../lib/tms9918 -I ../../../lib/sd
PAD  := ../../../lib/tms9918/tms9918_pad.asm
CFG  := ../../../cc65/apple1_tmsutil.cfg
OUT  := ../../../../sdcard/TMS

.PHONY: all clean test
all:
	ca65 $(INC) -o DIAPO.o DIAPO.asm
	ca65 $(INC) -o pad.o "$(PAD)"
	@mkdir -p "$(OUT)"
	ld65 -C "$(CFG)" DIAPO.o pad.o -o "$(OUT)/DIAPO$(H)060300"
	@echo "[DIAPO] built DIAPO$(H)060300 in $(OUT)"

clean:
	rm -f DIAPO.o pad.o

test:
	@echo "[DIAPO] no smoke test wired -- verified via manual @R DIAPO on-device"
