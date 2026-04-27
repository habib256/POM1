# Maze generators — text mode

Two stand-alone maze generators rendered as 19×11 cells on the 40×24
Apple 1 terminal. Each shows a title screen and marks `S` (start) and
`E` (exit).

- **Maze_Sidewinder** — Sidewinder algorithm, ~717 bytes.
- **Maze2_Backtracker** — Recursive Backtracker (DFS), ~929 bytes.

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM)
- Cards: none
- Recommended POM1 preset: TODO — any text-only Apple 1 preset.

## Sources

- `Maze_Sidewinder.asm` — main entry, loads at `$0280`
- `Maze2_Backtracker.asm` — main entry, loads at `$0280` (built separately)
- libs used: `dev/lib/apple1/`

## Build

    make                          # builds both → ../../../software/games/

By hand (one program at a time):

    ca65 -I ../../lib/apple1 Maze_Sidewinder.asm
    ld65 -C ../../cc65/apple1.cfg Maze_Sidewinder.o -o ../../../software/games/Maze_Sidewinder.bin

## Run in POM1

1. POM1 → Presets → any text-mode Apple 1 preset (TODO).
2. File → Load → `software/games/Maze_Sidewinder.bin` (or `Maze2_Backtracker.bin`).
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
