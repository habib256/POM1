# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

POM1 v1.7 is an Apple 1 emulator built with Dear ImGui. It emulates the MOS 6502 CPU and Apple 1 hardware including memory-mapped I/O, display, keyboard input, the Apple Cassette Interface (ACI) with live audio and tape files, Uncle Bernie's GEN2 Color Graphics Card (280×192 HIRES, NTSC artifact color), the P-LAB A1-SID Sound Card (MOS 6581/8580 SID, 3-voice synthesis), the P-LAB microSD Storage Card (65C22 VIA, FAT32 filesystem), the P-LAB MODEM BBS (65C51 ACIA, TCP/TELNET), and the P-LAB Terminal Card (TCP server for external terminal control). The UI is fully in English. Builds on Linux, macOS, Windows, and Web (Emscripten/WASM).

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
source /path/to/emsdk/emsdk_env.sh   # e.g. ~/emsdk/emsdk_env.sh
mkdir -p build-wasm && cd build-wasm
emcmake cmake ..
emmake make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
emrun pom1_imgui.html                # Test locally (needs a local HTTP server on some setups)
```

**Outputs** in `build-wasm/`: `pom1_imgui.html`, `pom1_imgui.js`, `pom1_imgui.wasm`, and `pom1_imgui.data`. The `.data` bundle is Emscripten’s **MEMFS** preload, produced by CMake `--preload-file` rules:

| Host path (repo) | Mount in WASM VFS |
|------------------|-------------------|
| `roms/`          | `roms/`           |
| `fonts/`         | `fonts/`          |
| `software/`      | `software/`       |
| **`sdcard/`**    | **`sdcard/`**     |

**Rebuild WASM** after changing any file under those directories (or after editing `build-wasm/shell.html`) so `pom1_imgui.data` stays in sync.

**WebAssembly and virtual SD card (`sdcard/`)**: On desktop, `Memory` constructor probes `sdcard`, `../sdcard`, and `../../sdcard` relative to the process working directory and calls `MicroSD::setSDCardPath()` when a directory exists. In the browser, the same tree is available because **`sdcard/` is preloaded** at the virtual path `sdcard/` (see table above). Enable **Hardware > P-LAB microSD Storage Card**, then run **`8000R`** to start the SD CARD OS — `DIR`, `LOAD`, `CD`, etc. operate on that embedded filesystem. The public WASM build cannot read the visitor’s local disk; only content baked into `pom1_imgui.data` is visible to the emulated card. To ship new files on the web, add them under **`sdcard/`** in the repository and rebuild WASM.

Hardware reference: [P-LAB Apple-1 microSD Storage Card](https://p-l4b.github.io/sdcard/).

### Assembling programs (requires cc65)
```bash
ca65 -o build/program.o software/program.asm
ld65 -C software/apple1.cfg -o build/program.bin build/program.o
```

## Architecture Overview

### Core Emulation Layer
- **M6502.cpp/h**: Complete MOS 6502 CPU emulation with all opcodes, addressing modes, and cycle counting. Key types: `op` is `quint16` for proper 16-bit address handling in all addressing modes. `tmp` is `int` for carry/borrow detection via bit 8.
- **Memory.cpp/h**: 64KB address space with ROM loading (with bounds checking), memory-mapped I/O (keyboard 0xD010/0xD011, display 0xD012) with PIA 6821 address aliasing (`$D0Fx`↔`$D01x`), binary and hex dump file loading (with inline comment stripping), save to file, and configurable terminal speed.

### UI Layer (ImGui-based)
- **main_imgui.cpp**: GLFW/OpenGL3 initialization (GL 3.2 Core, GLSL 150) and main render loop with GLFW keyboard callbacks chained to ImGui. `GLFW_OPENGL_FORWARD_COMPAT` is macOS-only (`#ifdef __APPLE__`).
- **MainWindow_ImGui.cpp/h**: Main application window with menu bar, toolbar with icon buttons, status bar, window management, CPU speed control (1/2/Max MHz), file loading/saving dialogs, clipboard paste, memory map window, and keyboard input handling.
- **Screen_ImGui.cpp/h**: Apple 1 display emulation (40x24 character grid) with green/white monitor modes, `@` blinking cursor (timer uses `fmod` to avoid float overflow), scanline CRT effect, configurable text scale, and bitmap glyph rendering sourced from `roms/charmap.rom` when available.
- **MemoryViewer_ImGui.cpp/h**: Interactive hex editor with color-coded regions (matching Memory Map colors), search (hex and ASCII), bookmarks, navigation shortcuts, and real-time editing.
- **GraphicsCard.cpp/h**: [Uncle Bernie's GEN2 Color Graphics Card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) emulation. Passively reads CPU RAM at `$2000-$3FFF` and renders a 280×192 HIRES image with NTSC artifact color (violet/green for group 1, blue/orange for group 2, white for adjacent pixels) in a separate ImGui window. Two-pass rendering: glow halos (semi-transparent rounded rects) then solid pixels on top. Apple II-compatible non-linear scanline memory layout (`scanlineAddress()`). Toggled via Hardware menu or toolbar button; auto-loads a demo HGR image from `software/hgr/` when plugged in.
- **TMS9918.cpp/h**: [P-LAB Apple-1 Graphic Card](https://p-l4b.github.io/graphic/) emulation. TMS9918A Video Display Processor with 16KB VRAM, I/O at `$CC00` (data) / `$CC01` (control/status). 256×192 resolution, 15 colors, 32 sprites. Supports Graphics I (32×24 tiles), Graphics II (full bitmap), Text (40×24), and Multicolor modes. Renders to a separate ImGui window. Compatible with the [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib) software library. Toggled via Hardware menu or toolbar button.
- **AudioDevice.cpp/h**: Central audio output device that owns the hardware (miniaudio on desktop, Web Audio ScriptProcessorNode on WASM). Defines the `AudioSource` interface (`fillAudioBuffer(float*, int)`). Mixes all registered `AudioSource` instances (CassetteDevice, SID) into a single mono float32 output at 44.1 kHz. Sources are added/removed dynamically via `addSource()`/`removeSource()`.
- **SID.cpp/h**: [P-LAB A1-SID Sound Card](https://p-l4b.github.io/A1-SID/) emulation. MOS 6581/8580 SID chip with 3 voices (triangle, sawtooth, pulse, noise waveforms), ADSR envelopes with exponential decay and delay bug, Zero-Delay Feedback SVF filter (Zavalishin trapezoidal topology) with 6581 non-linear cutoff curve and op-amp saturation, 4-bit master volume with digi playback DC offset. 4× oversampling at 176.4 kHz with triangular decimation, 18 kHz output lowpass. I/O at `$C800`-`$CFFF` (29 registers, address `& 0x1F`). Implements `AudioSource` — registered with `AudioDevice` when plugged. Coexists with TMS9918 at `$CC00`-`$CC01`. Toggled via Hardware menu or toolbar button.

- **MicroSD.cpp/h**: [P-LAB Apple-1 microSD Storage Card](https://p-l4b.github.io/sdcard/) emulation. 65C22 VIA chip at `$A000`-`$A00F` bridging the 6502 CPU to an emulated ATMEGA MCU that performs filesystem operations. SD CARD OS ROM (8KB EEPROM) loaded at `$8000`-`$9FFF` provides a DOS-like CLI with commands: DIR, LS, CD, LOAD, SAVE, READ, WRITE, DEL, MKDIR, RMDIR, PWD, MOUNT. Handshake protocol via PORTB bit 0 (CPU_STROBE) / bit 7 (MCU_STROBE) with PORTA as bidirectional data bus. Maps host `sdcard/` directory as virtual FAT32 SD card. Tagged filenames (`NAME#TTAAAA`) encode file type and load address. Firmware source: [apple1-sdcard](https://github.com/nippur72/apple1-sdcard). Toggled via Hardware menu or toolbar button.

- **WiFiModem.cpp/h**: [P-LAB Apple-1 Wi-Fi Modem](https://p-l4b.github.io/wifi/) emulation (menu: "P-LAB MODEM BBS"). 65C51 ACIA at `$B000`-`$B003` (4 registers: DATA, STATUS, COMMAND, CONTROL) with ESP8266 AT command interpreter. Hayes AT commands (AT, ATDT host:port, ATH, ATE0/ATE1, ATI, ATZ) with TELNET IAC protocol handling (WILL/WONT/DO/DONT negotiation, subnegotiation filtering). Non-blocking TCP socket client with baud rate simulation (50–19200 baud from W65C51N table). Escape sequence (+++) detection with 1-second guard time. Circular 4096-byte receive buffer with cycle-accurate delivery. Desktop only (`#if !POM1_IS_WASM`); WASM stubs return `NO CARRIER`. Toggled via Hardware menu or toolbar button.

- **TerminalCard.cpp/h**: [P-LAB Apple-1 Terminal Card](https://p-l4b.github.io/terminal/) emulation. Passive bidirectional serial bridge — eavesdrops on `$D012` display writes and injects keystrokes into `$D010`/`$D011`. No new I/O addresses. Exposes a TCP server on `localhost:6502` — connect with `telnet localhost 6502` or any terminal emulator. Modes: 7-bit (default, CR→CRLF, filter non-printable, optional uppercase CTRL-O/CTRL-I) and 8-bit raw pass-through (CTRL-T). Control commands: CTRL-L (clear screen), CTRL-R (reset Apple 1). TELNET IAC negotiation handled (refuses all WILL/DO). Native Apple 1 screen continues working in parallel. `setKeyPressedRaw()` in Memory bypasses forced uppercase for lowercase/8-bit support. Pending reset/clear use `std::atomic<bool>` flags consumed outside `stateMutex` to avoid deadlock. Desktop only; WASM stubs. Toggled via Hardware menu or toolbar button. Bundled terminal program: `software/wifi/terminal.txt` (ACIA polling bridge, load at `$0280`).

### ROM Files (roms/)
- **WozMonitor.rom** (256B @ 0xFF00): Wozniak's system monitor
- **basic.rom** (4KB @ 0xE000): Apple BASIC interpreter
- **krusader-1.3.rom** (8KB @ 0xA000): Symbolic assembler
- **charmap.rom** (1KB): Character map for display
- **sdcard.rom** (8KB @ 0x8000): P-LAB SD CARD OS firmware (from [apple1-sdcard](https://github.com/nippur72/apple1-sdcard))

The main firmware ROMs are loaded automatically at startup by `Memory::loadWozMonitor()`, `loadBasic()`, `loadKrusader()`, and `loadAciRom()`. The terminal renderer also uses `charmap.rom` when available.

### Software directory (software/)
Contains Apple 1 programs in Woz Monitor hex dump format (.txt) organized in subdirectories:
- **games/**: Games — Microchess, LittleTower, Lunar Lander, Blackjack, 2048, Mini Star Trek, etc.
- **demos/**: Demos — Game of Life, Maze (Sidewinder), Maze 2 (Recursive Backtracker), Mandelbrot, Cellular automaton, etc.
- **dev/**: Development tools — Woz Monitor, Enhanced BASIC, fig-FORTH
- **tests/**: Hardware test programs — hex I/O, keyboard, terminal tests
- **hgr/**: GEN2 HGR demo images (raw 8 KB binary loaded at `$2000`)
- **tms9918/**: P-LAB TMS9918 programs — Tetris, demo suite, PicShow image viewer (KickC binaries loaded at `$0280`), **TMS_SID_Demo** (world's first Apple 1 TMS9918+SID combined demo — Graphics II title screen + Streets of Rage 2 SID tune playing simultaneously, generated by `tools/make_tms_sid_demo.py`)
- **sid/**: P-LAB A1-SID programs — 30 SID tunes converted from HVSC `.sid` files via `tools/sid2apple1.py` (Rob Hubbard, Martin Galway, Jeroen Tel, Ben Daglish, Chris Huelsbeck). Includes IRQ-driven tunes (Arkanoid, Game Over, Combat School, Last V8, Myth). Binary `.bin` files loaded at `$0280`.
- **wifi/**: P-LAB Wi-Fi Modem terminal program (`terminal.txt`) — ACIA polling bridge for BBS connections, load at `$0280`, run with `0280R`

### Virtual SD Card (sdcard/)
The `sdcard/` directory at the project root is the virtual microSD card content for the P-LAB microSD Storage Card emulation. Files placed here are accessible from the SD CARD OS shell (enter with `8000R`). Files use tagged naming: `NAME#TTAAAA` where `TT` is the file type (`06`=binary, `F1`=Integer BASIC, `F8`=AppleSoft BASIC) and `AAAA` is the hex load address. Example: `GAME#F10300` is a BASIC program loading at `$0300`.

For **WebAssembly**, this directory is copied into the `.data` preload at the virtual path `sdcard/` — see **Build WASM** (table + notes) above.

Programs can be loaded via File > Load Memory, which provides a file browser with folder navigation. Assembly source files (`.asm`) can be assembled with cc65. Most programs come from [apple1software.com](https://apple1software.com/), an outstanding archive of Apple 1 software, hardware documentation, and historical resources. [AppleFritter](https://applefritter.com/apple1/) is the community hub where much of the technical research, BASIC version history, and hardware knowledge originates.

## Key Implementation Details

### Memory-Mapped I/O
- **0xD010 (KBD)**: Keyboard data register. Returns last key with bit 7 set. Reading clears the keyboard strobe (keyReady flag), matching PIA 6821 behavior.
- **0xD011 (KBDCR)**: Keyboard control register. Bit 7 = 1 when key is ready.
- **0xD012 (DSP)**: Display output register. Writing a character triggers the display callback. Reading returns bit 7 = 0 (display ready) after the terminal speed delay has elapsed, or bit 7 = 1 (busy) during the delay. The busy counter is clamped to 0 (never goes negative).

**PIA 6821 address aliasing**: The real Apple 1 has incomplete address decoding on the PIA chip. Any address in `$D0xx` with the same low 2 bits maps to the same register: `$D0F0`↔`$D010` (KBD), `$D0F1`↔`$D011` (KBDCR), `$D0F2`↔`$D012` (DSP). This matters because the original (Pagetable) version of Apple BASIC uses `$D0F2` for display I/O, while the Briel/Replica 1 version uses `$D012`. POM1 normalizes all `$D0xx` addresses to `$D010-$D012` in `memRead`/`memWrite`, so both BASIC versions work.

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
- `loadHexDump(filename, startAddress)`: Parses Woz Monitor hex format. Supports comment lines and inline comments (`//`, `#`, `;`), continuation lines, `T` prefix (turbo), `X` marker, and `R` suffix (run address). Handles single-line files where data bytes merge with addresses (e.g., `ED0300:` is split into data `ED` + address `0300`). Inline comments (e.g., `A9 00 // LDA #0`) are stripped to prevent hex letters in mnemonics from being parsed as data.
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

### Cassettes directory (cassettes/)
Reference material for the Apple cassette library — **`.ogg`** captures of original tapes (preservation / listening). POM1's **ACI Cassette Control** loads **`.wav`** or **`.aci`** only; convert or re-encode to WAV if you want to feed those captures into the emulated ACI.

### Additional Directories
- **bios/**: Legacy BIOS/ROM files from the original POM1 project.
- **cassettes/**: Original-tape `.ogg` captures (reference / preservation).
- **fonts/**: Font Awesome icon font (fa-solid-900.ttf) used by the UI.
- **doc/**: Documentation — Krusader 1.3 manual PDF, Apple 1 software reference, screenshot.
- **IconsFontAwesome6.h**: Font Awesome 6 icon codepoints header.

## Memory Map

```
0x0000-0x00FF  Zero page
0x0100-0x01FF  Stack
0x0200-0x1FFF  User RAM (programs typically load at 0x0280 or 0x0300)
0x2000-0x3FFF  GEN2 HGR Framebuffer (8 KB — when card is plugged)
0x4000-0x7FFF  User RAM
0x8000-0x9FFF  SD CARD OS ROM (8 KB EEPROM — when P-LAB microSD is plugged)
0xA000-0xA00F  VIA 65C22 I/O (16 registers — when P-LAB microSD is plugged)
0xA010-0xAFFF  User RAM
0xB000-0xB003  ACIA 65C51 I/O (4 registers — when P-LAB MODEM BBS is plugged)
0xB004-0xBFFF  User RAM
0xC800-0xCFFF  A1-SID I/O (29 registers, addr & 0x1F, when P-LAB SID is plugged)
0xCC00         TMS9918 DATA - VRAM data port   (when P-LAB card is plugged)
0xCC01         TMS9918 CTRL - Control/status    (when P-LAB card is plugged)
0xD010         KBD - Keyboard data register    (aliases: $D0F0, $D030, etc.)
0xD011         KBDCR - Keyboard control register (aliases: $D0F1, $D031, etc.)
0xD012         DSP - Display output register   (aliases: $D0F2, $D032, etc.)
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

### v1.7.1 (April 2026) — SID converter v2 & TMS9918+SID demo
- **SID converter v2** (`tools/sid2apple1.py`): all patching passes now instruction-aware (shared `INST_LENGTHS` table replaces byte-by-byte scanning, dramatically reduces false positives), Pass 2 expanded with indirect store opcodes ($81 STA ($xx,X), $91 STA ($xx),Y), Pass 3 expanded with LDX/LDY/BIT/CMP/CPX/CPY for CIA reads and full register range ($00-$07, $0D-$0F), Pass 4 expanded with indexed addressing modes (STA abs,X/Y, LDA abs,X/Y, LDY abs,X, LDX abs,Y, INC/DEC abs,X), Pass 5 refined with neighbor-pair requirement (reduces false positives on isolated data bytes), IRQ detection expanded with LDX/STX and LDY/STY vector patterns
- **SID player wrapper**: displays "APPLE1 P-LAB SID PLAYER" banner before "NOW PLAYING:", adds "ESC TO STOP" — pressing ESC silences all 3 SID voices + master volume and returns to Woz Monitor ($FF00)
- **TMS9918+SID demo** (`tools/make_tms_sid_demo.py`): world's first Apple 1 program combining TMS9918 graphics and SID audio simultaneously — Graphics II title screen with 8×8 bitmap font, rainbow color bars, per-line colored text, playing Streets of Rage 2 (DJ Space) on the A1-SID. Generic chunk-based VRAM writer, reuses all `sid2apple1.py` patching passes. Output: `software/tms9918/TMS_SID_Demo.bin` (requires both P-LAB TMS9918 and A1-SID enabled)
- All 13 bundled SID tunes reconverted with improved converter

### v1.7 (April 2026) — P-LAB MODEM BBS & Terminal Card
- [P-LAB Apple-1 Wi-Fi Modem](https://p-l4b.github.io/wifi/) emulation: 65C51 ACIA at `$B000`-`$B003`, ESP8266 AT command interpreter, Hayes AT commands (AT, ATDT host:port, ATH, ATE, ATI, ATZ), TELNET IAC protocol handling, non-blocking TCP socket client, baud rate simulation (50–19200 baud), escape sequence (+++) with guard time, circular 4096-byte receive buffer
- [P-LAB Apple-1 Terminal Card](https://p-l4b.github.io/terminal/) emulation: passive bidirectional serial bridge — TCP server on `localhost:6502` for external terminal control. Connect with `telnet localhost 6502` to control the entire Apple 1 (Woz Monitor, BASIC, programs) from any terminal emulator
- Terminal Card modes: 7-bit (default, CR→CRLF, uppercase conversion CTRL-O/CTRL-I) and 8-bit raw pass-through (CTRL-T). Control commands: CTRL-L (clear screen), CTRL-R (reset Apple 1)
- Native Apple 1 screen continues working in parallel with the Terminal Card
- TELNET IAC negotiation: both WiFi Modem and Terminal Card refuse all WILL/DO with WONT/DONT, filter subnegotiation
- `Memory::setKeyPressedRaw()`: new method bypassing forced uppercase conversion, used by Terminal Card for lowercase/8-bit support
- Thread-safe pending reset/clear via `std::atomic<bool>` flags consumed outside `stateMutex` in `EmulationController::runEmulationSlice()` to avoid deadlock
- Bundled ACIA terminal program (`software/wifi/terminal.txt`): polls ACIA `$B000`-`$B003` for keyboard↔modem bridge, load at `$0280`, run with `0280R`
- Auto-enable Wi-Fi Modem when loading from `software/wifi/` directory
- Wi-Fi Modem status window: connection state, baud rate, ACIA registers, traffic counters
- Terminal Card status window: server/client state, modes, byte counters, control commands help
- Memory Map info panel shows Wi-Fi Modem ACIA registers and Terminal Card passive eavesdrop info when enabled
- Desktop only (`#if !POM1_IS_WASM`): Terminal Card menu/toolbar hidden in WASM builds; WiFi Modem stubs return `NO CARRIER`
- Toggle via Hardware menu or toolbar buttons (WiFi icon / Terminal icon)
- Toolbar icon order reorganized: Load, SD Card, Cassette, SID, HGR, TMS9918, Terminal, BBS
- Hardware menu reorganized: ACI Cassette Control + Bernie's GEN2 (classics), then P-LAB cards grouped (microSD, SID, TMS9918, Terminal Card, MODEM BBS)
- Wi-Fi Modem renamed to "P-LAB MODEM BBS" in Hardware menu
- Cassette Control renamed to "ACI Cassette Control" in Hardware menu
- Cassettes directory moved from `software/cassettes/` to `cassettes/` at project root (original-tape `.ogg` captures for reference/preservation)

### v1.6 (April 2026) — P-LAB microSD Storage Card
- [P-LAB Apple-1 microSD Storage Card](https://p-l4b.github.io/sdcard/) emulation: 65C22 VIA at `$A000`-`$A00F`, ATMEGA MCU protocol emulation, SD CARD OS ROM (8KB EEPROM) at `$8000`-`$9FFF`, DOS-like CLI with DIR/LS/CD/LOAD/SAVE/READ/WRITE/DEL/MKDIR/RMDIR/PWD/MOUNT commands
- Virtual SD card maps host `sdcard/` directory as FAT32 filesystem — files placed there are accessible from the SD CARD OS shell (`8000R`)
- **WebAssembly**: `CMakeLists.txt` preloads `sdcard/` into MEMFS (`--preload-file …/sdcard@sdcard`) alongside `roms/`, `fonts/`, and `software/` so the browser build sees the same virtual card tree as desktop
- Tagged filename support (`NAME#TTAAAA`): file type (#06=binary, #F1=Integer BASIC, #F8=AppleSoft BASIC) and hex load address
- Fuzzy filename matching for LOAD command (case-insensitive prefix match, sends full tagged filename to firmware)
- VIA 65C22 handshake protocol: synchronous MCU emulation responds instantly within CPU write cycles — no busy-wait overhead
- MCU response protocol verified against ATMEGA firmware source: CMD_PWD sends null-terminated path string, CMD_MOUNT sends null-terminated status string, DIR/LS entries prepended with OK_RESPONSE status byte, stale responses auto-aborted on new command
- Timer 1 support for SD CARD OS timeout detection (free-running and one-shot modes)
- Robustness: string buffer limit (256 bytes), write size limit (32 KB), DIR listing timeout (500K cycles), fallback ERR_RESPONSE on invalid dir entries, ROM region cleared before loading (handles 8177-byte ROM in 8192-byte space)
- Debug logging conditional (`debugEnabled` flag, off by default for performance)
- Memory Map and Memory Viewer show SD CARD OS ROM (amber) and VIA I/O (orange) when card is plugged
- Auto-enable when loading from `sdcard/` directory
- Toggle via Hardware menu or toolbar button (SD card icon)
- Firmware source: [apple1-sdcard](https://github.com/nippur72/apple1-sdcard) by Antonino Porcino

### v1.5 (April 2026) — P-LAB A1-SID Sound Card
- [P-LAB A1-SID Sound Card](https://p-l4b.github.io/A1-SID/) (MOS 6581/8580 SID): 3 voices, 4 waveforms (triangle/sawtooth/pulse/noise), ADSR envelopes with exponential decay and 6581 delay bug, Zero-Delay Feedback SVF filter (Zavalishin trapezoidal) with non-linear 6581 cutoff curve and op-amp saturation (input + BP feedback via `tanh`), 4-bit master volume with digi playback DC offset, I/O at `$C800`-`$CFFF`, toggle via Hardware menu or toolbar
- SID emulation quality: 4× oversampling (176.4 kHz) with triangular decimation [1,3,3,1]/8, fractional phase accumulator, correct noise LFSR bit extraction (bits 22/20/16/13/11/7/4/2), noise+waveform LFSR writeback, combined waveform low-bit pull-down, voice DC bias (+0.17), waveform-0 exponential decay (anti-click), PW extreme handling ($000/$FFF), 18 kHz output lowpass, DC blocker for digi, ADSR cycle accumulator clamped to max rate period
- AudioDevice: central audio mixer extracted from CassetteDevice — owns the hardware (miniaudio / Web Audio), mixes registered `AudioSource` instances (CassetteDevice + SID)
- `tools/sid2apple1.py`: converts C64 PSID/RSID `.sid` files to Apple 1 `.bin` — 7 instruction-aware patching passes (absolute SID $D4→$C8, immediate pointer setups including indirect $81/$91, CIA register reads/writes with expanded opcode/register coverage, VIC $D0xx neutralization with indexed addressing modes, SID data tables with neighbor-pair false-positive filtering, IRQ vector/CIA writes, Kernal exit redirection), IRQ-driven ISR detection (`play=$0000`, LDA/LDX/LDY patterns), player wrapper with "APPLE1 P-LAB SID PLAYER" banner, ESC-to-stop (polls $D011, silences SID voices + volume, returns to Woz Monitor), RTI stub for interrupt-context players, PAL/NTSC timing, `--song N`, `--all-songs`, `--hex`, `--batch`
- `tools/make_tms_sid_demo.py`: generates a combined TMS9918 + SID demo binary — TMS9918 Graphics II title screen (8×8 font rendering, rainbow color bars, per-line colored text) with SID music playing simultaneously, generic chunk-based VRAM writer, reuses `sid2apple1.py` patching passes, outputs a single `.bin` at `$0280`
- Bundled 13 SID tunes from HVSC (Rob Hubbard, Martin Galway, Ben Daglish, Chris Huelsbeck) — in `software/sid/`
- Krusader ROM no longer loaded at default — `$A000-$BFFF` is free User RAM (enables SID tunes loading in that range)
- Auto-enable hardware cards when loading from `software/sid/`, `software/hgr/`, `software/tms9918/`
- Memory Map and Memory Viewer show A1-SID I/O region in purple when card is plugged
- Coexists with TMS9918 at `$CC00`-`$CC01` (TMS9918 has priority at its addresses)

### v1.4 (April 2026) — P-LAB Apple-1 Graphic Card (TMS9918)
- [P-LAB Apple-1 Graphic Card](https://p-l4b.github.io/graphic/) (TMS9918 VDP): 256×192, 15 colors, 32 sprites, I/O at `$CC00`/`$CC01`, Graphics I/II/Text/Multicolor modes, compatible with [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib), toggle via Hardware menu or toolbar
- Bundled P-LAB software: TMS9918 demo suite (Screen 1, Screen 2, sprites, bitmap), Tetris, PicShow image viewer — pre-built binaries in `software/tms9918/`
- File browser now shows `.bin` files alongside `.txt`
- Default binary load address changed from `$0300` to `$0280` (KickC Apple 1 standard)

### v1.3 (April 2026) — Uncle Bernie's GEN2 Color Graphics Card
- Uncle Bernie's GEN2 Color Graphics Card: 280×192 HIRES at `$2000-$3FFF`, NTSC artifact color (violet/green/blue/orange), pixel glow, Apple II-compatible scanline layout, toggle via Hardware menu or toolbar, demo image auto-load
- HGR Maze program: Recursive Backtracker maze generator rendering directly into the GEN2 framebuffer (19×11 cells, 7×8 pixel blocks, byte-aligned white walls), with maze counter, CLD safety, and work RAM cleanup
- Memory Viewer: inline hex editing on double-click (replaces modal popup)
- cc65 linker config for GEN2 programs (`software/hgr/apple1_gen2.cfg`): reserves `$2000-$3FFF` for HGR framebuffer

### v1.2 (April 2026) — 50th anniversary of Apple Computer
- PIA 6821 address aliasing: `$D0Fx` mapped to `$D01x` — original (Pagetable) and Briel Apple BASIC versions both work
- WASM Web Audio: ACI cassette live audio via ScriptProcessorNode (44.1 kHz, 512-sample buffer), auto-resume on first user gesture
- Hex dump loader: inline comment stripping (`//`, `;`) fixes data corruption from mnemonic letters (e.g., `LDA`, `DEX`)
- Display: increased character brightness and glow, greener phosphor in charmap mode, host-ASCII vertical alignment fix
- Desktop fullscreen: Apple 1 screen now fills the display correctly
- Memory Settings: added Reload ACI ROM button
- Defaults: real-time stabilized audio (instead of hardware-faithful)
- Bug fixes: WAV export overflow guard, WAV format early validation, `lastError` made `mutable`, dead code removal

### v1.1 (April 2026)
- Apple Cassette Interface: ACI ROM at `$C100`, I/O at `$C000`/`$C081`, live audio (hardware-faithful or stabilized), load/save `.aci` and `.wav`, Cassette Control window
- Terminal: bitmap rendering from `charmap.rom`, CRT scanlines/glow, green / brown / monochrome, optional host-ASCII mode; viewport aspect ~280:192; charmap contrast tuning
- Emulation on a worker thread with UI snapshots; CPU speed (1 MHz / 2 MHz / Max) syncs immediately from toolbar and Settings
- Memory Map: two-column layout (map + I/O under map, legend + CPU vectors)
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

## Version String Locations

When bumping the version number, update **all** of these:
- `main_imgui.cpp` — console output (line ~46) and GLFW window title (line ~72)
- `MainWindow_ImGui.cpp` — About dialog
- `Screen_ImGui.cpp` — Apple 1 welcome screen
- `build-wasm/shell.html` — HTML `<title>` and `<h1>` banner (2 occurrences)
- `README.md` — title and intro
- `CLAUDE.md` — project overview and version history

## Known Issues & TODOs

1. **No native file dialog**: File loading/saving uses built-in file browsers instead of system file pickers.
2. **GEN2 HGR Maze higher resolution**: The current HGR Maze uses 19×11 cells (7×8 pixel blocks). A higher-resolution version with 16-bit DFS and smaller blocks (e.g., 34×23 cells, 4×4 blocks) was attempted but produced visual artifacts due to NTSC artifact color rendering of non-byte-aligned pixel blocks. Needs a rendering approach that produces solid white walls at sub-byte granularity.
3. **GEN2 programs in `software/hgr/`**: Only a demo image and the HGR Maze are included. More GEN2 programs could be added (e.g., image viewers, drawing tools, more demos).
4. **SID: Arkanoid (Martin Galway) does not play**: The IRQ ISR is detected at `$4086` and the conversion completes, but the tune remains silent at runtime. Galway's player uses a multi-ISR architecture with VIC raster splits that switches between ISR addresses during playback — the static ISR detection picks up only the first handler. Needs a more sophisticated approach (e.g., dynamic ISR tracing or multi-handler support).
5. **SID: some IRQ-driven tunes may not work**: ISR detection relies on static pattern matching (`LDA #xx / STA $FFFE`). Players that compute the ISR address dynamically or use `JMP ($FFFE)` indirect vectors are not detected. BMX Kidz (Hubbard) is a known case.
6. **SID converter: false-positive patching** (mitigated in v1.7.1): All passes now use instruction-length walking instead of byte scanning, and Pass 5 requires neighbor-pair context. Residual-$D4 warnings in `--batch` mode help identify remaining edge cases.
