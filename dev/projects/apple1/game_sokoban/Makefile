# Sokoban (text mode) — default = stock 4K variant.
# Override at the command line: `make CFG=apple1_sok_8k.cfg` etc.
PROJECT  := Sokoban
CFG      ?= apple1_sok_4k.cfg
LOAD_CFG := $(CFG)
OUT_DIR  := ../../../../software/Apple-1 games
LIB      := -I ../../../lib/apple1 -I ../../../lib/games/sokoban

# Boot-smoke: load headless via the Terminal Card, assert the title prints.
TEST_CMD := python3 ../../../../tools/test_game_smoke.py --pom1 ../../../../build/POM1 --bin "$(OUT_DIR)/$(PROJECT).bin" --addr 0x0280 --expect "SOKOBAN"

include ../../../cc65/Makefile.common
