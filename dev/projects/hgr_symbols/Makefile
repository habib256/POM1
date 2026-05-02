# HGR Symbols Show -- displays the 23 SCROLL-O-SPRITES "Symbols" sprites on GEN2
PROJECT  := HGR_SymbolsShow
LOAD_CFG := ../../cc65/apple1_gen2.cfg
OUT_DIR  := ../../../software/hgr
LIB      := -I ../../lib/apple1 -I ../../lib/hgr
EMIT_TXT := 1

# Regenerate sprites_symbols_hgr.inc from the upstream TMS9918 source whenever
# sprites_symbols.asm changes.
sprites_symbols_hgr.inc: ../../lib/tms9918/sprites_symbols.asm \
                        _gen_sprites_symbols_hgr.py
	python3 _gen_sprites_symbols_hgr.py

$(PROJECT).asm: sprites_symbols_hgr.inc

include ../../cc65/Makefile.common
