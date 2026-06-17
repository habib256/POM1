# HGR Bestiary -- pages through 6 SCROLL-O-SPRITES categories on the GEN2 card.
#
# Sprite data comes from the shared HGR sprite library (dev/lib/gen2/sprites/,
# auto-generated from the TMS9918 sources by tools/build_hgr_sprites.py). The
# per-category .inc constants + .asm data blocks are .include'd by
# HGR_Bestiary.asm — no separate .o linking, which keeps EMIT_TXT happy.
#
#   make            -> "software/Graphic HGR/HGR_Bestiary.{bin,txt}"
#   make clean
#
# Run: load the .txt in POM1 (preset 8 / GEN2 enabled), then  E000R.
PROJECT  := HGR_Bestiary
LOAD_CFG := ../../../cc65/apple1_gen2.cfg
OUT_DIR  := ../../../../software/Graphic HGR
LIB      := -I ../../../lib/apple1 -I ../../../lib/gen2 -I ../../../lib/gen2/sprites
EMIT_TXT := 1

# Refresh any stale HGR sprite category from its TMS source (the canonical edit
# point). The generated .asm files ship committed, so a clean checkout builds
# without running these.
HGR_CATS   := creatures trollkind unliving fauna magick music
SPRITE_DIR := ../../../lib/gen2/sprites

$(SPRITE_DIR)/sprites_%_hgr.asm: ../../../lib/tms9918/sprites_%.asm \
                                 ../../../../tools/build_hgr_sprites.py
	python3 ../../../../tools/build_hgr_sprites.py --only $*

include ../../../cc65/Makefile.common
