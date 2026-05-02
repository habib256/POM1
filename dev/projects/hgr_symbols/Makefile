# HGR Symbols Show -- displays the 23 SCROLL-O-SPRITES "Symbols" sprites on GEN2.
# Sprite data comes from the shared HGR sprite library at
# dev/lib/hgr/sprites/sprites_symbols_hgr.asm (auto-generated from the
# TMS9918 sources by tools/build_hgr_sprites.py). Both the .inc constants
# header and the .asm data table are .include'd by HGR_SymbolsShow.asm —
# no separate .o linking required, which keeps the EMIT_TXT pipeline happy.
PROJECT  := HGR_SymbolsShow
LOAD_CFG := ../../cc65/apple1_gen2.cfg
OUT_DIR  := ../../../software/hgr
LIB      := -I ../../lib/apple1 -I ../../lib/hgr -I ../../lib/hgr/sprites
EMIT_TXT := 1

# Refresh the shared HGR sprite library if its TMS source has moved.
../../lib/hgr/sprites/sprites_symbols_hgr.asm: \
        ../../lib/tms9918/sprites_symbols.asm \
        ../../../tools/build_hgr_sprites.py
	python3 ../../../tools/build_hgr_sprites.py --only symbols

include ../../cc65/Makefile.common
