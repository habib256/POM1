<div align="center">

# 🍎 POM1 v1.7 — Apple 1 Emulator

**Experience the machine that started the personal computer revolution.**

🎂 **Celebrating 50 years of Apple (1976–2026)** — POM1 v1.7 is released in honor of the 50th anniversary of Apple Computer, founded on April 1, 1976.

A faithful Apple 1 emulator built with Dear ImGui & OpenGL — fast, lightweight, and cross-platform. 
Now with [P-LAB Wi-Fi Modem](https://p-l4b.github.io/wifi/), [P-LAB Terminal Card](https://p-l4b.github.io/terminal/), [P-LAB microSD Storage Card](https://p-l4b.github.io/sdcard/), [P-LAB A1-SID Sound Card](https://p-l4b.github.io/A1-SID/), [P-LAB Apple-1 Graphic Card (TMS9918)](https://p-l4b.github.io/graphic/), [P-LAB I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/), **CFFA1** CompactFlash storage (Rich Dreher design, ProDOS `.po` disk), **Applesoft Lite** (CFFA1 or P-LAB microSD layout), and [Uncle Bernie's GEN2 Color Graphics Card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) support.

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

🖥️ **Authentic Apple 1 Display** — 40×24 character grid, `charmap.rom` bitmap or host ASCII, green / brown / monochrome CRT with scanlines and glow, blinking `@` cursor

⚙️ **Cycle-Accurate 6502 CPU** — All official opcodes, all addressing modes, adjustable clock (1 MHz / 2 MHz / Max)

🔍 **Live Memory Editor** — Interactive hex viewer with color-coded regions, search, bookmarks, inline double-click editing, and undo/redo

🗺️ **Visual Memory Map** — Color-coded 64 KB overview with region legend, PC/SP indicators, and tooltips

📂 **Program Loader** — Load binary files or Woz Monitor hex dumps (with inline comment support) via a built-in file browser

📼 **Apple Cassette Interface (ACI)** — Woz ACI ROM at `$C100`, real-time audio (desktop & WebAssembly), tape import/export as `.aci` or `.wav` — see [Cassette Interface](#-cassette-interface)

🔌 **PIA 6821 Address Aliasing** — `$D0Fx` aliases enable both Pagetable and Briel/Replica 1 BASIC variants

🐛 **Step Debugger** — Single-step execution, register inspection, disassembly, stack view, log console

💾 **Memory Save/Export** — Save any memory range as binary or Woz Monitor hex dump

🎨 **GEN2 Color Graphics Card** — [Uncle Bernie's HIRES card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) — 280×192 NTSC artifact color — see [GEN2](#-gen2-color-graphics-card)

🖥️ **P-LAB Graphic Card (TMS9918)** — [P-LAB TMS9918](https://p-l4b.github.io/graphic/) — 256×192, 15 colors, sprites, 4 modes — see [P-LAB Graphic Card](#-p-lab-graphic-card-tms9918)

🎵 **P-LAB A1-SID Sound Card** — [P-LAB A1-SID](https://p-l4b.github.io/A1-SID/) — MOS 6581/8580-style synthesis with C64 `.sid` converter — see [A1-SID](#-p-lab-a1-sid-sound-card)

💾 **P-LAB microSD Storage Card** — [P-LAB microSD](https://p-l4b.github.io/sdcard/) — virtual FAT32 SD card mapped to host `sdcard/` directory — see [microSD](#-p-lab-microsd-storage-card)

💽 **CFFA1 CompactFlash Card** — Emulates the **CFFA1** interface (8 KB firmware ROM at `$9000`–`$AFDF`, ATA registers at `$AFE0`–`$AFFF`) with a host **ProDOS `.po`** disk image — mutually exclusive with the microSD card — see [CFFA1](#cffa1-compactflash-card)

⏰ **P-LAB I/O Board & RTC** — [P-LAB A1-IO](https://p-l4b.github.io/A1-IO_RTC/) — 65C22 VIA at `$2000`–`$200F` bridging a DS3231 real-time clock, temperature, ADC, and digital I/O — see [I/O Board & RTC](#p-lab-io-board--rtc)

📡 **P-LAB MODEM BBS** — [P-LAB Wi-Fi Modem](https://p-l4b.github.io/wifi/) — Hayes AT commands, TCP/TELNET to real BBS servers — see [MODEM BBS](#-p-lab-modem-bbs)

🖥️ **P-LAB Terminal Card** — [P-LAB Terminal Card](https://p-l4b.github.io/terminal/) — control the Apple 1 from any external terminal via `telnet localhost 6502` — see [Terminal Card](#-p-lab-terminal-card)

🖥️ **Machine Presets** — One-click hardware configurations (Woz, Replica 1, Replica 1 + CFFA1, Uncle Bernie's, P-LAB, POM1) — see [Machine Presets](#-machine-presets)

📋 **Clipboard Paste** — Paste code directly into the Apple 1 keyboard

🎮 **30+ Programs Included** — Games, demos, BASIC programs, dev tools, and expansion demos — many ready from `software/`

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

**Play directly in your browser:** [https://habib256.github.io/POM1/build-wasm/pom1_imgui.html](https://habib256.github.io/POM1/build-wasm/pom1_imgui.html)

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

## 📼 Cassette Interface

The emulator now includes the **Apple Cassette Interface (ACI)**:

- start the cassette monitor with `C100R`
- load a tape image from **File > Load Tape**
- export the last captured cassette signal from **File > Save Tape**
- use `.aci` for exact pulse timings or `.wav` for an audio waveform

This enables software that relies on the ACI output flip-flop, including sound demos such as **Twinkle Twinkle Little Star**. Audio works on both desktop (via miniaudio) and in the browser (via Web Audio API).

---

## 🎨 GEN2 Color Graphics Card

POM1 emulates [Uncle Bernie's GEN2 Color Graphics Card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1), a HIRES color graphics card designed for the Apple 1 by Uncle Bernie (AppleFritter community).

- **280×192 resolution** with Apple II-compatible HIRES memory layout at `$2000-$3FFF`
- **NTSC artifact color** — violet, green, blue, orange, white, and black
- **Pixel glow effect** for a CRT-like appearance
- Rendered in a dedicated **GEN2 Apple1 HGR Color Screen** window
- Toggle via **Hardware > GEN2 Graphics Card** or the toolbar button
- A demo HGR image (`software/hgr/GEN2.HGR.BIN`) is auto-loaded when the card is plugged in
- Includes **HGR Maze** — a Recursive Backtracker maze generator rendering directly into the framebuffer ([asm](software/hgr/HGR1_Maze.asm))

---

## 🖥️ P-LAB Graphic Card (TMS9918)

POM1 emulates the [P-LAB Apple-1 Graphic Card](https://p-l4b.github.io/graphic/), a TMS9918A Video Display Processor expansion for the Apple 1. The Apple 1 now has **two graphics cards**!

- **256×192 resolution**, 15 colors + transparent, 32 hardware sprites
- **4 display modes**: Graphics I (32×24 tiles), Graphics II (full bitmap), Text (40×24), Multicolor (64×48 blocks)
- I/O at `$CC00` (data) / `$CC01` (control/status), 16 KB dedicated VRAM
- Toggle via **Hardware > P-LAB Graphic Card (TMS9918)** or the toolbar button
- Compatible with [nippur72's apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib) (KickC library)

### Bundled P-LAB software (`software/tms9918/`)

| Program | Description |
|---------|-------------|
| 🎮 **Tetris** | Classic falling-blocks game with TMS9918 graphics |
| 🖥️ **Demo** | TMS9918 demo suite — Screen 1 text, Screen 2 bitmap, sprites, interrupt test |
| 🖼️ **PicShow** | Bitmap image viewer for TMS9918 |

Load via **File > Load Memory**, select a `.bin` file — default load address is `$0280`.

---

## 🎵 P-LAB A1-SID Sound Card

POM1 emulates the [P-LAB A1-SID Sound Card](https://p-l4b.github.io/A1-SID/), a MOS 6581/8580 SID expansion for the Apple 1.

- **I/O** at `$C800`–`$CFFF` (29 registers, effective address `& 0x1F`)
- **3 voices** with waveforms, ADSR, and filter; audio is mixed in **AudioDevice** with the cassette path
- Toggle via **Hardware > P-LAB A1-SID Sound Card** or the toolbar
- Coexists with the **P-LAB TMS9918** card (SID at `$C8xx`, VDP at `$CC00` / `$CC01`)

### SID converter

[`tools/sid2apple1.py`](tools/sid2apple1.py) converts **PSID/RSID** files into Apple 1 **`.bin`** images intended for load address **`$0280`** (after loading, run **`280R`** in the Woz Monitor). Recent converter capabilities:

| Feature | Description |
|--------|-------------|
| **SID remap** | Rewrites SID register accesses from **`$D400`** to **`$C800`**, including indirect pointer setups |
| **C64 hardware** | Neutralizes incompatible touches (e.g. **CIA** timers, **VIC** raster) so many players run on the Apple 1 map |
| **Bootstrap** | Wrapper prints **title/author**, calls **init** / **play**, delay loop tuned for **PAL vs NTSC** timing |
| **IRQ-style players** | Simulated IRQ entry and **RTI** stub when the tune’s player expects interrupt context |
| **`--song N`** | Select sub-tune (**1-based**; default follows the SID’s start song) |
| **`--all-songs`** | Emit one **`.bin`** per sub-tune |
| **`--hex`** | Also write a Woz Monitor **`.txt`** hex dump |
| **`--batch dir outdir`** | Convert every **`.sid`** in a directory |

```bash
# Single tune → Apple 1 binary (default output: <name>.a1sid.bin)
python3 tools/sid2apple1.py Music.sid

# Chosen sub-tune, optional hex dump
python3 tools/sid2apple1.py Music.sid out.bin --song 2 --hex

# All sub-tunes into a folder
python3 tools/sid2apple1.py Music.sid --all-songs ./out_sid/

# Batch folder
python3 tools/sid2apple1.py --batch /path/to/sids/ ./out_bins/
```

**Where to get `.sid` files:** the **[High Voltage SID Collection (HVSC)](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection)** — documented on Exotica — is the canonical large archive of Commodore 64 SID music used worldwide for preservation and playback. It is the usual source when batch-converting tunes for experimentation; always respect copyright and any license terms attached to individual files.

### Bundled SID software (`software/sid/`)

**`software/sid/`** ships **over a dozen `.bin`** tunes ready for the A1-SID — including Hubbard, Tel, Daglish, and Huelsbeck titles (see the folder). Load via **File > Load Memory** at **`$0280`**, then **`280R`**. Assembly sources for **Claudio Parmigiani’s SID PIANO** (QWERTY/AZERTY) are included (`.asm` / Woz-style `.txt`); build with cc65 using `piano.cfg` if you want a playable **`piano.bin`**.

> **Known issue:** **Arkanoid** (Martin Galway) converts but does not play — Galway's multi-ISR raster-split player architecture is not yet fully supported by the converter's static ISR detection. **BMX Kidz** (Hubbard) also fails for similar reasons (computed ISR address).

---

## 💾 P-LAB microSD Storage Card

POM1 emulates the [P-LAB Apple-1 microSD Storage Card](https://p-l4b.github.io/sdcard/), which adds a DOS-like file system interface to the Apple 1 via a 65C22 VIA chip and an ATMEGA microcontroller.

- **I/O** at `$A000`–`$A00F` (16 VIA registers), **SD CARD OS ROM** at `$8000`–`$9FFF` (8 KB EEPROM)
- **Virtual SD card** maps the host `sdcard/` directory (at repo root) as a FAT32 filesystem
- **Tagged filenames** (`NAME#TTAAAA`): `#06` = binary, `#F1` = Integer BASIC, `#F8` = AppleSoft BASIC, `AAAA` = hex load address
- **DOS-like shell** commands: `DIR`, `LS`, `CD`, `LOAD`, `SAVE`, `READ`, `WRITE`, `DEL`, `MKDIR`, `RMDIR`, `PWD`, `MOUNT`
- **Fuzzy filename matching** for LOAD (case-insensitive prefix match)
- Toggle via **Hardware > P-LAB microSD Storage Card** or the toolbar button
- Enter the shell with `8000R` in the Woz Monitor

### Quick start

1. Place files in the `sdcard/` directory (use tagged names like `MYPROG#060300` for a binary loading at `$0300`)
2. Enable the card via **Hardware** menu or toolbar
3. Type `8000R` to enter SD CARD OS
4. Use `DIR` to list files, `LOAD MYPROG` to load, then exit to Woz Monitor and `300R` to run

Firmware source: [apple1-sdcard](https://github.com/nippur72/apple1-sdcard) by Antonino Porcino.

### Applesoft Lite (P-LAB microSD)

When **microSD** is enabled and **CFFA1** is off, presets that use Applesoft load **`applesoft-lite-microsd.rom`** at **`$6000`–`$7FFF`** ([P-LAB APPLESOFT-FT](https://p-l4b.github.io/terminal/APPLESOFT-FT.zip) — Fast Terminal + SD OS 1.2). Integer BASIC stays at **`$E000`**, Woz Monitor at **`$FF00`**. Cold start **`6000R`**, warm start **`6003R`**. You can also manage programs from the SD shell (`8000R`) with **`#F8`** tagged files and **`ASAVE` / `LOAD` / `RUN`**.

---

## 💽 CFFA1 CompactFlash Card

POM1 emulates **Rich Dreher’s CFFA1** CompactFlash interface for the Apple 1: **8 KB firmware ROM** at **`$9000`–`$AFDF`** and **ATA/IDE-style registers** at **`$AFE0`–`$AFFF`**, backed by a host **ProDOS block device** (`.po` disk image). **CFFA1 and P-LAB microSD cannot both be enabled** — the emulator turns one off when you plug the other.

- Toggle via **Hardware > CFFA1 CompactFlash Card** or the toolbar
- With CFFA1 on, **Applesoft Lite** loads from **`applesoft-lite-cffa1.rom`** at **`$E000`–`$FFFF`** (includes Woz Monitor in that layout)
- Enter the firmware menu with **`9006R`** in the Woz Monitor
- Reload firmware or attach a disk image from the CFFA1 window / settings as documented in the app

Reference: CFFA1 manual and firmware notes under `doc/CFFA1_cdromv1.1.zip` in the repo.

---

## ⏰ P-LAB I/O Board & RTC

POM1 emulates the [P-LAB Apple-1 I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/): a **65C22 VIA** at **`$2000`–`$200F`** talking to an emulated **ATMEGA32** + **DS3231** real-time clock.

- **Registers 0–5**: RTC (hour, minute, second, day, month, year)  
- **Register 6**: DS3231 temperature  
- **Additional registers**: ADC and digital inputs (see in-app **Memory Map** tooltips when the card is enabled)
- Toggle via **Hardware > P-LAB I/O Board & RTC** or the toolbar
- **Note:** **`$2000`–`$3FFF`** is also the **GEN2 HGR framebuffer** when the GEN2 card is enabled — avoid enabling both on the same machine unless you know what you are doing

---

## 📡 P-LAB MODEM BBS

POM1 emulates the [P-LAB Apple-1 Wi-Fi Modem](https://p-l4b.github.io/wifi/), a 65C51 ACIA + ESP8266 expansion that connects the Apple 1 to real BBS servers over TCP/TELNET.

- **I/O** at `$B000`–`$B003` (DATA, STATUS, COMMAND, CONTROL registers)
- **Hayes AT commands**: `AT`, `ATDT host:port`, `ATH`, `ATE0`/`ATE1`, `ATI`, `ATZ`
- **TELNET protocol**: IAC negotiation (WILL/WONT/DO/DONT), subnegotiation filtering
- **Baud rate simulation**: 50–19200 baud (W65C51N standard table)
- **Escape sequence**: `+++` with 1-second guard time to return to command mode
- Toggle via **Hardware > P-LAB MODEM BBS** or the toolbar button
- Desktop only (no networking in WASM)

### Connecting to a BBS

1. Enable Wi-Fi Modem and Terminal Card (Hardware menu)
2. Connect an external terminal: `telnet localhost 6502`
3. Load the terminal program: **File > Load Memory** → `software/wifi/terminal.txt`
4. In the Woz Monitor (prompt `\`), type `0280R` to start the ACIA bridge
5. Test the modem: type `AT` — response: `OK`
6. Connect to a BBS: `ATDT BBS.FOZZTEXX.COM:23`
7. Disconnect: wait 1s, type `+++`, wait 1s, then `ATH`

The Terminal Card provides full ANSI rendering in your terminal emulator — colors, cursor positioning, and screen clearing work natively.

---

## 🖥️ P-LAB Terminal Card

POM1 emulates the [P-LAB Apple-1 Terminal Card](https://p-l4b.github.io/terminal/), a passive bidirectional serial bridge that connects the Apple 1 to an external terminal.

- **TCP server** on `localhost:6502` — connect with `telnet localhost 6502` or any terminal emulator
- **Passive device**: eavesdrops on `$D012` display writes, injects keystrokes into `$D010`/`$D011` — no new I/O addresses
- **Native Apple 1 screen** continues working in parallel
- **7-bit mode** (default): CR→CRLF, uppercase conversion (Ctrl-O outgoing, Ctrl-I incoming)
- **8-bit mode** (Ctrl-T): raw pass-through for PETSCII/extended ASCII
- **Control commands**: Ctrl-L (clear screen), Ctrl-R (reset Apple 1)
- Toggle via **Hardware > P-LAB Terminal Card** or the toolbar button
- Desktop only (no TCP server in WASM)

### Quick start

```bash
# In POM1: enable Terminal Card via Hardware menu
# In another terminal window:
telnet localhost 6502
# You now control the Apple 1 — Woz Monitor, BASIC, everything
```

---

## 🖥️ Machine Presets

**Hardware > Machine Preset** applies a named configuration in one click — enabling the right cards and snapping windows into a sensible default layout.

| Preset | RAM | BASIC | Krusader | microSD | CFFA1 | SID | TMS9918 | GEN2 HGR | I/O & RTC | WiFi | Terminal |
|--------|:---:|:-----:|:--------:|:-------:|:-----:|:---:|:-------:|:--------:|:---------:|:----:|:--------:|
| **Woz Apple 1 (1976)** | 8 KB | Integer | — | — | — | — | — | — | — | — | — |
| **Replica 1 (Briel)** | 32 KB | Integer | ✓ | — | — | — | — | — | — | — | — |
| **Replica 1 + CFFA1** | 32 KB | Applesoft Lite | — | — | ✓ | — | — | — | — | — | ✓ |
| **Uncle Bernie's Apple 1** | 32 KB | Integer | — | — | — | — | — | ✓ | — | — | — |
| **P-LAB Apple 1** | 32 KB | Applesoft Lite | — | ✓ | — | ✓ | ✓ | — | ✓ | ✓ | ✓ |
| **POM1** | 56 KB | Applesoft Lite | — | ✓ | — | ✓ | ✓ | ✓ | — | ✓ | ✓ |

Each preset also repositions windows into a default layout: the Apple 1 screen anchors top-left, graphics cards open to the right, and status panels fill the bottom row. You can drag windows freely after applying a preset.

---

## 🎮 Software Library

The `software/` directory ships with **30+ ready-to-run programs** — load them via **File > Load Memory**.
Most programs are sourced from [apple1software.com](https://apple1software.com/), the reference archive for Apple 1 software.
Some programs also include their 6502 assembly source code (`.asm`) for study and modification.

### 🕹️ Games

| Program | Description |
|---------|-------------|
| ♟️ **Microchess** | Peter Jennings' chess engine — the first commercial microcomputer game |
| 🏰 **LittleTower** | Text adventure — explore a tower, defeat a vampire ([asm](software/games/LittleTower-1.0.asm)) |
| 🌙 **Lunar Lander** | Pilot your lander safely to the surface |
| 🔢 **2048** | Sliding tile puzzle |
| 🔐 **Codebreaker** | Code-breaking logic game |
| 🧠 **Mastermind** | Classic code-breaking board game |
| 📝 **Worple** | Word guessing game |
| 🧩 **15-Puzzle** | Sliding number puzzle |
| 🔵 **Peg Solitaire** | Board peg-jumping game |
| 🎲 **Shut the Box** | Dice and tile game |

### 🎨 Demos

| Program | Description |
|---------|-------------|
| 🧬 **Game of Life** | Conway's cellular automaton |
| 🌀 **Maze** | Sidewinder maze generator with title screen ([asm](software/games/Maze_Sidewinder.asm)) |
| 🌀 **Maze 2** | Recursive Backtracker (DFS) maze generator ([asm](software/games/Maze2_Backtracker.asm)) |
| 🌌 **Mandelbrot** | Mandelbrot fractal renderer |
| 📊 **Cellular** | 1D cellular automaton |
| 🎂 **30th** | Apple 1 30th anniversary demo |
| 🎨 **PasArt** | Parametric ASCII art generator |
| 🍺 **99 Bottles of Beer** | Classic song countdown demo |
| 🐱 **ASCII Cat** | ASCII art display |
| 🎨 **HGR Maze** | GEN2 HIRES maze generator — Recursive Backtracker on 280×192 ([asm](software/hgr/HGR1_Maze.asm)) |

### 💻 BASIC Programs

*Require loading Enhanced BASIC first (E000R).*

| Program | Description |
|---------|-------------|
| 🚀 **Star Trek** | Mini Star Trek strategy game |
| 🃏 **Blackjack** | Classic card game |
| 🌙 **Lunar Lander (Graphics)** | Lunar Lander with ASCII graphics |
| 🏛️ **Hamurabi** | Rule ancient Sumeria — classic strategy game |
| 🎯 **Dobble** | Spot-it card matching game |
| ⏱️ **Stopwatch** | Real-time clock and stopwatch |
| 🔧 **Resistor Calculator** | 4-band resistor color code calculator |

### 🛠️ Dev Tools

| Program | Description |
|---------|-------------|
| 👁️ **Woz Monitor** | Steve Wozniak's original system monitor |
| 💻 **Enhanced BASIC** | Extended BASIC with extra commands |
| 📘 **fig-FORTH** | FORTH language interpreter |
| 🔬 **Disassembler** | 6502 disassembler |
| 🔨 **A1 Assembler** | Apple 1 in-memory assembler |

### 🧰 Utilities

| Program | Description |
|---------|-------------|
| ✍️ **Typewriter** | Text input and display tool |
| 🎉 **Party** | Guest check-in management tool |

### 🎵 SID music (A1-SID, `software/sid/`)

Binary tunes for the [P-LAB A1-SID](https://p-l4b.github.io/A1-SID/) — load at **`$0280`**, enable the SID card, then **`280R`**. More tunes can be produced from **`.sid`** files using [`tools/sid2apple1.py`](tools/sid2apple1.py) (see [SID converter](#sid-converter)); source material is commonly drawn from the [HVSC](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection) archive.

---

## 🔧 Assembling Your Own Programs

POM1 includes linker configs for [cc65](https://cc65.github.io/):

```bash
# Standard Apple 1 program
ca65 -o build/program.o source.asm
ld65 -C software/apple1.cfg -o build/program.bin build/program.o

# GEN2 graphics program (reserves $2000-$3FFF for HGR framebuffer)
ca65 -o build/program.o source.asm
ld65 -C software/hgr/apple1_gen2.cfg -o build/program.bin build/program.o
```

Load the binary via **File > Load Memory**, or type the start address + `R` in the Woz Monitor (e.g. `280R`).

---

## 🗂️ Project Layout

```
POM1/
├── M6502.cpp/h              # 🧠 MOS 6502 CPU — all opcodes, cycle counting
├── Memory.cpp/h             # 💾 64 KB address space, ROM loader, PIA I/O
├── main_imgui.cpp           # 🪟 GLFW/OpenGL bootstrap
├── MainWindow_ImGui.cpp/h   # 🎛️ App window, menus, CPU speed control
├── Screen_ImGui.cpp/h       # 🖥️ Apple 1 display (40×24, CRT effects)
├── GraphicsCard.cpp/h       # 🎨 GEN2 color graphics card (280×192 HIRES)
├── TMS9918.cpp/h            # 🖥️ P-LAB TMS9918 VDP (256×192, 15 colors, sprites)
├── SID.cpp/h                # 🎵 P-LAB A1-SID (6581/8580-style synthesis)
├── MicroSD.cpp/h            # 💾 P-LAB microSD Storage Card (65C22 VIA + MCU)
├── WiFiModem.cpp/h          # 📡 P-LAB Wi-Fi Modem (65C51 ACIA + TCP/TELNET)
├── TerminalCard.cpp/h       # 🖥️ P-LAB Terminal Card (TCP server, serial bridge)
├── A1IO_RTC.cpp/h           # ⏰ P-LAB I/O Board & RTC (65C22 VIA + DS3231)
├── CFFA1.cpp/h              # 💽 CFFA1 CompactFlash (ROM + ProDOS .po)
├── MemoryViewer_ImGui.cpp/h # 🔍 Hex editor with search & navigation
├── tools/
│   └── sid2apple1.py        # 🎛️ C64 PSID/RSID → Apple 1 .bin for A1-SID
├── roms/                    # 📀 WozMonitor, BASIC, Krusader, ACI, SD CARD OS, charmap
├── sdcard/                  # 💾 Virtual SD card content (host directory)
├── software/                # 📂 Hex dump programs + assembly sources
│   ├── games/               #   🎮 Games
│   ├── demos/               #   🎨 Demos
│   ├── basic/               #   💻 BASIC programs
│   ├── dev/                 #   🛠️ Dev tools
│   ├── utils/               #   🧰 Utilities
│   ├── hgr/                 #   🎨 GEN2 HGR images & programs
│   ├── tms9918/             #   🖥️ P-LAB TMS9918 programs (Tetris, demos)
│   ├── sid/                 #   🎵 A1-SID music (.bin)
│   ├── wifi/                #   📡 Wi-Fi Modem terminal program
│   └── tests/               #   🧪 Hardware test programs
├── cassettes/               # 📼 Original-tape .ogg captures (reference/preservation)
├── build-wasm/              # 🌐 WebAssembly build output
├── software/apple1.cfg      # ⚙️ cc65 linker config
├── setup_imgui.sh           # 📦 One-shot setup script
└── run_emulator.sh          # 🚀 Build check + ROM copy + launch
```

---

## 📀 ROMs

| ROM | Size | Address | Origin |
|-----|------|---------|--------|
| 📼 **ACI** | 256 B | `$C100` | Woz Apple Cassette Interface monitor |
| 👁️ **Woz Monitor** | 256 B | `$FF00` | Steve Wozniak's original system monitor |
| 💻 **Apple BASIC** | 4 KB | `$E000` | Integer BASIC interpreter |
| 💿 **Applesoft Lite (CFFA1)** | 8 KB | `$E000`–`$FFFF` | Applesoft + Woz Monitor in CFFA1 memory map (`applesoft-lite-cffa1.rom`) |
| 💿 **Applesoft Lite (microSD)** | 8 KB | `$6000`–`$7FFF` | P-LAB Fast Terminal / SD OS build (`applesoft-lite-microsd.rom`) |
| 🔧 **Krusader 1.3** | 8 KB | `$A000` | Ken Wessen's symbolic assembler (Replica 1 preset — reload via **Settings** on other configs) |
| 💾 **SD CARD OS** | 8 KB | `$8000` | P-LAB microSD Storage Card firmware ([apple1-sdcard](https://github.com/nippur72/apple1-sdcard)) |
| 💽 **CFFA1 firmware** | ~8 KB | `$9000`–`$AFDF` | CFFA1 card ROM (`cffa1.rom`) |
| 🔤 **Charmap** | 1 KB | — | Character generator table used by the terminal renderer |

The main firmware ROMs (Woz Monitor, Integer BASIC, ACI) load at startup. **SD CARD OS** loads when the microSD card is enabled; **CFFA1** loads **`cffa1.rom`** and the matching **Applesoft Lite** image when that card is enabled. **Applesoft Lite (microSD)** loads at **`$6000`–`$7FFF`** only with microSD on and CFFA1 off. Krusader loads with the **Replica 1** preset or via **Settings > Reload Krusader** when `$A000` is not used by the microSD VIA.

---

## 🗺️ Memory Map

```
$0000-$00FF   Zero Page
$0100-$01FF   Stack
$0200-$1FFF   User RAM (programs load at $0280 or $0300)
$2000-$200F   P-LAB I/O Board & RTC — VIA 65C22 (when card is plugged; overlaps GEN2 HGR page)
$2000-$3FFF   GEN2 HGR Framebuffer (8 KB — when GEN2 card is plugged)
$4000-$7FFF   User RAM
$6000-$7FFF   Applesoft Lite ROM (8 KB — P-LAB microSD + Applesoft layout only)
$8000-$9FFF   SD CARD OS ROM (8 KB — when P-LAB microSD is plugged)
$9000-$AFDF   CFFA1 firmware ROM (when CFFA1 is plugged)
$AFE0-$AFFF   CFFA1 ATA/IDE registers (when CFFA1 is plugged)
$A000-$A00F   VIA 65C22 I/O (16 registers — when P-LAB microSD is plugged)
$A010-$AFFF   User RAM (Krusader may use $A000-$BFFF when microSD is off and it is loaded)
$B000-$B003   ACIA 65C51 — MODEM BBS I/O (when P-LAB modem is plugged)
$B004-$BFFF   User RAM
$C000-$C0FF   Apple Cassette Interface I/O
$C081         Tape input
$C100-$C1FF   Woz ACI ROM
$C800-$CFFF   A1-SID — SID registers (when P-LAB SID card is plugged; addr & $1F)
$CC00         TMS9918 DATA — VRAM data port (when P-LAB card is plugged)
$CC01         TMS9918 CTRL — Control/status  (when P-LAB card is plugged)
$D010-$D012   PIA 6821 — Keyboard (KBD) & Display (DSP)  (aliases: $D0Fx)
$E000-$EFFF   Integer BASIC ROM (4 KB) — or Applesoft Lite region with CFFA1 layout
$FF00-$FFFF   Woz Monitor ROM (256 B)
```

The **POM1** machine preset (**56 KB** RAM) treats **`$0000`–`$DFFF`** as backed by RAM in the emulator (overlays from expansion cards and ROMs still win where decoded). Open the in-app **Memory Map** for the live layout.

---

## 👏 Credits

- **Arnaud Verhille** — Original POM1 (Java, 2000) & Dear ImGui port (2026)
- **Ken Wessen** — Upgrades, 65C02 support (2006)
- **Joe Crobak** — macOS Cocoa port
- **John D. Corrado** — C/SDL port (2006–2014)
- **Lee Davison** — Enhanced BASIC
- **Achim Breidenbach** — Sim6502
- **Fabrice Frances** — Java Microtan Emulator
- **Uncle Bernie** — [GEN2 Color Graphics Card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) for Apple 1
- **Claudio Parmigiani ([P-LAB](https://p-l4b.github.io/))** — designer of the entire P-LAB Apple-1 expansion family: [microSD Storage Card](https://p-l4b.github.io/sdcard/), [A1-SID Sound Card](https://p-l4b.github.io/A1-SID/), [Apple-1 Graphic Card](https://p-l4b.github.io/graphic/) (TMS9918 VDP), [I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/), [Terminal Card](https://p-l4b.github.io/terminal/), [MODEM BBS / Wi-Fi Modem](https://p-l4b.github.io/wifi/)
- **Rich Dreher** — **CFFA1** CompactFlash interface for Apple 1 (firmware / hardware design emulated here)
- **Nippur72 (Antonino Porcino)** — [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib) (KickC library, Tetris, demos for the P-LAB Graphic Card), [apple1-sdcard](https://github.com/nippur72/apple1-sdcard) (microSD firmware)
- **Tom Owad** — AppleFritter community & Apple 1 resources
- **Steve Wozniak & Steve Jobs** — For creating the Apple 1 🍎

## 🔗 Resources

- [**apple1software.com**](https://apple1software.com/) — The definitive Apple 1 software archive. Meticulously curated collection of programs, hardware documentation, schematics, and historical research. Most of the software included in POM1 comes from this outstanding resource. An invaluable reference for anyone interested in the Apple 1.
- [**AppleFritter**](https://applefritter.com/apple1/) — The heart of the Apple 1 community. Home to decades of technical discussions, hardware projects, BASIC version research, and first-hand accounts from original Apple 1 owners and builders. Many of the programs, patches, and discoveries documented here have directly shaped this emulator.
- [**Uncle Bernie's GEN2 Color Graphics Card**](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) — The original hardware project by Uncle Bernie on AppleFritter. A 280×192 HIRES color graphics card for the Apple 1 using Apple II-compatible memory layout and NTSC artifact color encoding.
- [**P-LAB Apple-1 Graphic Card**](https://p-l4b.github.io/graphic/) — TMS9918 VDP expansion card for the Apple 1. Schematics, documentation, and CodeTank daughterboard.
- [**apple1-videocard-lib**](https://github.com/nippur72/apple1-videocard-lib) — KickC C library and demos (Tetris, image viewer, etc.) for the P-LAB Graphic Card.
- [**P-LAB Apple-1 microSD Storage Card**](https://p-l4b.github.io/sdcard/) — SD card storage expansion. 65C22 VIA bridge, ATMEGA MCU, FAT32 filesystem. Firmware: [apple1-sdcard](https://github.com/nippur72/apple1-sdcard).
- [**P-LAB Apple-1 Wi-Fi Modem**](https://p-l4b.github.io/wifi/) — 65C51 ACIA serial modem with ESP8266 Wi-Fi. Hayes AT commands, TCP/TELNET for BBS connections.
- [**P-LAB Apple-1 I/O Board & RTC**](https://p-l4b.github.io/A1-IO_RTC/) — 65C22 VIA, DS3231 RTC, ADC, and digital I/O expansion.
- [**P-LAB Apple-1 Terminal Card**](https://p-l4b.github.io/terminal/) — USB serial terminal replacing native keyboard/display. ANSI pass-through, 8-bit support, fast terminal mode.
- [**High Voltage SID Collection (HVSC)**](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection) — Exotica wiki page for HVSC, the major archive of Commodore 64 SID tunes; use with [`tools/sid2apple1.py`](tools/sid2apple1.py) to build Apple 1 binaries for the A1-SID (see [P-LAB A1-SID Sound Card](#-p-lab-a1-sid-sound-card)).
- [POM1 Project Page](https://www.gistlabs.net/Apple1project/)

---

## 📄 License

GPL-3.0 — see [LICENSE](LICENSE)

<div align="center">

*Made with ❤️ for the Apple 1 community*

</div>
