# TMS9918 WozMon Demo — preuve de concept M1->M2 :
#   driver Text Mode F1 + runtime console + démo qui affiche du texte sur
#   le TMS9918 (40x24, charmap Apple-1) avec wrap colonne 40 et scroll.
#
# Pas de patch Wozmon ici (M3, livraison suivante) — la démo se charge à
# $0300, init le TMS9918, imprime, puis JMP $FF1F (WOZ_RST) pour rendre
# la main au Wozmon stock.
.DEFAULT_GOAL := all
OUT_DIR  := ../../../software/tms9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918

OBJS := TMS_Wozmon_demo.o tms9918_text.o tms9918_console.o tms9918_helpers.o tms9918_pad.o

TMS_Wozmon_demo.o: TMS_Wozmon_demo.asm
	ca65 $(LIB) -o $@ $<

tms9918_text.o: ../../lib/tms9918/tms9918_text.asm
	ca65 $(LIB) -o $@ $<

tms9918_console.o: ../../lib/tms9918/tms9918_console.asm
	ca65 $(LIB) -o $@ $<

tms9918_helpers.o: ../../lib/tms9918/tms9918_helpers.asm
	ca65 $(LIB) -o $@ $<

tms9918_pad.o: ../../lib/tms9918/tms9918_pad.asm
	ca65 $(LIB) -o $@ $<

$(OUT_DIR)/TMS_Wozmon_demo.bin: $(OBJS)
	ld65 -C apple1_tms_wozmon.cfg $^ -o $@

$(OUT_DIR)/TMS_Wozmon_demo.txt: $(OUT_DIR)/TMS_Wozmon_demo.bin
	python3 emit_TMS_Wozmon_demo_txt.py

.PHONY: all clean
all: $(OUT_DIR)/TMS_Wozmon_demo.txt

clean:
	rm -f $(OBJS) $(OUT_DIR)/TMS_Wozmon_demo.bin $(OUT_DIR)/TMS_Wozmon_demo.txt
