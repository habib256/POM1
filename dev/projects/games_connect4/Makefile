# Connect 4 (text mode)
PROJECT  := Connect4
LOAD_CFG := ../../cc65/apple1_4k.cfg
OUT_DIR  := ../../../software/Apple-1 games
LIB      := -I ../../lib/apple1

# Boot-smoke: load headless via the Terminal Card, assert the title prints.
TEST_CMD := python3 ../../../tools/test_game_smoke.py --pom1 ../../../build/POM1 --bin "$(OUT_DIR)/$(PROJECT).bin" --addr 0x0280 --expect "CONNECT 4"

include ../../cc65/Makefile.common
