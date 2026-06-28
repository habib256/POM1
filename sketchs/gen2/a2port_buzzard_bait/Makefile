# Buzzard Bait — portage Apple-1 + GEN2 HGR
# 'make' verifie que le source se ré-assemble a l'identique (round-trip).

AS  = ca65
LD  = ld65
DA  = da65
CPU = 6502

all: verify verify-port

buzzard_bait.rebuilt.bin: buzzard_bait.s buzzard_bait.cfg
	$(AS) --cpu $(CPU) buzzard_bait.s -o buzzard_bait.o
	$(LD) -C buzzard_bait.cfg buzzard_bait.o -o $@

verify: buzzard_bait.rebuilt.bin
	@cmp buzzard_bait.rebuilt.bin buzzard_bait.bin \
	  && echo "ROUND-TRIP OK : buzzard_bait.s se re-assemble a l'identique." \
	  || (echo "ECHEC round-trip" ; exit 1)

gen2_port.bin: gen2_port.s gen2_port.cfg
	$(AS) --cpu $(CPU) gen2_port.s -o gen2_port.o
	$(LD) -C gen2_port.cfg gen2_port.o -o $@

verify-port: gen2_port.bin
	@echo "gen2_port.s assemble (shims $$FB00/$$FB10/$$FB80/$$FCA8)."

disasm:
	$(DA) --info buzzard_bait.info

clean:
	rm -f *.o buzzard_bait.rebuilt.bin gen2_port.bin buzzard_bait.regen.s

.PHONY: all verify verify-port disasm clean
