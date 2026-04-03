# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

POM1 v1.1 is an Apple 1 emulator built with Dear ImGui. It emulates the MOS 6502 CPU and Apple 1 hardware including memory-mapped I/O, display, keyboard input, and the Apple Cassette Interface (ACI) with live audio and tape files. The UI is fully in English. Builds on Linux, macOS, Windows, and Web (Emscripten/WASM).

## Build & Run Commands

### Setup (first time only)
```bash
./setup_imgui.sh          # Linux/macOS — downloads ImGui and installs dependencies
setup_imgui.bat           # Windows — uses vcpkg for GLFW
```

### Build
```bash
cd build
cmake ..
make                      # Linux/macOS
cmake --build . --config Release  # Windows (MSVC)
```

### Run
```bash
./run_emulator.sh         # Linux/macOS — handles ROM copying and build checks
run_emulator.bat          # Windows
# OR
cd build
./pom1_imgui              # Linux/macOS
build\Release\pom1_imgui.exe  # Windows
```

Note: ROMs must be present next to the executable. The run scripts automatically copy them from `roms/` if needed.

### Build WASM (requires Emscripten)
```bash
source /path/to/emsdk/emsdk_env.sh
mkdir -p build-wasm && cd build-wasm
emcmake cmake ..
emmake make -j$(nproc)
emrun pom1_imgui.html         # Test locally
```

### Assembling programs (requires cc65)
```bash
ca65 -o build/program.o software/program.asm
ld65 -C software/apple1.cfg -o build/program.bin build/program.o
```

## Architecture Overview

### Core Emulation Layer
- **M6502.cpp/h**: Complete MOS 6502 CPU emulation with all opcodes, addressing modes, and cycle counting. Key types: `op` is `quint16` for proper 16-bit address handling in all addressing modes. `tmp` is `int` for carry/borrow detection via bit 8.
- **Memory.cpp/h**: 64KB address space with ROM loading (with bounds checking), memory-mapped I/O (keyboard 0xD010/0xD011, display 0xD012), binary and hex dump file loading, save to file, and configurable terminal speed.

### UI Layer (ImGui-based)
- **main_imgui.cpp**: GLFW/OpenGL3 initialization (GL 3.2 Core, GLSL 150) and main render loop with GLFW keyboard callbacks chained to ImGui. `GLFW_OPENGL_FORWARD_COMPAT` is macOS-only (`#ifdef __APPLE__`).
- **MainWindow_ImGui.cpp/h**: Main application window with menu bar, toolbar with icon buttons, status bar, window management, CPU speed control (1/2/Max MHz), file loading/saving dialogs, clipboard paste, memory map window, and keyboard input handling.
- **Screen_ImGui.cpp/h**: Apple 1 display emulation (40x24 character grid) with green/white monitor modes, `@` blinking cursor (timer uses `fmod` to avoid float overflow), scanline CRT effect, configurable text scale, and bitmap glyph rendering sourced from `roms/charmap.rom` when available.
- **MemoryViewer_ImGui.cpp/h**: Interactive hex editor with color-coded regions (matching Memory Map colors), search (hex and ASCII), bookmarks, navigation shortcuts, and real-time editing.

### ROM Files (roms/)
- **WozMonitor.rom** (256B @ 0xFF00): Wozniak's system monitor
- **basic.rom** (4KB @ 0xE000): Apple BASIC interpreter
- **krusader-1.3.rom** (8KB @ 0xA000): Symbolic assembler
- **charmap.rom** (1KB): Character map for display

The main firmware ROMs are loaded automatically at startup by `Memory::loadWozMonitor()`, `loadBasic()`, `loadKrusader()`, and `loadAciRom()`. The terminal renderer also uses `charmap.rom` when available.

### Software directory (software/)
Contains Apple 1 programs in Woz Monitor hex dump format (.txt) organized in subdirectories:
- **games/**: Games — Microchess, LittleTower, Lunar Lander, Blackjack, 2048, Mini Star Trek, etc.
- **demos/**: Demos — Game of Life, Maze (Sidewinder), Maze 2 (Recursive Backtracker), Mandelbrot, Cellular automaton, etc.
- **dev/**: Development tools — Woz Monitor, Enhanced BASIC, fig-FORTH
- **tests/**: Hardware test programs — hex I/O, keyboard, terminal tests

Programs can be loaded via File > Load Memory, which provides a file browser with folder navigation. Assembly source files (`.asm`) can be assembled with cc65.

## Key Implementation Details

### Memory-Mapped I/O
- **0xD010 (KBD)**: Keyboard data register. Returns last key with bit 7 set. Reading clears the keyboard strobe (keyReady flag), matching PIA 6821 behavior.
- **0xD011 (KBDCR)**: Keyboard control register. Bit 7 = 1 when key is ready.
- **0xD012 (DSP)**: Display output register. Writing a character triggers the display callback. Reading returns bit 7 = 0 (display ready) after the terminal speed delay has elapsed, or bit 7 = 1 (busy) during the delay. The busy counter is clamped to 0 (never goes negative).

**Note**: Keyboard input is automatically converted to uppercase (Apple 1 convention).

### CPU Execution Model
The CPU has three execution modes managed by MainWindow_ImGui:
1. **Stopped**: No execution
2. **Running**: Executes `executionSpeed` cycles per frame (1 MHz = 16667, 2 MHz = 33333, Max = 1000000)
3. **Step Mode**: Execute one instruction at a time via `stepCpu()`

Access CPU state via getter methods for debugging: `getAccumulator()`, `getProgramCounter()`, etc.

### Display Rendering
Characters are rendered from the Apple-1 bitmap character generator ROM (`charmap.rom`) using a 5x7 pixel matrix within widened display cells. The window size at startup is still calculated from the cell dimensions used by `Screen_ImGui::render()`.

### Addressing Mode Implementation
The M6502 addressing mode functions (Abs, AbsX, AbsY, Ind, IndZeroX, IndZeroY, etc.) store the resolved address in `op` (quint16). The Imm() function stores `programCounter` in `op` (so memRead(op) fetches the immediate value). All instructions use `memory->memRead(op)` or `memory->memWrite(op, value)` uniformly.

### Loading Programs
Memory supports loading and saving:
- `loadBinary(filename, startAddress)`: Raw binary loaded at specified address
- `loadHexDump(filename, startAddress)`: Parses Woz Monitor hex format. Supports comment lines (`//`, `#`, `;`), continuation lines, `T` prefix (turbo), `X` marker, and `R` suffix (run address). Handles single-line files where data bytes merge with addresses (e.g., `ED0300:` is split into data `ED` + address `0300`).
- Save dialog: exports memory range as binary or Woz Monitor hex dump format.
- Clipboard paste: feeds characters to Apple 1 keyboard (limited to 4096 chars).

### Memory Map Window
Visual 16x16 grid (256 pages = 64KB) with color-coded regions, KB labels, PC/SP indicators, hover tooltips (guarded by `IsWindowHovered`), legend, I/O register details, and CPU vector addresses read in real-time.

### Keyboard Shortcuts
| Key | Action |
|-----|--------|
| F1 | Toggle Memory Viewer |
| F2 | Toggle Memory Map |
| F3 | Toggle Debug Console |
| F5 | Soft Reset |
| Ctrl+F5 | Hard Reset |
| F6 | Start/Stop CPU |
| F7 | Step |
| Ctrl+O | Load Memory |
| Ctrl+S | Save Memory |
| Ctrl+V | Paste Code |
| Ctrl+Q | Quit |

### Additional Directories
- **bios/**: Legacy BIOS/ROM files from the original POM1 project.
- **fonts/**: Font Awesome icon font (fa-solid-900.ttf) used by the UI.
- **doc/**: Documentation — Krusader 1.3 manual PDF, Apple 1 software reference, screenshot.
- **IconsFontAwesome6.h**: Font Awesome 6 icon codepoints header.

## Memory Map

```
0x0000-0x00FF  Zero page
0x0100-0x01FF  Stack
0x0200-0x9FFF  User RAM (programs typically load at 0x0280 or 0x0300)
0xA000-0xBFFF  Krusader ROM (8 KB)
0xD010         KBD - Keyboard data register
0xD011         KBDCR - Keyboard control register
0xD012         DSP - Display output register
0xE000-0xEFFF  Apple BASIC ROM (4 KB)
0xFF00-0xFFFF  WOZ Monitor ROM (256 B)
  0xFFFA/B     NMI vector
  0xFFFC/D     Reset vector → 0xFF00
  0xFFFE/F     IRQ vector → 0xFF00
```

## Platform-Specific Notes

### CMake Build System
CMakeLists.txt uses a fallback strategy for GLFW discovery:
1. `find_package(glfw3 CONFIG)` — works with vcpkg (Windows), Homebrew (macOS)
2. Falls back to `pkg_check_modules` — works with apt/dnf/pacman (Linux)

### Windows
- Requires Visual Studio (C++ workload), CMake, Git, and vcpkg
- `setup_imgui.bat` installs GLFW via vcpkg and configures CMake
- `run_emulator.bat` handles MSVC output directories (Release/Debug)
- MSVC flags: `/utf-8` and `_CRT_SECURE_NO_WARNINGS`

### macOS
Adds framework links (Cocoa, IOKit, CoreVideo). `GLFW_OPENGL_FORWARD_COMPAT` set only on macOS.

### Linux
The setup script supports apt (Ubuntu/Debian), dnf (Fedora/CentOS), and pacman (Arch).

## Repository Notes

The `build/`, `build-wasm/`, and `imgui/` directories are excluded from git via `.gitignore`.

## Version History

### v1.1 (April 2026)
- Apple Cassette Interface: ACI ROM at `$C100`, I/O at `$C000`/`$C081`, live audio (hardware-faithful or stabilized), load/save `.aci` and `.wav`, Cassette Control window
- Terminal: bitmap rendering from `charmap.rom`, CRT scanlines/glow, green / brown / monochrome, optional host-ASCII mode; viewport aspect ~280:192; charmap contrast tuning
- Emulation on a worker thread with UI snapshots; CPU speed (1 MHz / 2 MHz / Max) syncs immediately from toolbar and Settings
- Memory Map: two-column layout (map + I/O under map, legend + ACI ROM / CPU vectors)
- Defaults: brown phosphor, cassette live audio hardware-faithful

### v1.0 (April 2026)
- Dear ImGui UI with green/white CRT monitor, scanline effect
- Complete MOS 6502 CPU emulation (BRK opcode fixed to use implied addressing)
- Memory viewer with aligned hex columns, memory map, step debugger
- File browser for loading/saving programs (binary and Woz Monitor hex dump)
- About dialog with credits, resources, and toolbar info button
- ROM write-protection toggle in Settings (synced with actual memory state)
- 20+ programs included (games, demos, dev tools)
- Maze (Sidewinder algorithm) and Maze 2 (Recursive Backtracker) with title screens, S/E markers
- Builds on Linux, macOS, Windows, and Web (Emscripten/WASM via Emscripten 5.x)
- Dead code cleanup: removed legacy SDL stubs (saveState/loadState, setSpeed, synchronize, keyStickyCounter)
- Renamed software directory from `soft-asm/` to `software/`

## Known Issues & TODOs

1. **No native file dialog**: File loading/saving uses built-in file browsers instead of system file pickers.
