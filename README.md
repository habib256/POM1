<div align="center">

# 🍎 POM1 v1.8.6 — Apple 1 Emulator

**Experience the machine that started the personal computer revolution.**

🎂 **Celebrating 50 years of Apple (1976–2026)** — cycle-accurate **libresidfp** SID engine with hot-swappable 6581/8580 chip variants, TTL-faithful keyboard (no autorepeat by default), and a full stack of expansion cards from 1976 through 2026.

Built with Dear ImGui & OpenGL — fast, lightweight, and cross-platform (Linux, macOS, Windows, Web).

**Play it now in your browser:** 
[![Play Online](https://img.shields.io/badge/Play%20Online-WebAssembly-blueviolet.svg)](https://habib256.github.io/POM1/build-wasm/POM1.html)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows%20%7C%20Web-lightgrey.svg)](#-quick-start)
[![C++](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](#)

![POM1 Screenshot](doc/screenshot.png)

</div>

---

## ✨ Features

**Core machine**
- 🖥️ Authentic **40×24 display** — `charmap.rom` bitmap or host ASCII, green / amber / monochrome phosphor, CRT scanlines + glow, blinking `@` cursor
- ⚙️ Cycle-accurate **6502** — all official opcodes & addressing modes, adjustable clock (~1.022727 MHz nominal / 2× / MAX)
- ⌨️ Paste-code, load/save memory, step debugger with register inspection, disassembly, stack view
- 🔍 Live **memory editor** + visual **memory map** (PC / SP indicators, I/O tooltips)

**Expansion cards**
- 📼 **ACI cassette** with procedural deck widget — `.aci` / `.wav` / `.mp3` / `.ogg`, smart jaquette from `tapeinfo.txt`
- 🎨 Three graphics cards — [Uncle Bernie's GEN2](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) (280×192 NTSC artifact colour), [P-LAB TMS9918](https://p-l4b.github.io/graphic/) (256×192 + sprites), and **SWTPC GT-6144** (1976, 64×96 mono — *first commercial Apple-1 graphics card*)
- 🖨️ **SWTPC PR-40** 40-column printer (Jobs' Oct-76 *Interface Age* mod) — 40-char FIFO, ~0.8 s mechanical cycle, scrollable paper roll
- 🎵 [P-LAB A1-SID](https://p-l4b.github.io/A1-SID/) — libresidfp 6581/8580, C64 `.sid` converter
- 💾 Storage — [P-LAB microSD](https://p-l4b.github.io/sdcard/) (virtual FAT32), **CFFA1** CompactFlash (ProDOS `.po`), [P-LAB Juke-Box](https://p-l4b.github.io/) (32 KB EEPROM library)
- 📡 Networking — [P-LAB Wi-Fi Modem](https://p-l4b.github.io/wifi/) (Hayes AT + TCP/TELNET), [P-LAB Terminal Card](https://p-l4b.github.io/terminal/) (`telnet localhost 6502`)
- ⏰ [P-LAB I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/) — DS3231 + ADC + digital I/O

**Out of the box**
- 🖥️ **15 one-click machine presets** from *Bare Apple-1 (July 1976)* to *POM1 Multiplexing Fantasy (2026)*
- 🎮 **60+ programs** shipped in `software/` — games, demos, BASIC, dev tools, A1-SID tunes, TMS9918 demos

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

Prerequisites: [Visual Studio](https://visualstudio.microsoft.com/) (C++ workload), [CMake](https://cmake.org/download/), [Git](https://git-scm.com/download/win), [vcpkg](https://vcpkg.io/).

```batch
git clone https://github.com/gistarcade/POM1.git
cd POM1
setup_imgui.bat                     REM fetch Dear ImGui + install GLFW via vcpkg
cd build
cmake --build . --config Release
cd ..
run_emulator.bat                    REM copies ROMs & launches the emulator
```

### 🌐 WebAssembly

**Play directly:** [https://habib256.github.io/POM1/build-wasm/POM1.html](https://habib256.github.io/POM1/build-wasm/POM1.html)

Build it yourself:

```bash
# One-time Emscripten install
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh && cd ..

# Build + serve locally
cd POM1 && mkdir -p build-wasm && cd build-wasm
emcmake cmake .. && emmake make -j$(nproc)
emrun POM1.html
```

### 🎛️ Command-line flags

`POM1` takes a small set of flags — useful for headless / scripted runs (see `tools/test_*_telnet.py`):

| Flag | Effect |
|------|--------|
| `--list-presets` | Print every preset as `index: name` and exit. |
| `--preset <N\|name>` / `-p` | Select preset by index or case-insensitive substring (first match wins). |
| `--terminal` | Force-enable the Terminal Card (`127.0.0.1:6502`). Desktop-only. |
| `--tape <path>` | Preload a `.aci`/`.wav`/`.mp3`/`.ogg` and **auto-press Play**. Default: `cassettes/WOZ_talk.mp3` (silent, press Play). |
| `--save-tape <path>` | Dump the cassette recording on clean shutdown (also triggered by `SIGINT`/`SIGTERM`). |
| `--cpu-max` | Pin CPU to 1 000 000 cycles / frame on boot (MAX button). |

```bash
./POM1 --list-presets
./POM1 --preset 11 --terminal &    # Juke-Box + Terminal on :6502
python3 tools/test_jukebox_telnet.py
./POM1 --preset "A1-SID" --terminal
./POM1 --preset 2 --terminal --save-tape /tmp/out.wav --cpu-max &
```

### 📦 Manual dependency install

<details><summary>Ubuntu / Debian</summary>

```bash
sudo apt install cmake libglfw3-dev libgl1-mesa-dev pkg-config
```
</details>

<details><summary>Fedora</summary>

```bash
sudo dnf install cmake glfw-devel mesa-libGL-devel pkgconf
```
</details>

<details><summary>Arch</summary>

```bash
sudo pacman -S cmake glfw mesa pkgconf
```
</details>

<details><summary>macOS</summary>

```bash
brew install cmake glfw pkg-config
```
</details>

<details><summary>Windows (vcpkg)</summary>

```batch
vcpkg install glfw3:x64-windows
```
</details>

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action | Shortcut | Action |
|----------|--------|----------|--------|
| `F1` | Memory Viewer | `Ctrl+O` | Load program |
| `F2` | Memory Map | `Ctrl+S` | Save memory |
| `F3` | Debug Console | `Ctrl+V` | Paste code |
| `F5` | Soft Reset | `Ctrl+Q` | Quit |
| `Ctrl+F5` | Hard Reset | `F7` | Single-step |
| `F6` | Start / Stop CPU | | |

---

## 📼 Cassette Interface

**Apple Cassette Interface (ACI)** — Woz ACI ROM at `$C100`, real-time audio on desktop & WebAssembly.

- Load `.aci` (raw pulse capture), `.wav`, `.mp3`, or `.ogg` via **File > Load Tape**; export to `.aci` or `.wav`
- Drives software that relies on the ACI output flip-flop, including **Twinkle Twinkle Little Star**
- Start the cassette monitor with `C100R`

**Procedural Cassette Deck** — realistic widget on top of the ACI:

- Piano-key transport with real interlocks (REC alone = REC+PLAY, PAUSE only latches on Play/Rec, STOP releases everything)
- Mechanical counter 000–999, spinning hubs, integrated volume knob, Apple 50th-anniversary label
- **Smart jaquette** — add `filename = load-range` to `cassettes/tapeinfo.txt` (e.g. `APPLE50TH.ogg = 0280.0FFF`) and the label shows *"Type 0280.0FFFR"* so you know exactly what to type into Wozmon
- Default tape: `cassettes/WOZ_talk.mp3` (Woz speaking) — press **Play** to hear him

---

## 🎨 Graphics Cards

POM1 emulates three independent Apple 1 graphics expansion cards. Enable from the **Hardware** menu or the toolbar (respect each card's bus-window exclusions — see the in-app Hardware Reference).

### SWTPC GT-6144 Graphic Terminal *(1976)*

Southwest Technical Products' GT-6144 — the **first commercial graphics card for the Apple 1** ($98.50), demoed by Woz in *Interface Age*.

- **64×96** monochrome framebuffer on 6× Intel 2102 SRAM, write-only I/O at `$D00A` (PIA A3 chip-select)
- 4-phase command protocol over a single port; visible SRAM power-on noise (bistable "petits rectangles") on every plug-in
- No bus overlap with other POM1 peripherals — composes freely
- `software/gt-6144/` ships Game-of-Life + demos

### Uncle Bernie's GEN2 Color Graphics Card

[Uncle Bernie's HIRES card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) (AppleFritter community).

- **280×192** HIRES with Apple II-compatible memory layout at `$2000-$3FFF`
- **NTSC artifact color** — violet, green, blue, orange, white, black — plus pixel-glow effect
- Demo `software/hgr/GEN2.HGR.BIN` auto-loads; includes **HGR Maze** (Recursive Backtracker — [asm](software/hgr/HGR1_Maze.asm))

### P-LAB Graphic Card (TMS9918)

[P-LAB Apple-1 Graphic Card](https://p-l4b.github.io/graphic/) — TMS9918A VDP.

- **256×192**, 15 colors + transparent, 32 hardware sprites, 4 display modes
- I/O at `$CC00` (data) / `$CC01` (control/status), 16 KB dedicated VRAM
- Compatible with [nippur72's apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib)
- Bundled (`software/tms9918/`): **Tetris**, TMS9918 **Demo** suite (text / bitmap / sprites / IRQ), **PicShow** image viewer — load via **File > Load Memory**, default `$0280`

---

## 🎵 P-LAB A1-SID Sound Card

[P-LAB A1-SID](https://p-l4b.github.io/A1-SID/), driven by **[libresidfp](https://github.com/libsidplayfp/libsidplayfp)** (vendored under `third_party/libresidfp/`).

- **I/O** `$C800-$CFFF` (29 registers, effective addr `& 0x1F`)
- **Hot-swappable chip model** — **Hardware > A1-SID chip model** toggles MOS 6581 (vintage, non-linear filter) ↔ CSG 8580 (cleaner revision); live-swap restores the register state so the tune keeps playing
- **Cycle-driven** — samples clock at the emulated CPU rate (1 022 727 Hz) into a lock-free SPSC ring; tempo follows emulation speed exactly
- Coexists with **P-LAB TMS9918** (VDP wins at `$CC00`/`$CC01` via PeripheralBus priority)

### SID converter

[`tools/sid2apple1.py`](tools/sid2apple1.py) turns **PSID/RSID** files into Apple 1 `.bin` images for load address `$0280` (run with `280R`):

| Feature | What it does |
|---------|--------------|
| **SID remap** | Rewrites register accesses `$D400` → `$C800`, including indirect pointer setups |
| **C64 hardware** | Neutralises incompatible touches (CIA timers, VIC raster) |
| **Bootstrap** | Prints title/author, calls init/play, PAL vs NTSC timing loop |
| **IRQ-style players** | Simulated IRQ entry + `RTI` stub for tunes that expect interrupt context |
| `--song N` | Pick a sub-tune (1-based) |
| `--all-songs` | One `.bin` per sub-tune |
| `--hex` | Also emit a Woz Monitor hex dump |
| `--batch dir outdir` | Convert every `.sid` in a directory |

```bash
python3 tools/sid2apple1.py Music.sid                       # -> Music.a1sid.bin
python3 tools/sid2apple1.py Music.sid out.bin --song 2 --hex
python3 tools/sid2apple1.py Music.sid --all-songs ./out_sid/
python3 tools/sid2apple1.py --batch /path/to/sids/ ./out_bins/
```

Source material: the **[High Voltage SID Collection (HVSC)](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection)** is the canonical archive — respect per-file copyright.

`software/sid/` ships a dozen ready-to-run tunes (Hubbard, Tel, Daglish, Huelsbeck…). Load at `$0280`, enable the SID card, then `280R`. Claudio Parmigiani's **SID PIANO** source is included (QWERTY/AZERTY, `.asm` + Woz-style `.txt`, build with `piano.cfg`).

> **Converter limits:** **Arkanoid** (Galway) and **BMX Kidz** (Hubbard) stay silent — Galway's multi-ISR raster-split and Hubbard's computed ISR addresses escape `sid2apple1.py`'s static ISR detection. The SID chip emulation is fine; the converter is the bottleneck.

---

## 💾 P-LAB microSD Storage Card

[P-LAB microSD](https://p-l4b.github.io/sdcard/) — DOS-like file system over a 65C22 VIA + emulated ATMEGA MCU.

- **I/O** `$A000-$A00F`, **SD CARD OS ROM** at `$8000-$9FFF` (firmware: [apple1-sdcard](https://github.com/nippur72/apple1-sdcard))
- Host `sdcard/` directory mounted as virtual FAT32
- **Tagged filenames** `NAME#TTAAAA`: `#06` binary, `#F1` Integer BASIC, `#F8` Applesoft, `AAAA` hex load address
- **Shell commands** — `DIR` / `LS`, `CD <dir>` / `CD ..` (only navigation primitive), `LOAD <name>`, `SAVE`, `READ`, `WRITE`, `DEL <name>`, `MKDIR`, `RMDIR`, `PWD`, `MOUNT`
- `LOAD` uses **fuzzy case-insensitive prefix match** — **within the current directory only, no recursion**. Use `CD <dir>` to navigate before `LOAD` / `DEL` / `SAVE` on a file that lives deeper in the tree
- The **prompt shows the current directory** — `/PLAB>` means `currentDirectory = PLAB`, `/PLAB/MCODE>` means you're one level deeper — no guessing where `LOAD` will look

### Quick start

1. Drop files into `sdcard/` with tagged names (e.g. `MYPROG#060300` = binary at `$0300`), or use the shipped library under `sdcard/PLAB/` — games in `MCODE/`, BASIC in `BASIC/`, Applesoft in `ASOFT/`, …
2. Enable the card (**Hardware** menu or toolbar), type `8000R` — the prompt lands at `/>`
3. `DIR` lists the current dir; for the shipped library type `CD PLAB`, then `CD MCODE`, `DIR`
4. `LOAD YUM` reads at the tagged address, exit to Woz Monitor, `300R` to run
5. **Regression-pinned**: `tools/test_sdcard_subdir_navigation_telnet.py` auto-launches POM1 and exercises the "CD before LOAD / DEL" invariant end-to-end

### Applesoft Lite (P-LAB microSD layout)

With **microSD on + CFFA1 off**, Applesoft-preset machines load `applesoft-lite-microsd.rom` at `$6000-$7FFF` ([P-LAB APPLESOFT-FT](https://p-l4b.github.io/terminal/APPLESOFT-FT.zip), Fast Terminal + SD OS 1.2). Integer BASIC stays at `$E000`, Woz Monitor at `$FF00`. Cold start `6000R`, warm start `6003R`. Manage programs from the SD shell (`8000R`) via `#F8` tagged files and `ASAVE` / `LOAD` / `RUN`.

---

## 💽 CFFA1 CompactFlash Card

**Rich Dreher's CFFA1** — 8 KB firmware ROM at `$9000-$AFDF`, ATA/IDE registers at `$AFE0-$AFFF`, backed by a host **ProDOS `.po`** block device. **Mutually exclusive with microSD** (shared `$9000` region).

- Toggle via **Hardware > CFFA1 CompactFlash Card** or the toolbar
- With CFFA1 on, Applesoft Lite loads from `applesoft-lite-cffa1.rom` at `$E000-$FFFF` (Woz Monitor included)
- Enter firmware menu with `9006R`
- Default disk: `cfcard/cfcard.po` next to the executable (or repo root); the WASM build preloads `cfcard/` like `sdcard/`
- Reference: `doc/CFFA1_cdromv1.1.zip` in the repo

---

## ⏰ P-LAB I/O Board & RTC

[P-LAB Apple-1 I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/) — 65C22 VIA at `$2000-$200F` bridging an emulated ATMEGA32 + DS3231.

- **Registers 0–5**: RTC (hour / minute / second / day / month / year)
- **Register 6**: DS3231 temperature
- Additional registers: ADC + digital inputs — see the in-app Memory Map tooltips
- **⚠ Overlap**: `$2000-$3FFF` is also the GEN2 HGR framebuffer — don't enable both cards on the same machine

---

## 📡 P-LAB MODEM BBS

[P-LAB Wi-Fi Modem](https://p-l4b.github.io/wifi/) — 65C51 ACIA + ESP8266, real BBS servers over TCP/TELNET. Desktop only (no networking in WASM).

- **I/O** `$B000-$B003` (DATA / STATUS / COMMAND / CONTROL)
- **Hayes AT**: `AT`, `ATDT host:port`, `ATH`, `ATE0/ATE1`, `ATI`, `ATZ`; `+++` with 1 s guard
- Baud-rate simulation 50–19200 (W65C51N table); TELNET IAC negotiation (WILL/WONT/DO/DONT)

### Connecting to a BBS

1. Enable **Wi-Fi Modem** + **Terminal Card** (Hardware menu)
2. External terminal: `telnet localhost 6502`
3. Load `software/net/ATmodem.txt` (**File > Load Memory**)
4. From the Wozmon `\` prompt, `0280R` starts the ACIA bridge
5. `AT` → `OK`, `ATDT BBS.FOZZTEXX.COM:23` to connect, `+++` then `ATH` to hang up

Full ANSI rendering (colours, cursor positioning, screen clearing) works natively in the external terminal.

---

## 🖥️ P-LAB Terminal Card

[P-LAB Terminal Card](https://p-l4b.github.io/terminal/) — passive bidirectional serial bridge. Desktop only.

- **TCP server** on `localhost:6502` — `telnet localhost 6502` or any emulator
- Sniffs `$D012` writes, injects keystrokes into `$D010`/`$D011` — no new I/O addresses; native Apple 1 screen keeps working
- **7-bit mode** (default): CR→CRLF, optional uppercase (Ctrl-O/I)
- **8-bit mode** (`Ctrl-T`): raw pass-through for PETSCII / extended ASCII
- **Controls**: `Ctrl-L` clear, `Ctrl-R` reset; ESC-prefixed alternates (ESC T/O/L/R/I) for ttys that eat the raw control chars

```bash
telnet localhost 6502     # after enabling the card — you now drive Wozmon, BASIC, everything
```

---

## 🖨️ SWTPC PR-40 Printer *(Jobs 1976)*

Steve Jobs' October-1976 *Interface Age* hack: tee the SWTPC PR-40 40-column matrix printer off PIA Port B so **every character sent to the Apple 1 display is also printed**.

- **No MMIO** — a third sniffer on `$D012` writes, after the display callback and Terminal Card
- **40-char FIFO**, flushed on `CR` (`$0D`) or when full; each flush arms a ~0.8 s mechanical cycle (`818 182` CPU cycles)
- **DPDT switch** in the Hardware window: *Off* / *Mixed* (Jobs' 2-pos: PB7 = video-busy OR printer-busy, CPU stalls for either) / *PrintOnly* (community 3-pos: PB7 = printer-busy alone; flood the FIFO at 1 MHz)
- Scrollable paper roll with Save-to-`.txt` and tear-off page
- Full-tape invariants pinned by `pr40_printer_smoke` (PB7 wiring, FIFO/CR flush, mechanical stall duration)

---

## 💿 P-LAB Apple-1 Juke-Box

Claudio Parmigiani's [P-LAB Juke-Box](https://p-l4b.github.io/) — memory-mapped 32 KB EEPROM (28c256) acting as an in-address-space program library. No cassette, no SD card.

- **ROM window**: `$4000-$BFFF` (RAM-16 / ROM-32 jumper) or `$8000-$BFFF` (RAM-32 / ROM-16 jumper) — toggle from the Juke-Box window
- **Program Manager** at `$BD00` — `BD00R` → `&` prompt → `H / D / L<X> / P<0-F> / B / X`
- **Save Program** at `$B800` — `B800R` writes current RAM back to the EEPROM (requires RW jumper)
- Mutually exclusive with CFFA1, microSD, Krusader, Wi-Fi Modem (all live inside the Juke-Box address window)
- Integer BASIC at `$E000` stays available — `L<letter>` then `B` loads a BASIC program and hands it to the interpreter
- Firmware: `roms/jukebox.rom`; rebuild with `doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py` (signature byte at file offset `$7D00` must be `$A5`)
- v1 models the single-page 28c256. Multi-page 29c020 / 29c040 (`P0..PF` / `S0..S1`) pending public MMIO documentation.

---

## 🖥️ Machine Presets

**Presets menu** applies a one-click configuration — enabling the right cards and snapping windows into a sensible layout. Indices match `--preset N`.

| # | Preset | RAM | BASIC | Expansion cards |
|:-:|:-------|:---:|:------|:----------------|
| 0 | **Bare Apple-1 (July 1976)** | 4 KB | — | — |
| 1 | **Apple-1 with ACI & Integer BASIC (Oct 1976)** | 8 KB | Integer | ACI |
| 2 | **Apple-1 + SWTPC GT-6144 (1976)** | 8 KB | Integer | ACI, GT-6144 |
| 3 | **Replica-1 with ACI, Krusader (Briel 2003)** | 32 KB | Integer | ACI, Krusader |
| 4 | **Replica-1 with CFFA1 & Applesoft Lite (Dreher 2007)** | 32 KB | Applesoft Lite | CFFA1 |
| 5 | **P-LAB microSD & Applesoft Lite (Apr 2022)** | 32 KB | Applesoft Lite | microSD |
| 6 | **P-LAB A1-SID Sound Card ($C800-$CFFF)** | 32 KB | Integer | A1-SID |
| 7 | **P-LAB A1-AUDIO Special Edition ($CC00-$CC1F)** | 32 KB | Integer | A1-AUDIO SE |
| 8 | **P-LAB TMS9918 Graphic Card** | 32 KB | Integer | TMS9918 |
| 9 | **P-LAB I/O Board & RTC** | 32 KB | Integer | I/O & RTC |
| 10 | **P-LAB Wi-Fi Modem BBS** | 32 KB | Integer | Wi-Fi Modem |
| 11 | **P-LAB Juke-Box (16 kB RAM)** | 16 KB | Integer + Juke-Box | Juke-Box |
| 12 | **P-LAB Multiplexing Fantasy** | 64 KB | Applesoft Lite | microSD, A1-SID, TMS9918, I/O & RTC, Wi-Fi, Terminal, PR-40 |
| 13 | **Uncle Bernie's GEN2 HGR Color (Apr 2026)** | 32 KB | Integer | GEN2 HGR |
| 14 | **POM1 Multiplexing Fantasy (2026)** | 64 KB | Applesoft Lite | microSD, A1-SID, Wi-Fi, Terminal |

- **Bare (0)** — pre-ACI July-1976 shipping configuration (first ~150 units left the bench this way).
- **Juke-Box (11)** — Integer BASIC at `$E000`, EEPROM library via the Program Manager at `$BD00`; ACI dropped (EEPROM replaces cassette).
- **POM1 Fantasy (14)** — **default preset**, shows the POM1 banner on the Apple 1 screen, opens Welcome + Cassette Deck to the right.

Each preset repositions windows into a default layout (Apple 1 Screen top-left, expansion panels to the right, status at the bottom). Drag freely afterwards — per-preset layouts persist under `ini/imgui_preset_NN.ini` (plus an `ini/preset_NN.size` sidecar for the OS window frame), so switching presets saves/restores each profile independently.

---

## 🎮 Software Library

`software/` ships **60+ ready-to-run programs** — load via **File > Load Memory**. Most come from [apple1software.com](https://apple1software.com/), the reference archive. Many include `.asm` source for study / modification.

### 🕹️ Games & demos

| Program | Description |
|---------|-------------|
| ♟️ **Microchess** | Peter Jennings' chess engine — first commercial microcomputer game |
| 🏰 **LittleTower** | Text adventure ([asm](software/games/LittleTower-1.0.asm)) |
| 🌙 **Lunar Lander** | Pilot your lander safely to the surface |
| 🔢 **2048** | Sliding-tile puzzle |
| 🔐 **Codebreaker** / 🧠 **Mastermind** | Code-breaking logic games |
| 📝 **Worple** / 🧩 **15-Puzzle** / 🔵 **Peg Solitaire** / 🎲 **Shut the Box** | Word / sliding / board games |
| 🧬 **Game of Life** | Conway's cellular automaton |
| 🌀 **Maze** / **Maze 2** | Sidewinder ([asm](software/games/Maze_Sidewinder.asm)) and Recursive Backtracker ([asm](software/games/Maze2_Backtracker.asm)) generators |
| 🎨 **HGR Maze** | GEN2 HIRES maze generator — 280×192 ([asm](software/hgr/HGR1_Maze.asm)) |
| 🌌 **Mandelbrot** / 📊 **Cellular** / 🎨 **PasArt** | Fractal, 1D CA, parametric ASCII art |
| 🎂 **30th** / 🐱 **ASCII Cat** / 🍺 **99 Bottles** | Classic demos |

### 💻 BASIC programs *(load Enhanced BASIC first with `E000R`)*

| Program | Description |
|---------|-------------|
| 🚀 **Star Trek** | Mini Star Trek strategy game |
| 🃏 **Blackjack** | Classic card game |
| 🏛️ **Hamurabi** | Rule ancient Sumeria — classic strategy |
| 🌙 **Lunar Lander (Graphics)** / ⏱️ **Stopwatch** / 🔧 **Resistor Calculator** | BASIC utilities |

### 🛠️ Dev tools & utilities

| Program | Description |
|---------|-------------|
| 👁️ **Woz Monitor** | Steve Wozniak's original system monitor |
| 💻 **Enhanced BASIC** | Extended BASIC with extra commands |
| 📘 **fig-FORTH** | FORTH interpreter |
| 🔬 **Disassembler** / 🔨 **A1 Assembler** | 6502 disassembler + in-memory assembler |
| ✍️ **Typewriter** / 🎉 **Party** | Text input & guest check-in |

### 🎵 A1-SID music (`software/sid/`)

Binary tunes for the [P-LAB A1-SID](https://p-l4b.github.io/A1-SID/) — load at `$0280`, enable the SID card, then `280R`. Add your own from the [HVSC](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection) archive via [`tools/sid2apple1.py`](tools/sid2apple1.py) (see [SID converter](#sid-converter)).

---

## 🔧 Assembling Your Own Programs

POM1 ships linker configs for [cc65](https://cc65.github.io/):

```bash
# Standard Apple 1 program
ca65 -o build/program.o source.asm
ld65 -C software/apple1.cfg -o build/program.bin build/program.o

# GEN2 graphics program (reserves $2000-$3FFF for the HGR framebuffer)
ld65 -C software/hgr/apple1_gen2.cfg -o build/program.bin build/program.o
```

Load via **File > Load Memory**, or type the start address + `R` in Wozmon (e.g. `280R`).

---

## 🗂️ Project Layout

```
POM1/
├── M6502.cpp/h              # MOS 6502 CPU — all opcodes, cycle counting
├── CpuClock.h               # 1 022 727 Hz + cycles/frame helpers
├── Memory.cpp/h             # 64 KB, ROM loader, PIA I/O
├── PeripheralBus.cpp/h      # Central I/O dispatch table
├── EmulationController.*    # Emulation thread, step/run/reset, hardware toggles
├── SnapshotPublisher.*      # Lock-free snapshot slot published to the UI
├── KeyboardController.*     # Thread-safe key queue
├── RomLoader.cpp/h          # ROM load/reload helpers
├── Disassembler6502.cpp/h   # Standalone 6502 disassembler
├── Logger.cpp/h             # Leveled logger + UI ring-buffer sink
├── main_imgui.cpp           # GLFW/OpenGL bootstrap
├── MainWindow_*.cpp         # App window, 9 TUs (ImGui / Layout / Presets /
│                            #   Menu / Dialogs / HardwareWindows / FileDialogs /
│                            #   DebugWindows / Keyboard)
├── Screen_ImGui.cpp/h       # Apple 1 display (40×24 + CRT effects)
├── CassetteDeck_ImGui.cpp/h # Procedural cassette deck widget
├── MemoryViewer_ImGui.cpp/h # Hex editor with search & navigation
├── CassetteDevice.cpp/h     # Apple Cassette Interface (Woz ACI + audio)
├── GraphicsCard.cpp/h       # GEN2 color graphics card (280×192 HIRES)
├── GT6144.cpp/h             # SWTPC GT-6144 (64×96 mono, 1976)
├── TMS9918.cpp/h            # P-LAB TMS9918 VDP
├── SID.cpp/h                # P-LAB A1-SID (libresidfp)
├── AudioDevice.cpp/h        # miniaudio / Web Audio output, SID + cassette mixer
├── MicroSD.cpp/h            # P-LAB microSD Storage Card (65C22 + MCU)
├── CFFA1.cpp/h              # CFFA1 CompactFlash (ROM + ProDOS .po)
├── JukeBox.cpp/h            # P-LAB Apple-1 Juke-Box (32 KB EEPROM library)
├── PR40Printer.cpp/h        # SWTPC PR-40 printer ($D012 sniffer, Jobs 1976)
├── WiFiModem.cpp/h          # P-LAB Wi-Fi Modem (65C51 ACIA + TCP/TELNET)
├── TerminalCard.cpp/h       # P-LAB Terminal Card (TCP server + serial bridge)
├── A1IO_RTC.cpp/h           # P-LAB I/O Board & RTC (65C22 + DS3231)
├── third_party/libresidfp/  # Vendored cycle-accurate SID engine (GPL-2.0+)
├── third_party/miniaudio.h  # Single-header audio + decoder (MP3/OGG/WAV)
├── tools/
│   ├── sid2apple1.py        # C64 PSID/RSID → Apple 1 .bin for A1-SID
│   └── test_*_telnet.py     # Headless integration tests
├── roms/                    # WozMonitor, BASIC, Applesoft Lite ×2, Krusader,
│                            #   ACI, SD CARD OS, CFFA1, Juke-Box, charmap
├── sdcard/                  # Virtual SD card content (host directory)
├── cfcard/                  # CFFA1 ProDOS disk — bundled for desktop & WASM
├── cassettes/               # Original-tape captures (incl. WOZ_talk default)
├── pic/                     # App icon + About photo + Apple 50th logo
├── fonts/                   # Font Awesome (toolbar glyphs)
├── software/                # 60+ programs + assembly sources (games, demos,
│                            #   BASIC, Applesoft, dev, utils, hgr, tms9918,
│                            #   sid, net, a1io_rtc, tests)
├── build-wasm/              # WebAssembly build output
├── setup_imgui.sh           # One-shot setup
└── run_emulator.sh          # Build check + ROM copy + launch
```

---

## 📀 ROMs

| ROM | Size | Address | Origin |
|-----|------|---------|--------|
| 📼 **ACI** | 256 B | `$C100` | Woz Apple Cassette Interface monitor |
| 👁️ **Woz Monitor** | 256 B | `$FF00` | Steve Wozniak's original system monitor |
| 💻 **Apple BASIC** | 4 KB | `$E000` | Integer BASIC interpreter |
| 💿 **Applesoft Lite (CFFA1)** | 8 KB | `$E000-$FFFF` | Applesoft + Woz Monitor (CFFA1 layout) — `applesoft-lite-cffa1.rom` |
| 💿 **Applesoft Lite (microSD)** | 8 KB | `$6000-$7FFF` | P-LAB Fast Terminal / SD OS 1.2 — `applesoft-lite-microsd.rom` |
| 🔧 **Krusader 1.3** | 8 KB | `$A000` | Ken Wessen's symbolic assembler (Replica 1 preset) |
| 💾 **SD CARD OS** | 8 KB | `$8000` | P-LAB microSD firmware ([apple1-sdcard](https://github.com/nippur72/apple1-sdcard)) |
| 💽 **CFFA1 firmware** | ~8 KB | `$9000-$AFDF` | CFFA1 card ROM — `cffa1.rom` |
| 💿 **Juke-Box EEPROM** | 32 KB | `$4000-$BFFF` or `$8000-$BFFF` | P-LAB Juke-Box — `jukebox.rom` (signature `$A5` at file offset `$7D00`) |
| 🔤 **Charmap** | 1 KB | — | Character generator table |

Woz Monitor, Integer BASIC and the ACI ROM load at startup. Card-specific ROMs load when the matching card is enabled (SD CARD OS with microSD, CFFA1 firmware with CFFA1, Krusader with the Replica 1 preset, Juke-Box EEPROM with preset #10).

---

## 🗺️ Memory Map

```
$0000-$00FF   Zero Page
$0100-$01FF   Stack
$0200-$1FFF   User RAM (programs load at $0280 or $0300)
$2000-$200F   P-LAB I/O Board & RTC — VIA 65C22 (overlaps GEN2 HGR page)
$2000-$3FFF   GEN2 HGR Framebuffer (8 KB)
$4000-$BFFF   Juke-Box EEPROM window (32 KB, RAM-16/ROM-32 jumper)
$4000-$7FFF   User RAM (otherwise)
$6000-$7FFF   Applesoft Lite ROM (P-LAB microSD + Applesoft layout only)
$8000-$BFFF   Juke-Box EEPROM window (16 KB upper half, RAM-32/ROM-16)
$8000-$9FFF   SD CARD OS ROM (P-LAB microSD)
$9000-$AFDF   CFFA1 firmware ROM (with CFFA1)
$AFE0-$AFFF   CFFA1 ATA/IDE registers
$A000-$A00F   VIA 65C22 I/O (16 registers, P-LAB microSD)
$A010-$AFFF   User RAM (Krusader may use $A000-$BFFF without microSD)
$B000-$B003   ACIA 65C51 — MODEM BBS I/O
$B004-$BFFF   User RAM
$C000-$C0FF   Apple Cassette Interface I/O  ($C081 tape input)
$C100-$C1FF   Woz ACI ROM
$C800-$CFFF   A1-SID registers (addr & $1F)
$CC00 / $CC01 TMS9918 DATA / CTRL (also A1-AUDIO SE window $CC00-$CC1F)
$D00A         SWTPC GT-6144 command port (write-only)
$D010-$D012   PIA 6821 — Keyboard (KBD) & Display (DSP), aliases $D0Fx
$E000-$EFFF   Integer BASIC ROM (or Applesoft Lite in the CFFA1 layout)
$FF00-$FFFF   Woz Monitor ROM
```

The **POM1** preset ships with **64 KB** of user RAM; overlays from expansion cards and ROMs still win where they decode. Open the in-app **Memory Map** for the live layout.

---

## 👏 Credits

- **Arnaud Verhille** — Original POM1 (Java, 2000) & Dear ImGui port (2026)
- **Ken Wessen** — Upgrades, 65C02 support (2006)
- **Joe Crobak** — macOS Cocoa port
- **John D. Corrado** — C/SDL port (2006–2014)
- **Lee Davison** — Enhanced BASIC
- **Achim Breidenbach** — Sim6502
- **Fabrice Frances** — Java Microtan Emulator
- **Uncle Bernie** — [GEN2 Color Graphics Card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1)
- **Claudio Parmigiani ([P-LAB](https://p-l4b.github.io/))** — the entire P-LAB Apple-1 expansion family: [microSD](https://p-l4b.github.io/sdcard/), [A1-SID](https://p-l4b.github.io/A1-SID/), [Graphic Card (TMS9918)](https://p-l4b.github.io/graphic/), [I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/), [Terminal Card](https://p-l4b.github.io/terminal/), [Wi-Fi Modem](https://p-l4b.github.io/wifi/)
- **Rich Dreher** — **CFFA1** CompactFlash interface (firmware / hardware design emulated here)
- **Nippur72 (Antonino Porcino)** — [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib) (KickC, Tetris, demos) + [apple1-sdcard](https://github.com/nippur72/apple1-sdcard) firmware
- **Tom Owad** — AppleFritter community & Apple 1 resources
- **Steve Wozniak & Steve Jobs** — For creating the Apple 1 🍎

## 🔗 Resources

- [**apple1software.com**](https://apple1software.com/) — definitive Apple 1 software archive (source of most of POM1's bundled programs).
- [**AppleFritter**](https://applefritter.com/apple1/) — the Apple 1 community hub (technical threads, hardware projects, first-hand accounts).
- [**Uncle Bernie's GEN2 Graphics Card**](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1) — original 280×192 HIRES hardware project.
- [**P-LAB Graphic Card**](https://p-l4b.github.io/graphic/) + [**apple1-videocard-lib**](https://github.com/nippur72/apple1-videocard-lib) — TMS9918 card + KickC library.
- [**P-LAB microSD**](https://p-l4b.github.io/sdcard/) · [**Wi-Fi Modem**](https://p-l4b.github.io/wifi/) · [**I/O & RTC**](https://p-l4b.github.io/A1-IO_RTC/) · [**Terminal Card**](https://p-l4b.github.io/terminal/) — P-LAB expansion docs.
- [**HVSC**](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection) — the Commodore 64 SID music archive; feed into [`tools/sid2apple1.py`](tools/sid2apple1.py) for the A1-SID.
- [POM1 Project Page](https://www.gistlabs.net/Apple1project/)

---

## 📄 License

GPL-3.0 — see [LICENSE](LICENSE).

<div align="center">

*Made with ❤️ for the Apple 1 community*

</div>
