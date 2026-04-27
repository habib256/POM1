# SID Piano (AZERTY)
.DEFAULT_GOAL := all
PROJECT  := Claudio_PARMIGIANI_SID_PIANO_AZERTY
LOAD_CFG := piano.cfg
OUT_DIR  := ../../../software/sid

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
	ca65 $<
	ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

.PHONY: all clean
all: $(OUT_DIR)/$(PROJECT).bin

clean:
	rm -f $(PROJECT).o
