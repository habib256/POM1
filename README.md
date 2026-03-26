# POM1 — Apple 1 Emulator

A faithful **Apple 1** emulator built with **Dear ImGui** and GLFW/OpenGL.
Full MOS 6502 CPU emulation, 64 KB memory, memory-mapped I/O, and the original ROM set — all in a lightweight, GPU-accelerated package.

Ported from the original Java POM1 project to modern C++ for speed, simplicity, and zero framework bloat.

## What You Get

- **Authentic Apple 1 display** — 40x24 character grid, green phosphor or white mode, blinking `@` cursor, CRT scanline effect
- **Cycle-accurate 6502 CPU** — all official opcodes, all addressing modes, adjustable clock speed (1 MHz / 2 MHz / Max)
- **Live memory editor** — interactive hex viewer with search, quick-jump shortcuts, real-time editing
- **Three ROMs loaded at boot** — Woz Monitor (`$FF00`), Apple BASIC (`$E000`), Krusader assembler (`$A000`)
- **Program loader** — binary files or Woz Monitor hex dumps, with a built-in file browser and folder navigation for the `soft-asm/` library
- **Step debugger** — single-step execution, soft/hard reset, full register inspection

## Quick Start

```bash
./setup_imgui.sh          # fetch Dear ImGui + install deps (one-time)
cd build && cmake .. && make
./run_emulator.sh         # copies ROMs, launches the emulator
```

### Manual dependency install

```bash
# Ubuntu / Debian
sudo apt install cmake libglfw3-dev libgl1-mesa-dev pkg-config

# Fedora
sudo dnf install cmake glfw-devel mesa-libGL-devel pkgconf

# Arch
sudo pacman -S cmake glfw mesa pkgconf

# macOS
brew install cmake glfw pkg-config
```

## Software Library

The `soft-asm/` directory ships with ready-to-run Apple 1 programs organized by category (loadable via **File > Load Memory** with folder navigation):

| Program | Description |
|---------|-------------|
| **Microchess** | Peter Jennings' chess engine — the first commercial microcomputer game |
| **Lunar Lander** | Pilot your lander to the surface |
| **Game of Life** | Conway's cellular automaton |
| **LittleTower** | Text adventure — explore a tower, defeat a vampire |
| **fig-FORTH** | FORTH language interpreter |
| **Enhanced BASIC** | Extended BASIC with extra commands |
| **Mastermind** | Code-breaking logic game |
| **Star Trek** | Mini Star Trek strategy game |

## Assembling Your Own Programs

POM1 includes a linker config for [cc65](https://cc65.github.io/):

```bash
ca65 -o build/program.o source.in
ld65 -C build/apple1.cfg -o build/program.bin build/program.o
```

Load the resulting binary via **File > Load Memory** and type the start address + `R` in the Woz Monitor (e.g. `300R`).

## Project Layout

```
POM1/
├── M6502.cpp/h              # MOS 6502 CPU — all opcodes, cycle counting
├── Memory.cpp/h             # 64 KB address space, ROM loader, PIA I/O
├── main_imgui.cpp           # GLFW/OpenGL bootstrap
├── MainWindow_ImGui.cpp/h   # App window, menus, CPU speed control
├── Screen_ImGui.cpp/h       # Apple 1 display (40x24, CRT effects)
├── MemoryViewer_ImGui.cpp/h # Hex editor with search & navigation
├── roms/                    # WozMonitor, BASIC, Krusader, charmap
├── soft-asm/                # Hex dump programs + assembly sources
│   ├── games/               #   Games (Microchess, LittleTower, 2048…)
│   ├── demos/               #   Demos (Life, Maze, Mandelbrot…)
│   ├── dev/                 #   Dev tools (Woz Monitor, BASIC, FORTH)
│   └── tests/               #   Hardware test programs
├── build/apple1.cfg         # cc65 linker config
├── setup_imgui.sh           # One-shot setup script
└── run_emulator.sh          # Build check + ROM copy + launch
```

## ROMs

| ROM | Size | Address | Origin |
|-----|------|---------|--------|
| **Woz Monitor** | 256 B | `$FF00` | Steve Wozniak's original system monitor |
| **Apple BASIC** | 4 KB | `$E000` | Integer BASIC interpreter |
| **Krusader 1.3** | 8 KB | `$A000` | Ken Wessen's symbolic assembler |
| **Charmap** | 1 KB | — | Character generator table |

All three main ROMs are loaded automatically at startup.

## License

GPL-3.0 — see [LICENSE](LICENSE)
