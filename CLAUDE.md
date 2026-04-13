# CLAUDE.md

Guidance for Claude Code working in this repository.

## Project Overview

POM1 is an Apple 1 emulator built with Dear ImGui. It emulates the MOS 6502 CPU and the original Apple 1 hardware (display, keyboard, ACI cassette), plus a stack of expansion cards: Uncle Bernie's GEN2 Color Graphics Card (HIRES, NTSC artifact color), the P-LAB A1-SID Sound Card (MOS 6581/8580), the P-LAB Apple-1 Graphic Card (TMS9918 VDP), the P-LAB microSD Storage Card (65C22 VIA + ATMEGA), the P-LAB MODEM BBS (65C51 ACIA + TCP/TELNET), the P-LAB Terminal Card (TCP server), the P-LAB A1-IO Board & RTC (65C22 VIA + ATMEGA32 + DS3231), and the CFFA1 CompactFlash Interface (Rich Dreher, ATA/IDE + ProDOS `.po` image). UI is in English. Builds on Linux, macOS, Windows, and Web (Emscripten/WASM).

User-facing feature list, install instructions, ROM table, keyboard shortcuts, screenshots, and the software library listing all live in **`README.md`**. Open work and tech debt live in **`TODO.md`**. Past releases are in `git log`.

## Build & Run

```bash
# First time
./setup_imgui.sh                # Linux/macOS — fetch ImGui + deps
setup_imgui.bat                 # Windows — vcpkg + GLFW

# Build
cd build && cmake .. && make                    # Linux/macOS
cmake --build . --config Release                # Windows (MSVC)

# Run
./run_emulator.sh                               # copies ROMs from roms/ if needed
./pom1_imgui                                    # or directly from build/
```

ROMs must be next to the executable; the run scripts handle the copy.

### WASM build (Emscripten)

```bash
source /path/to/emsdk/emsdk_env.sh
mkdir -p build-wasm && cd build-wasm
emcmake cmake ..
emmake make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
emrun pom1_imgui.html
```

Outputs in `build-wasm/`: `pom1_imgui.{html,js,wasm,data}`. The `.data` bundle is Emscripten **MEMFS** preload (CMake `--preload-file` rules):

| Host path | Mount in WASM VFS |
|-----------|-------------------|
| `roms/`    | `roms/`           |
| `fonts/`   | `fonts/`          |
| `software/`| `software/`       |
| `sdcard/`  | `sdcard/`         |
| `cfcard/`  | `cfcard/`         |

Rebuild WASM after any change under those directories (or after editing `build-wasm/shell.html`) so `pom1_imgui.data` stays in sync. The browser build can only see content baked into `pom1_imgui.data` — to ship new files on the web, add them under the relevant directory and rebuild.

The desktop `Memory` constructor probes `sdcard`, `../sdcard`, `../../sdcard` relative to the process working directory and calls `MicroSD::setSDCardPath()` when a directory exists. It probes `cfcard`, `../cfcard`, `../../cfcard` for `cfcard.po` and opens it with the CFFA1 emulation when present.

### Assembling programs (cc65)

```bash
ca65 -o build/program.o software/program.asm
ld65 -C software/apple1.cfg -o build/program.bin build/program.o
# GEN2 HGR programs use software/hgr/apple1_gen2.cfg (reserves $2000-$3FFF).
# Sokoban variants target real Apple 1 hardware via three dedicated cfgs:
#   software/games/apple1_sok_4k.cfg   stock 4K (text)
#   software/games/apple1_sok_8k.cfg   stock 8K + TMS card (TMS variant)
#   software/games/apple1_sok_hgr.cfg  8K + GEN2 card     (HGR variant)
# They define a LEVELBUF segment (type=zp, $0020) for the RLE scratch buffer
# and a STATEGRID segment (type=bss, $0F00 or $1F00) for the game state grid,
# so neither touches the $4000 region. Use `.segment "LEVELBUF": zeropage`
# in asm to force zp,X addressing on the buffer.
```

## Architecture

Each `.cpp/.h` pair is responsible for one concern. The descriptions below are deliberately terse — only the things you can't deduce from reading the file.

### Core
- **M6502.cpp/h** — MOS 6502 CPU. `op` is `quint16` for 16-bit address handling across all addressing modes; `tmp` is `int` for carry/borrow detection via bit 8. BCD ADC propagates the low→high carry from `(accumulator & 0xF0)` (bit 4 of the BCD-adjusted accumulator), not from the unadjusted sum.
- **CpuClock.h** — Nominal 6502 clock **`POM1_CPU_CLOCK_HZ` = 1 022 727** (~1.022727 MHz, 14.31818 MHz ÷ 14). Exports `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` / `POM1_CPU_CYCLES_PER_FRAME_2X_60HZ` (rounded cycles per frame at 60 fps) and `POM1_CPU_CYCLES_PER_MILLISECOND`. Consumed by EmulationController/UI speed, SID, TMS9918 frame tick, modem baud timing and `+++` guard, Terminal Card polling, microSD idle timeouts, cassette realtime timebase, and terminal display delay.
- **Memory.cpp/h** — 64 KB address space. Owns every peripheral (`unique_ptr` + enable flag). Memory-mapped I/O dispatches inline. PIA 6821 address aliasing: `$D0xx` low-2-bits collapse onto `$D010-$D012`, so both Pagetable and Briel BASIC variants work. `setKeyPressedRaw()` bypasses forced uppercase for the Terminal Card.

### UI (ImGui)
- **main_imgui.cpp** — GLFW/OpenGL3 init (GL 3.2 Core, GLSL 150). `GLFW_OPENGL_FORWARD_COMPAT` is macOS-only.
- **MainWindow_ImGui.cpp/h** — Menu bar, toolbar, status bar, file dialogs, clipboard paste, hardware-card windows, machine presets. **Machine Presets** (`kMachinePresets[]` in anonymous namespace): each entry sets all hardware flags atomically via `applyMachineConfig(int)` and populates `pendingLayout`; `applyPendingLayout(const char*)` runs before each hardware window's `Begin()` to reposition with `ImGuiCond_Always`. Auto-enable hardware cards based on the source directory of a loaded file (`software/sid/`, `software/hgr/`, `software/tms9918/`, `software/wifi/`, `software/net/`, `sdcard/`); reloading from `software/net/` calls `wifiModemReset()` to drop any live connection.
- **Screen_ImGui.cpp/h** — 40×24 character grid, green/white CRT modes, blinking `@` cursor (`fmod` to avoid float overflow), scanline effect, bitmap glyphs from `roms/charmap.rom`.
- **MemoryViewer_ImGui.cpp/h** — Hex editor with color-coded regions, search, bookmarks, inline double-click editing.

### Peripherals
- **CassetteDevice.cpp/h** — Apple Cassette Interface (ACI). Woz ROM at `$C100-$C1FF`, I/O at `$C000-$C0FF` (`$C000` output flip-flop, `$C081` tape input). `AudioSource`-compatible: playback mixes into the shared 44.1 kHz stream via AudioDevice. Supports raw binary tapes and `.wav` captures.
- **GraphicsCard.cpp/h** — Uncle Bernie's GEN2 Color Graphics Card. Passively reads RAM `$2000-$3FFF`; renders 280×192 HIRES with NTSC artifact color (violet/green for group 1, blue/orange for group 2, white between). Two-pass: glow halos then solid pixels. Apple II-compatible non-linear scanline layout (`scanlineAddress()`).
- **TMS9918.cpp/h** — P-LAB Apple-1 Graphic Card. TMS9918A VDP, 16 KB VRAM, I/O at `$CC00`/`$CC01`. Compatible with [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib).
- **AudioDevice.cpp/h** — Central audio output. Owns the hardware (miniaudio on desktop, Web Audio ScriptProcessorNode on WASM). Defines `AudioSource` (`fillAudioBuffer(float*, int)`); mixes registered sources (CassetteDevice, SID) at 44.1 kHz mono float32.
- **SID.cpp/h** — P-LAB A1-SID. MOS 6581/8580 emulation with ZDF SVF filter (Zavalishin trapezoidal), 6581 non-linear cutoff, op-amp `tanh` saturation, 4-bit master volume + digi DC offset, 4× oversampling (176.4 kHz), 18 kHz output lowpass, ADSR with delay bug. I/O `$C800-$CFFF` (29 registers, addr `& 0x1F`). Coexists with TMS9918 at `$CC00-$CC01` (TMS9918 wins).
- **MicroSD.cpp/h** — P-LAB microSD Storage Card. 65C22 VIA at `$A000-$A00F` bridging the CPU to an emulated ATMEGA MCU. SD CARD OS ROM (8 KB EEPROM) at `$8000-$9FFF`. Handshake: PORTB bit 0 = CPU_STROBE, bit 7 = MCU_STROBE, PORTA = bidirectional data bus. Maps host `sdcard/` as virtual FAT32. Tagged filenames `NAME#TTAAAA` encode type + load address. Firmware: [apple1-sdcard](https://github.com/nippur72/apple1-sdcard).
- **CFFA1.cpp/h** — Rich Dreher's CompactFlash Interface for Apple-1. 8 KB firmware ROM at `$9000-$AFDF` (with ID bytes `$CF`/`$FA` at `$AFDC`/`$AFDD`), ATA/IDE registers at `$AFE0-$AFFF` (A4 not decoded → `$AFE0` mirrors `$AFF0`). Backs a ProDOS `.po` disk image; emulates READ/WRITE SECTOR + SET FEATURE only (everything else the firmware actually uses). Desktop auto-mount: the `Memory` constructor probes `cfcard/cfcard.po` up three directories.
- **A1IO_RTC.cpp/h** — P-LAB A1-IO Board & RTC. 65C22 VIA at `$2000-$200F` (⚠ overlaps the GEN2 HGR framebuffer — the two cards are mutually exclusive at the preset level) bridging to an emulated ATMEGA32 that drives a DS3231 RTC (date/time + internal temperature), a DS18B20 probe, 8 analog inputs, 4 digital inputs, and a 16-bit shift-register digital output. Broadcast protocol: 24 registers pumped on a 100-cycle period with PORTB STROBE handshake. Board reference: [A1-IO_RTC](https://p-l4b.github.io/A1-IO_RTC/).
- **WiFiModem.cpp/h** — P-LAB MODEM BBS. 65C51 ACIA at `$B000-$B003`, ESP8266 AT command interpreter, Hayes AT (AT, ATDT host:port, ATH, ATE0/1, ATI, ATZ), TELNET IAC (WILL/WONT/DO/DONT, subnegotiation filter, CR+LF→CR strip), non-blocking TCP, baud-rate simulation 50–19200, `+++` escape with 1 s guard (`POM1_CPU_CLOCK_HZ` cycles), 4096-byte circular Rx buffer. Public `requestDisconnect()` is the UI-thread-safe entry point (calls `handleATH()` under `modemMutex`). Desktop only (`#if !POM1_IS_WASM`); WASM stubs return `NO CARRIER`.
- **TerminalCard.cpp/h** — P-LAB Terminal Card. Passive bidirectional bridge: eavesdrops on `$D012` writes, injects keystrokes into `$D010`/`$D011`. TCP server bound to IPv4 loopback on port 6502 (IPv6 `::1` connections are refused — `telnet localhost` falls back to `127.0.0.1` cleanly). 7-bit (CR→CRLF, optional uppercase via Ctrl-O/I) and 8-bit raw (Ctrl-T) modes; control: Ctrl-L clear, Ctrl-R reset. Ctrl-T is the one control char that fires even in 8-bit mode (otherwise the user has no way back to 7-bit). Each control also has an **ESC-prefixed alternate** (ESC T, ESC O, ESC L, ESC R, ESC I) — necessary on macOS/BSD where the tty line discipline eats `Ctrl-T` (status), `Ctrl-O` (discard) and `Ctrl-R` (rprnt) before telnet can send them. The alternate state is `escapePending`; unrecognised ESC-sequences forward the held ESC and fall through so ANSI escape codes still reach the Apple 1. On accept, sends proactive `IAC WILL ECHO` + `IAC WILL SUPPRESS-GO-AHEAD` + `IAC DO SUPPRESS-GO-AHEAD` to flip the client into character-at-a-time mode (otherwise Linux/BSD `telnet` stays line-buffered and control keys never arrive). Accepts the matching replies silently (see `isOptionAccepted` in anonymous namespace); every other option is refused with DONT/WONT. Pending reset/clear use `std::atomic<bool>` flags consumed outside `stateMutex` to avoid deadlock with `EmulationController::runEmulationSlice()`. Desktop only.
- **SocketHandle.h** — Move-only RAII wrapper (`NativeSocket` = `SOCKET`/`int`) used by WiFiModem and TerminalCard. Closes the FD in the destructor so exceptions between `socket()` and the explicit close cannot leak.

## Key implementation details

### Memory-mapped I/O
- **`$D010` (KBD)** — last key with bit 7 set; reading clears the keyboard strobe (matches PIA 6821).
- **`$D011` (KBDCR)** — bit 7 = 1 when a key is ready.
- **`$D012` (DSP)** — write triggers the display callback. Read returns bit 7 = 0 (ready) after the terminal-speed delay, or bit 7 = 1 (busy) during the delay; busy counter clamped to ≥ 0.

PIA 6821 incomplete address decoding aliases all `$D0xx` to `$D010-$D012` based on the low 2 bits, so both BASIC variants work. `Memory::memRead`/`memWrite` normalize before dispatch. Keyboard input is forced uppercase by default (`setKeyPressed`); the Terminal Card uses `setKeyPressedRaw()` to bypass this.

### CPU execution
Three modes managed by MainWindow_ImGui via EmulationController: **Stopped**, **Running** (`executionSpeed` cycles per frame — nominal ~1.022727 MHz = `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` (17045), double = `POM1_CPU_CYCLES_PER_FRAME_2X_60HZ` (34091), Max = 1000000), and **Step** (`stepCpu()`). CPU getters (`getAccumulator()`, `getProgramCounter()`, ...) are for the debug UI.

### Addressing modes
`Abs`, `AbsX`, `AbsY`, `Ind`, `IndZeroX`, `IndZeroY`, ... store the resolved address in `op` (quint16). `Imm()` stores `programCounter` in `op`, so `memRead(op)` fetches the immediate value. All instructions go through `memory->memRead(op)` / `memory->memWrite(op, value)` uniformly.

### Loading programs
- `Memory::loadBinary(filename, startAddress)` — raw binary at the given address.
- `Memory::loadHexDump(filename, startAddress)` — Woz Monitor hex format. Supports comment lines and inline `//`, `#`, `;` comments (the inline strip is what prevents mnemonic letters like `LDA`, `DEX` being parsed as data), continuation lines, `T` prefix (turbo), `X` marker, `R` suffix (run address). Also handles single-line files where data merges with addresses (e.g. `ED0300:` is split into data `ED` + address `0300`).
- File save dialog exports a memory range as binary or Woz Monitor hex dump.
- Clipboard paste feeds characters to the Apple 1 keyboard (capped at 4096 chars).

### Memory Map window
16×16 grid (256 pages = 64 KB), color-coded regions, KB labels, PC/SP indicators, hover tooltips guarded by `IsWindowHovered`, legend, I/O register details, real-time CPU vector readout.

## Memory Map

```
$0000-$00FF  Zero page
$0100-$01FF  Stack
$0200-$1FFF  User RAM (programs typically load at $0280 or $0300)
$2000-$200F  A1-IO RTC VIA 65C22 (when the A1-IO Board is plugged — mutually exclusive with GEN2 below)
$2000-$3FFF  GEN2 HGR framebuffer (8 KB — when Uncle Bernie's GEN2 card is plugged)
$4000-$7FFF  User RAM
$8000-$9FFF  SD CARD OS ROM (8 KB — when P-LAB microSD is plugged)
$9000-$AFDF  CFFA1 firmware ROM (~8 KB — when CFFA1 is plugged; shadows microSD ROM + BASIC low page)
$A000-$A00F  VIA 65C22 I/O (when P-LAB microSD is plugged)
$A010-$AFDF  User RAM (when neither microSD nor CFFA1 is plugged)
$AFE0-$AFFF  CFFA1 ATA/IDE registers (A4 undecoded: $AFE0 mirrors $AFF0; ID at $AFDC/$AFDD)
$B000-$B003  ACIA 65C51 I/O (when P-LAB MODEM BBS is plugged)
$B004-$BFFF  User RAM
$C000-$C0FF  Apple Cassette Interface I/O ($C081 tape input, $C000 output flip-flop)
$C100-$C1FF  Woz ACI ROM
$C800-$CFFF  A1-SID I/O (29 registers, addr & $1F, when plugged)
$CC00/$CC01  TMS9918 DATA / CTRL (when plugged — overrides A1-SID at those two addresses)
$D010        KBD - Keyboard data        (aliases: $D0F0, $D030, ...)
$D011        KBDCR - Keyboard control   (aliases: $D0F1, $D031, ...)
$D012        DSP - Display              (aliases: $D0F2, $D032, ...)
$E000-$EFFF  Apple BASIC ROM (4 KB)
$FF00-$FFFF  Woz Monitor ROM (256 B) + vectors (NMI/Reset/IRQ at $FFFA-$FFFF)
```

## Platform notes

- **CMake** — `find_package(glfw3 CONFIG)` first (vcpkg / Homebrew), falls back to `pkg_check_modules` (apt/dnf/pacman).
- **Windows** — needs Visual Studio C++ workload + CMake + Git + vcpkg. MSVC flags: `/utf-8`, `_CRT_SECURE_NO_WARNINGS`. `package_windows_release.bat` builds a standalone release archive (DLLs, ROMs, software, fonts, sdcard, cassettes, docs).
- **macOS** — links Cocoa, IOKit, CoreVideo. `GLFW_OPENGL_FORWARD_COMPAT` is set only on macOS.
- **Linux** — `setup_imgui.sh` supports apt, dnf, and pacman.

`build/`, `build-wasm/`, and `imgui/` are gitignored.

## Version string locations

When bumping the version number, update **all** of these:
- `main_imgui.cpp` — console output and GLFW window title
- `MainWindow_ImGui.cpp` — About dialog
- `Screen_ImGui.cpp` — Apple 1 welcome screen
- `build-wasm/shell.html` — HTML `<title>` and `<h1>` banner (2 occurrences)
- `README.md` — title and intro
