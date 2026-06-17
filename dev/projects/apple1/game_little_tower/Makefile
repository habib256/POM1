# Little Tower v1.0
PROJECT  := LittleTower-1.0
LOAD_CFG := apple1_little_tower.cfg
OUT_DIR  := ../../../../software/Apple-1 games
LIB      := -I ../../../lib/apple1

# Boot-smoke: load headless via the Terminal Card, assert the title prints.
TEST_CMD := python3 ../../../../tools/test_game_smoke.py --pom1 ../../../../build/POM1 --bin "$(OUT_DIR)/$(PROJECT).bin" --addr 0x0280 --expect "LITTLE TOWER"

include ../../../cc65/Makefile.common
