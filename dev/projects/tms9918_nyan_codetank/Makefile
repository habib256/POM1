# TMS9918 Nyan CodeTank — RLE-compressed 12-frame Mode III demo.
# Output: software/tms9918/TMS_Nyan_CodeTank.bin (16 KB ROM image)
#         roms/codetank/Codetank_Nyan.rom (32 KB, lower bank = Nyan,
#                                            upper bank = $FF padding)
PROJECT  := TMS_Nyan_CodeTank
CFG      ?= apple1_nyan_codetank.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../software/Graphic TMS9918
LIB      := -I ../../lib/apple1 -I ../../lib/tms9918
EXTRA_ASM := nyan_rle.asm ../../lib/tms9918/tms9918_pad.asm
EMIT_TXT := 0

include ../../cc65/Makefile.common

# Add a post-link step that packs the .bin into a 32 KB CodeTank ROM.
# Lower 16 KB = Nyan; upper 16 KB = $FF (EEPROM erased state). The
# operator picks "Lower" jumper to boot Nyan; "Upper" yields a blank
# screen (intentional — flips back when the CodeTank library reloads).
ROM_OUT := ../../../roms/codetank/Codetank_Nyan.rom

all: $(ROM_OUT)

$(ROM_OUT): $(OUT_DIR_T)/$(PROJECT).bin
	python3 build_rom.py "$(OUT_DIR)/$(PROJECT).bin" "$@"
