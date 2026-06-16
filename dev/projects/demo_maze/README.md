# Maze generators — text mode

Two stand-alone maze generators rendered as 19×11 cells on the 40×24
Apple 1 terminal. Each shows a title screen and marks `S` (start) and
`E` (exit).

- **Maze_Sidewinder** — Sidewinder algorithm, ~717 bytes.
- **Maze2_Backtracker** — Recursive Backtracker (DFS), ~929 bytes.

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM)
- Cards: none
- Recommended POM1 preset: 1 (Apple-1 with ACI & BASIC; any text preset works).

## Sources

- `Maze_Sidewinder.asm` — main entry, loads at `$0280`
- `Maze2_Backtracker.asm` — main entry, loads at `$0280` (built separately)
- libs used: `dev/lib/apple1/`

## Build

    make                          # builds both → ../../../software/Apple-1 games/

By hand (one program at a time):

    ca65 -I ../../lib/apple1 Maze_Sidewinder.asm
    ld65 -C ../../cc65/apple1_4k.cfg Maze_Sidewinder.o -o ../../../software/Apple-1 games/Maze_Sidewinder.bin

## Run in POM1

1. POM1 → Presets → preset 1 (Apple-1 with ACI & BASIC).
2. File → Load → `software/Apple-1 games/Maze_Sidewinder.txt` (or `Maze2_Backtracker.txt`).
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
