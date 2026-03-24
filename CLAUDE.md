# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

POM1 is an Apple 1 emulator ported from Qt to Dear ImGui. It emulates the MOS 6502 CPU and Apple 1 hardware including memory-mapped I/O, display, and keyboard input.

## Build & Run Commands

### Setup (first time only)
```bash
./setup_imgui.sh  # Downloads ImGui and installs dependencies
```

### Build
```bash
cd build
cmake ..
make
```

### Run
```bash
./run_emulator.sh  # Recommended - handles ROM copying and build checks
# OR
cd build
./pom1_imgui
```

Note: ROMs must be present in the `build/` directory. The `run_emulator.sh` script automatically copies them from `roms/` if needed.

### Assembling programs (requires cc65)
```bash
ca65 -o build/program.o source.in
ld65 -C build/apple1.cfg -o build/program.bin build/program.o
```

## Architecture Overview

### Core Emulation Layer
- **M6502.cpp/h**: Complete MOS 6502 CPU emulation with all opcodes, addressing modes, and cycle counting. Key types: `op` is `quint16` for proper 16-bit address handling in all addressing modes. `tmp` is `int` for carry/borrow detection via bit 8.
- **Memory.cpp/h**: 64KB address space with ROM loading, memory-mapped I/O (keyboard 0xD010/0xD011, display 0xD012), binary and hex dump file loading, and configurable terminal speed.

### UI Layer (ImGui-based)
- **main_imgui.cpp**: GLFW/OpenGL3 initialization (GL 3.2 Core, GLSL 150) and main render loop with GLFW keyboard callbacks chained to ImGui.
- **MainWindow_ImGui.cpp/h**: Main application window with menu bar, status bar, window management, CPU speed control (1/2/Max MHz), file loading dialog with hex dump support, and keyboard input handling.
- **Screen_ImGui.cpp/h**: Apple 1 display emulation (40x24 character grid) with green/white monitor modes, `@` blinking cursor, scanline CRT effect, and configurable text scale.
- **MemoryViewer_ImGui.cpp/h**: Interactive hex editor with search, navigation shortcuts, and real-time editing.

### ROM Files (roms/)
- **WozMonitor.rom** (256B @ 0xFF00): Wozniak's system monitor
- **basic.rom** (4KB @ 0xE000): Apple BASIC interpreter
- **krusader-1.3.rom** (8KB @ 0xA000): Symbolic assembler
- **charmap.rom** (1KB): Character map for display

All three main ROMs are loaded automatically at startup by Memory::loadWozMonitor(), loadBasic(), and loadKrusader().

### Software directory (soft-asm/)
Contains Apple 1 programs in Woz Monitor hex dump format (.txt). These can be loaded via File > Load Memory and selecting "Hex dump (.txt)". Programs include Microchess, Lunar Lander, Game of Life, LittleTower (adventure game), etc.

The file `LittleTower.in` is ca65 assembly source that can be assembled with cc65.

## Key Implementation Details

### Memory-Mapped I/O
- **0xD010 (KBD)**: Keyboard data register. Returns last key with bit 7 set. Reading clears the keyboard strobe (keyReady flag), matching PIA 6821 behavior.
- **0xD011 (KBDCR)**: Keyboard control register. Bit 7 = 1 when key is ready.
- **0xD012 (DSP)**: Display output register. Writing a character triggers the display callback. Reading returns bit 7 = 0 (display ready) after the terminal speed delay has elapsed, or bit 7 = 1 (busy) during the delay. The delay simulates real Apple 1 terminal speed (configurable, default 60 chars/sec = 16667 CPU cycles per character at 1 MHz).

**Note**: Keyboard input is automatically converted to uppercase (Apple 1 convention).

### CPU Execution Model
The CPU has three execution modes managed by MainWindow_ImGui:
1. **Stopped**: No execution
2. **Running**: Executes `executionSpeed` cycles per frame (1 MHz = 16667, 2 MHz = 33333, Max = 1000000)
3. **Step Mode**: Execute one instruction at a time via `stepCpu()`

Access CPU state via getter methods for debugging: `getAccumulator()`, `getProgramCounter()`, etc.

### Display Callback Pattern
Memory and M6502 both have display callbacks set to Screen_ImGui::displayCallback (static). When a character is written to 0xD012, Memory::memWrite() masks bit 7, sets the display busy counter, and invokes the callback which calls Screen_ImGui::writeChar().

### Addressing Mode Implementation
The M6502 addressing mode functions (Abs, AbsX, AbsY, Ind, IndZeroX, IndZeroY, etc.) store the resolved address in `op` (quint16). The Imm() function stores `programCounter` in `op` (so memRead(op) fetches the immediate value). All instructions use `memory->memRead(op)` or `memory->memWrite(op, value)` uniformly.

### Loading Programs
Memory supports two loading methods:
- `loadBinary(filename, startAddress)`: Raw binary loaded at specified address
- `loadHexDump(filename, startAddress)`: Parses Woz Monitor hex format (lines like `0300: A2 58 BD ...`), supports comment lines starting with `//`, `#`, or `;`, and continuation lines (`:` without address prefix).

The UI dialog (File > Load Memory) scans the `soft-asm/` directory and lists available programs.

### Additional Directories
- **bios/**: Legacy BIOS/ROM files from the original POM1 project (6502.rom.bin, 65C02.rom.bin, apple1.rom, apple1.vid).
- **fonts/**: Font Awesome icon font (fa-solid-900.ttf) used by the UI.
- **doc/**: Documentation (Krusader 1.3 manual PDF).
- **images/**: UI icons for menus and dialogs (PNG files).
- **IconsFontAwesome6.h**: Font Awesome 6 icon codepoints header.

### Legacy Files (Qt version - not used)
The following files are from the original Qt version and are NOT compiled in the ImGui build:
- MainWindow.cpp/h, MemoryViewer.cpp/h, Screen.cpp/h, main.cpp
- qtpom1.pro, qtpom1.pro.user, qtpom1.qrc

## Platform-Specific Notes

### macOS
CMakeLists.txt uses pkg-config to find GLFW and adds macOS-specific framework links (Cocoa, IOKit, CoreVideo).

### Linux
The setup script supports apt (Ubuntu/Debian), dnf (Fedora/CentOS), and pacman (Arch). CMakeLists.txt uses pkg-config to find GLFW dynamically.

## Repository Notes

The `build/` and `imgui/` directories are currently tracked in git but should ideally be ignored. The `.gitignore` covers them for future additions. To fully untrack them, run:
```bash
git rm -r --cached build/ imgui/
```

## Known Issues & TODOs

1. **File save not implemented**: Save memory function is stubbed.
2. **Clipboard paste**: Paste code function is stubbed but not implemented.
3. **No native file dialog**: File loading uses a text input field + directory scan of soft-asm/ instead of a system file picker.
