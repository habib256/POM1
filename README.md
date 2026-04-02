<div align="center">

# 🍎 POM1 v1.0 — Apple 1 Emulator

**Experience the machine that started the personal computer revolution.**

A faithful Apple 1 emulator built with Dear ImGui & OpenGL — fast, lightweight, and cross-platform.

**Play it now in your browser** : 
[![Play Online](https://img.shields.io/badge/Play%20Online-WebAssembly-blueviolet.svg)](https://habib256.github.io/POM1/build-wasm/pom1_imgui.html)

or build it natively.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows%20%7C%20Web-lightgrey.svg)](#-quick-start)
[![C++](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](#)


![POM1 Screenshot](doc/screenshot.png)

</div>

---

## ✨ Features

🖥️ **Authentic Apple 1 Display** — 40×24 character grid, green phosphor or white mode, blinking `@` cursor, CRT scanline effect

⚙️ **Cycle-Accurate 6502 CPU** — All official opcodes, all addressing modes, adjustable clock (1 MHz / 2 MHz / Max)

🔍 **Live Memory Editor** — Interactive hex viewer with color-coded regions, search, bookmarks, and real-time editing

🗺️ **Visual Memory Map** — Color-coded 64 KB overview with region legend, PC/SP indicators, and tooltips

📂 **Program Loader** — Load binary files or Woz Monitor hex dumps with a built-in file browser

🐛 **Step Debugger** — Single-step execution, register inspection, disassembly, stack view, log console

💾 **Memory Save/Export** — Save any memory range as binary or Woz Monitor hex dump

📋 **Clipboard Paste** — Paste code directly into the Apple 1 keyboard from your clipboard

🎮 **20+ Programs Included** — Games, demos, and dev tools ready to run out of the box

---

## 🚀 Quick Start

### 🐧 Linux / 🍏 macOS

```bash
git clone https://github.com/gistarcade/POM1.git
cd POM1
./setup_imgui.sh                    # fetch Dear ImGui + install deps (one-time)
cd build && cmake .. && make
cd .. && ./run_emulator.sh          # copies ROMs & launches the emulator
```

### 🪟 Windows

**Prerequisites:** [Visual Studio](https://visualstudio.microsoft.com/) (C++ workload), [CMake](https://cmake.org/download/), [Git](https://git-scm.com/download/win), [vcpkg](https://vcpkg.io/)

```batch
git clone https://github.com/gistarcade/POM1.git
cd POM1
setup_imgui.bat                     REM fetch Dear ImGui + install GLFW via vcpkg
cd build
cmake --build . --config Release
cd ..
run_emulator.bat                    REM copies ROMs & launches the emulator
```

### 🌐 Web Version (WebAssembly)

**Play directly in your browser:** [https://habib256.github.io/POM1/](https://habib256.github.io/POM1/html/pom1_imgui.html)

To build the WASM version yourself:

```bash
# Install Emscripten (one-time)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh && cd ..

# Build
cd POM1
mkdir -p build-wasm && cd build-wasm
emcmake cmake ..
emmake make -j$(nproc)

# Test locally
emrun pom1_imgui.html
```

### 📦 Manual dependency install

<details>
<summary>Ubuntu / Debian</summary>

```bash
sudo apt install cmake libglfw3-dev libgl1-mesa-dev pkg-config
```
</details>

<details>
<summary>Fedora</summary>

```bash
sudo dnf install cmake glfw-devel mesa-libGL-devel pkgconf
```
</details>

<details>
<summary>Arch</summary>

```bash
sudo pacman -S cmake glfw mesa pkgconf
```
</details>

<details>
<summary>macOS</summary>

```bash
brew install cmake glfw pkg-config
```
</details>

<details>
<summary>Windows (vcpkg)</summary>

```batch
vcpkg install glfw3:x64-windows
```
</details>

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `F1` | Toggle Memory Viewer |
| `F2` | Toggle Memory Map |
| `F3` | Toggle Debug Console |
| `F5` | Soft Reset |
| `Ctrl+F5` | Hard Reset |
| `F6` | Start / Stop CPU |
| `F7` | Step (single instruction) |
| `Ctrl+O` | Load program |
| `Ctrl+S` | Save memory |
| `Ctrl+V` | Paste code |

---

## 🎮 Software Library

The `software/` directory ships with **20+ ready-to-run programs** — load them via **File > Load Memory**.

### 🕹️ Games

| Program | Description |
|---------|-------------|
| ♟️ **Microchess** | Peter Jennings' chess engine — the first commercial microcomputer game |
| 🏰 **LittleTower** | Text adventure — explore a tower, defeat a vampire |
| 🌙 **Lunar Lander** | Pilot your lander safely to the surface |
| 🚀 **Star Trek** | Mini Star Trek strategy game |
| 🔢 **2048** | Sliding tile puzzle |
| 🃏 **Blackjack** | Classic card game |
| 🔐 **Codebreaker** | Code-breaking logic game |
| 📝 **Worple** | Word guessing game |
| 🧩 **15-Puzzle** | Sliding number puzzle |
| 🔵 **Peg Solitaire** | Board peg-jumping game |
| 🎲 **Shut the Box** | Dice and tile game |

### 🎨 Demos

| Program | Description |
|---------|-------------|
| 🧬 **Game of Life** | Conway's cellular automaton |
| 🌀 **Maze** | Sidewinder maze generator with title screen |
| 🌀 **Maze 2** | Recursive Backtracker (DFS) maze generator |
| 🌌 **Mandelbrot** | Mandelbrot fractal renderer |
| 📊 **Cellular** | 1D cellular automaton |
| 🎂 **30th** | Apple 1 30th anniversary demo |

### 🛠️ Dev Tools

| Program | Description |
|---------|-------------|
| 👁️ **Woz Monitor** | Steve Wozniak's original system monitor |
| 💻 **Enhanced BASIC** | Extended BASIC with extra commands |
| 📘 **fig-FORTH** | FORTH language interpreter |

---

## 🔧 Assembling Your Own Programs

POM1 includes a linker config for [cc65](https://cc65.github.io/):

```bash
ca65 -o build/program.o source.asm
ld65 -C build/apple1.cfg -o build/program.bin build/program.o
```

Load the binary via **File > Load Memory**, or type the start address + `R` in the Woz Monitor (e.g. `300R`).

---

## 🗂️ Project Layout

```
POM1/
├── M6502.cpp/h              # 🧠 MOS 6502 CPU — all opcodes, cycle counting
├── Memory.cpp/h             # 💾 64 KB address space, ROM loader, PIA I/O
├── main_imgui.cpp           # 🪟 GLFW/OpenGL bootstrap
├── MainWindow_ImGui.cpp/h   # 🎛️ App window, menus, CPU speed control
├── Screen_ImGui.cpp/h       # 🖥️ Apple 1 display (40×24, CRT effects)
├── MemoryViewer_ImGui.cpp/h # 🔍 Hex editor with search & navigation
├── roms/                    # 📀 WozMonitor, BASIC, Krusader, charmap
├── software/                # 📂 Hex dump programs + assembly sources
│   ├── games/               #   🎮 Games
│   ├── demos/               #   🎨 Demos
│   ├── dev/                 #   🛠️ Dev tools
│   └── tests/               #   🧪 Hardware test programs
├── build/apple1.cfg         # ⚙️ cc65 linker config
├── setup_imgui.sh           # 📦 One-shot setup script
└── run_emulator.sh          # 🚀 Build check + ROM copy + launch
```

---

## 📀 ROMs

| ROM | Size | Address | Origin |
|-----|------|---------|--------|
| 👁️ **Woz Monitor** | 256 B | `$FF00` | Steve Wozniak's original system monitor |
| 💻 **Apple BASIC** | 4 KB | `$E000` | Integer BASIC interpreter |
| 🔧 **Krusader 1.3** | 8 KB | `$A000` | Ken Wessen's symbolic assembler |
| 🔤 **Charmap** | 1 KB | — | Character generator table |

All three main ROMs are loaded automatically at startup.

---

## 🗺️ Memory Map

```
$0000-$00FF   Zero Page
$0100-$01FF   Stack
$0200-$9FFF   User RAM (programs load at $0280 or $0300)
$A000-$BFFF   Krusader ROM (8 KB)
$D010-$D012   I/O — Keyboard (KBD) & Display (DSP)
$E000-$EFFF   Apple BASIC ROM (4 KB)
$FF00-$FFFF   Woz Monitor ROM (256 B)
```

---

## 👏 Credits

- **Arnaud Verhille** — Original POM1 (Java, 2000) & Dear ImGui port (2026)
- **Ken Wessen** — Upgrades, 65C02 support (2006)
- **Joe Crobak** — macOS Cocoa port
- **John D. Corrado** — C/SDL port (2006–2014)
- **Lee Davison** — Enhanced BASIC
- **Achim Breidenbach** — Sim6502
- **Fabrice Frances** — Java Microtan Emulator
- **Steve Wozniak & Steve Jobs** — For creating the Apple 1 🍎

## 🔗 Resources

- [Apple 1 Software Collection](https://apple1software.com/)
- [AppleFritter Apple 1 Forum](https://applefritter.com/apple1/)
- [POM1 Project Page](https://www.gistlabs.net/Apple1project/)

---

## 📄 License

GPL-3.0 — see [LICENSE](LICENSE)

<div align="center">

*Made with ❤️ for the Apple 1 community*

</div>
