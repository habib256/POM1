# dev/codetank -- P-LAB CodeTank ROM construction. THE CI gate for the
# cartridge line-up: `make -C dev/codetank` rebuilds the four release
# cartridges + the CODETANKDEV flash cart via tools/build_codetank_rom.py
# (which assembles every bank program from its sketchs/ source with the
# bank cfgs under bank_cfgs/), then asserts the committed release images
# did not drift.
#
# LAYOUT RULE (juillet 2026): this directory holds ONLY cartridge
# construction material -- the menu/loader sources (game1_menu,
# demos_menu, game6_menu) and the per-bank linker cfgs (bank_cfgs/).
# Program sources NEVER live here: games and tools belong under
# sketchs/<card>/ (e.g. sketchs/tms9918/game_maze3d), where the DevBench
# and the per-sketch Makefiles pick them up. build_codetank_rom.py pulls
# them from there when it composes a cartridge.
#
# Burn gate before any real EPROM: python3 tools/verify_codetank_roms.py

ROOT := ../..

.PHONY: all clean test
all:
	python3 $(ROOT)/tools/build_codetank_rom.py
	git -C $(ROOT) diff --exit-code --stat roms/codetank \
	  || { echo "[codetank] committed release ROMs drifted -- inspect git diff roms/codetank"; exit 1; }
	@echo "[codetank] release cartridges rebuilt byte-identical"

clean:
	@echo "[codetank] nothing to clean (ROMs land in roms/codetank/)"

test: all
