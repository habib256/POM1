<div align="center">

# 🍎 POM1 v1.9.2 — Apple 1 Emulator

### *The 1976 personal computer revolution, faithfully reborn — with 50 years of expansion cards bolted on.*

🎂 **Celebrating 50 years of Apple (1976 → 2026)** with the most complete Apple 1 emulator ever shipped: 13 one-click machine presets, 16 expansion cards, 60+ ready-to-run programs, and a cycle-accurate libresidfp SID engine with hot-swappable 6581/8580 chips — a 1976 SWTPC GT-6144 graphics card sitting next to a 2026 Wi-Fi modem.

**Two colour graphics cards, one graphics BASIC.** Paint in colour on **Uncle Bernie's GEN2 HGR Card** (280×192) *and* the **P-LAB TMS9918** (256×192 + 32 sprites) — then drive *both* from an **Apple-1 Applesoft** whose Apple II graphics commands (`HGR` · `HPLOT` · `HCOLOR`) run the **same listing** on either card.

Built with Dear ImGui & OpenGL — fast, lightweight, cross-platform.

[![▶ Play in browser (no install)](https://img.shields.io/badge/▶%20Play%20in%20browser-WebAssembly-blueviolet?style=for-the-badge)](https://habib256.github.io/POM1/build-wasm/POM1.html)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Linux%20•%20macOS%20•%20Windows%20•%20Web-lightgrey.svg)](#-get-pom1)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](#)
[![13 presets](https://img.shields.io/badge/Presets-13-success.svg)](#%EF%B8%8F-machine-presets)
[![Programs](https://img.shields.io/badge/Software-60%2B-yellowgreen.svg)](#-software-library)

![POM1 Screenshot](doc/reference/screenshot.png)

</div>

---

## 🌟 Why POM1?

> *Every other Apple 1 emulator stops at the WOZ Monitor. POM1 keeps going for 50 years.*

- 🎨 **Three independent graphics cards across half a century** — **Uncle Bernie's GEN2 HGR** (280×192 Apple-II-style colour) and the **P-LAB TMS9918** (256×192 + 32 hardware sprites), plus the 1976 SWTPC GT-6144. Drive both colour cards from graphics BASIC, C, or asm.
- 🎵 **Real chiptune sound on a 1976 board** — genuine MOS 6581 / CSG 8580 SID through libresidfp, swap chips *while it plays*.
- 📡 **Wi-Fi modem dialing real BBSes** — `ATDT bbs.fozztexx.com:23` in WOZ Monitor and you're online, on a 1976 machine.
- 💾 **A cartridge ecosystem unique to POM1** — the P-LAB CodeTank ships **3 ready-to-flip cartridges** (GAME1/2/3): arcade games, a dungeon crawler, a LOGO turtle, graphics demos.
- 🔬 **Cycle-accurate down to the bus** — SID, TMS9918, cassette and modem all run on the same 1.022727 MHz clock; tempo follows emulation speed, not wall-clock. Klaus Dormann's 6502 test pinned in CI.
- 🛠️ **Write your own — without leaving the app** — the in-app **DevBench** assembles 6502 asm, compiles C, runs BASIC, or eats a Woz hex dump, then boots it in one click.

---

## ⚡ 60-second tour

Five things to try **right after first boot** (every preset boots into WOZ Monitor at `$FF00`). New to the Apple 1? Follow the guided **[`QUICKSTART.md`](QUICKSTART.md)** — your first program in 5 minutes.

```
0:F      ; Wozmon "examine" — read $0000 (PIA test)
F000R    ; cold-start whatever ROM is currently mapped at $F000
```

1. **Write your first BASIC program** → preset **#4**, type `E000R` (cold-start Integer BASIC), then `10 PRINT "HELLO WORLD"` and `RUN`. Welcome to 1976.
2. **Play the A1-SID piano** → preset **#12** (default), *File → Load Memory* → `software/SOUND SID/Claudio_PARMIGIANI_SID_PIANO_ORIG.txt`, type `C400R`, then press keys to play.
3. **Plug a TMS9918 cartridge** → preset **#9** (CodeTank), *File → P-LAB CodeTank Library* → `Codetank_GAME2.rom` → flip *upper jumper* → `4000R`. Mode-III Nyan Cat at 20 fps.
4. **BBS over real TCP** → preset **#12** (default), `0280R` to load ATmodem, then `ATDT bbs.fozztexx.com:23`. Browse a 2026-era BBS in WOZ Monitor.
5. **Live debugging** → `F1` opens the memory viewer, `F7` single-steps the 6502, `F3` opens the BRK trace. Watch Microchess plan its move.

---

## 🚀 Get POM1

Two zero-effort ways to start — no toolchain, no build:

### ▶ Play in your browser

**[Launch POM1 now →](https://habib256.github.io/POM1/build-wasm/POM1.html)** — the full emulator runs in WebAssembly. Nothing to install.

### ⬇ Download a release

**[Grab the latest build →](https://github.com/habib256/POM1/releases)** for Windows (`.zip`), macOS (`.dmg`) or Linux (AppImage). The desktop packages **bundle everything** — including the cc65 DevBench toolchain — so it works out of the box with nothing else to install.

<details>
<summary><b>🔧 Build from source</b> (developers)</summary>

#### 🐧 Linux / 🍏 macOS

```bash
git clone https://github.com/gistarcade/POM1.git
cd POM1
./setup_pom1.sh                    # fetch Dear ImGui + install deps (one-time)
cd build && cmake .. && make
cd .. && ./run_emulator.sh
```

#### 🪟 Windows

Prereqs: [Visual Studio](https://visualstudio.microsoft.com/) (C++ workload), [CMake](https://cmake.org/download/), [Git](https://git-scm.com/download/win), [vcpkg](https://vcpkg.io/).

```batch
git clone https://github.com/gistarcade/POM1.git
cd POM1
setup_pom1.bat
cd build && cmake --build . --config Release
cd .. && run_emulator.bat
```

#### 🌐 WebAssembly

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh && cd ..
cd POM1 && mkdir -p build-wasm && cd build-wasm
emcmake cmake .. && emmake make -j$(nproc)
emrun POM1.html
```

#### Manual dependency install (if `setup_pom1.sh` can't)

| OS | Command |
|---|---|
| Ubuntu / Debian | `sudo apt install cmake libglfw3-dev libgl1-mesa-dev pkg-config` |
| Fedora | `sudo dnf install cmake glfw-devel mesa-libGL-devel pkgconf` |
| Arch | `sudo pacman -S cmake glfw mesa pkgconf` |
| macOS | `brew install cmake glfw pkg-config` |
| Windows | `vcpkg install glfw3:x64-windows` |

A source/git build also needs a system **cc65** for the asm/C DevBench targets (`apt install cc65` · `dnf install cc65` · `pacman -S cc65` · `brew install cc65` · <https://cc65.github.io/>). Release packages bundle it.

</details>

<details>
<summary><b>🎛️ Command line</b> (~30 flags for headless / scripted runs)</summary>

Full reference → [`doc/CLI.md`](doc/CLI.md).

```bash
./POM1 --list-presets
./POM1 --preset 4 --enable jukebox --terminal &   # Juke-Box + telnet on :6502
./POM1 --enable sid --terminal --cpu-max          # plug A1-SID on the default preset
./POM1 --tape cassettes/APPLE50TH.ogg             # auto-press Play
./POM1 --preset 9 \
       --codetank-rom roms/codetank/Codetank_GAME2.rom \
       --codetank-jumper upper                     # boot directly into Nyan/CodeTank
```

</details>

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action | Shortcut | Action |
|----------|--------|----------|--------|
| `F1` | Memory Viewer | `Ctrl+O` | Load program |
| `F2` | Memory Map | `Ctrl+S` | Save memory |
| `F3` | Debug Console | `Ctrl+V` | Paste code |
| `F5` / `Ctrl+F5` | Soft / Hard Reset | `F6` | Start / Stop CPU |
| `F7` | Single-step | `Ctrl+Q` | Quit |

The keyboard is **TTL-faithful** — no autorepeat by default, like the real ASCII keyboard ROM. Toggle host autorepeat from *Settings* if you can't take it.

---

## 🖥️ Machine Presets

One click in the **Presets** menu (or `--preset N`) reconfigures the whole machine — RAM, BASIC and cards. Per-preset window layouts persist under `ini/imgui_preset_NN.ini`.

| # | Preset | RAM | BASIC | Cards |
|:-:|:-------|:---:|:------|:------|
| 0 | **Apple-1 CC65 Development Bench** 🛠 | 8 KB | Integer cassette | ACI |
| 1 | **Apple-1 TMS9918 Development Bench** 🛠 | 8 KB | — | TMS9918, CodeTank |
| 2 | **Apple-1 GEN2 HGR Development Bench** 🛠 | 48 KB | — | GEN2 HGR, ACI |
| 3 | **Bare Apple-1 (July 1976)** | 4 KB | — | — |
| 4 | **Apple-1 with ACI & BASIC cassette (Oct 1976)** | 8 KB | Integer cassette | ACI |
| 5 | **Apple-1 + SWTPC GT-6144 (1976)** | 8 KB | — | ACI, GT-6144, PR-40 |
| 6 | **Replica-1 with ACI & Krusader (Briel 2003)** | 8 KB | — | ACI, Krusader |
| 7 | **Replica-1 with CFFA1 & Applesoft Lite (Dreher 2007)** | 8 KB | Applesoft Lite | CFFA1 |
| 8 | **P-LAB microSD & Applesoft Lite (Apr 2022)** | 8 KB | Applesoft Lite | microSD |
| 9 | **P-LAB Apple-1 with TMS9918 + CodeTank** | 8 KB | — | TMS9918, CodeTank |
| 10 | **P-LAB Multiplexing Fantasy** | 64 KB | Applesoft Lite | microSD, A1-SID, TMS9918 + CodeTank, I/O & RTC, Wi-Fi, Terminal |
| 11 | **Uncle Bernie's GEN2 HGR Color (Apr 2026)** | 48 KB | — | GEN2 HGR, ACI |
| 12 | **POM1 Multiplexing Fantasy (2026)** ⭐ | 64 KB | Applesoft Lite | ACI, microSD, A1-SID, Wi-Fi, Terminal |

⭐ = default. 🛠 **Development benches (0–2)** are the profiles the in-app **DevBench** loads per machine; each mirrors an existing preset (CC65 = *ACI & BASIC cassette*, TMS9918 = *TMS9918 + CodeTank*, GEN2 = *GEN2 HGR Color*). The **A1-SID, I/O & RTC, Wi-Fi Modem, Juke-Box and IEC** cards have no dedicated preset — plug them from the **Hardware** menu, or via `--enable {sid,rtc,wifi,jukebox,iec}`. Preset details (RAM banking, the GEN2 "54 KB" expansion, Parmigiani's one-board rule) → [`CLAUDE.md`](CLAUDE.md).

---

## ✨ Hardware — the cards

The **core machine** is an authentic 40×24 display (`charmap.rom` bitmap, three CRT phosphors, scanlines, glow) driving a cycle-accurate **6502** (all official opcodes, ~1.022727 MHz / 2× / MAX) with paste-code, a step debugger and a live memory editor + visual map. Then you bolt on 50 years of expansion cards:

| Card | Year | Highlights |
|---|---:|---|
| 📼 **ACI cassette** | 1976 | `.aci`/`.wav`/`.mp3`/`.ogg`, procedural deck widget, smart jaquette via `tapeinfo.txt` |
| 🎨 **SWTPC GT-6144** | 1976 | First commercial Apple 1 graphics card. 64×96 mono, $98.50, demoed by Woz in *Interface Age*. Power-on bistable noise reseeds on every plug |
| 🖨️ **SWTPC PR-40** | 1976 | Steve Jobs' Oct-76 *Interface Age* mod — every char on `$D012` also prints on a 40-col matrix printer. Mixed/PrintOnly DPDT switch |
| 💾 **Briel Krusader** | 2003 | Vince Briel's monitor extension |
| 💾 **CFFA1** | 2007 | Rich Dreher's CompactFlash card. ProDOS `.po` images, ATA/IDE regs at `$AFE0` |
| 💾 **[P-LAB microSD](https://p-l4b.github.io/sdcard/)** | 2022 | Virtual FAT32 over 65C22+ATMEGA. SD CARD OS shell (`DIR`, `LOAD`, `CD`, fuzzy match) |
| 📼 **[P-LAB IEC daughterboard](https://p-l4b.github.io/iec/)** | 2026 | Commodore 1541 over SN7406 on microSD's spare VIA pins. `@DEV`/`@$`/`@L`/`@S` shell |
| 🎵 **[P-LAB A1-SID](https://p-l4b.github.io/A1-SID/)** | — | libresidfp 6581/8580, hot-swap chip model live, `.sid` converter |
| 🎵 **A1-AUDIO Special Edition** | — | Same chip relocated to `$CC00-$CC1F` (excludes TMS9918) |
| 🎨 **[Uncle Bernie's GEN2 HGR](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1)** | 2026 | 280×192 Apple-II-style framebuffer with NTSC artifact colour |
| 🎨 **[P-LAB TMS9918](https://p-l4b.github.io/graphic/)** | — | TMS9918A VDP, 256×192, 32 sprites, 4 modes. Silicon-strict timing model |
| 💾 **P-LAB CodeTank** | — | Daughterboard of the TMS9918 card. **3 cartridges shipped** (see Software) |
| 💾 **[P-LAB Juke-Box](https://p-l4b.github.io/jukebox/)** | — | Paged 16 KB–512 KB flash + writable 28c256 EEPROM. `$CA00` bank latch |
| ⏰ **[P-LAB I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/)** | — | DS3231, DS18B20, ADC + digital I/O |
| 📡 **[P-LAB Wi-Fi Modem](https://p-l4b.github.io/wifi/)** | — | 65C51 + ESP8266, Hayes AT, real TCP/TELNET (desktop only) |
| 🖥️ **[P-LAB Terminal Card](https://p-l4b.github.io/terminal/)** | — | TCP server on `:6502`, sniffs `$D012`, injects keys at `$D010`. ANSI host terminals work as the Apple 1 console |

Bus-window exclusions are enforced (one P-LAB card at a time, per Parmigiani's real-hardware rule); see the in-app *Help → Hardware Reference*.

<details>
<summary><b>📖 Card deep-dives</b> — addresses, modes, quirks</summary>

**SWTPC GT-6144** *(1976)* — first commercial Apple-1 graphics card, $98.50, demoed by Woz in *Interface Age*. **64×96** mono framebuffer on 6× Intel 2102 SRAM, write-only I/O at `$D00A`. 4-phase command protocol; visible bistable SRAM noise on every plug-in. `software/Graphic gt-6144/` ships Game-of-Life + demos.

**Uncle Bernie's GEN2 HGR** — **280×192** HIRES, Apple II-compatible memory at `$2000-$3FFF`, **NTSC artifact colour**. Auto-loads `software/Graphic HGR/GEN2.HGR.BIN`; includes [HGR Maze](sketchs/gen2/game_maze/HGR_Maze.asm). Details → [`doc/GEN2_RELEASE.md`](doc/GEN2_RELEASE.md).

**P-LAB TMS9918** — TMS9918A VDP, **256×192**, 15 colours + transparent, 32 hardware sprites, 4 modes. I/O at `$CC00`/`$CC01`, 16 KB dedicated VRAM. Compatible with [nippur72's apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib). **Silicon Strict** mode enforces the VRAM timing model — tune it from *DevBench → Silicon Strict Inspector*. Chip quirks → [`Programming_TMS9918.md`](sketchs/doc/Programming_TMS9918.md).

**P-LAB CodeTank** — ROM **daughterboard** of the TMS9918 card (enabling CodeTank auto-plugs TMS9918). Single 32 KB 28c256; jumper picks which 16 KB half maps to `$4000-$7FFF`. The **3-cartridge library** (GAME1/2/3) is in [Software Library](#-software-library). CLI: `--enable codetank`, `--codetank-jumper lower|upper`, `--codetank-rom <path>`.

**P-LAB A1-SID** — driven by **[libresidfp](https://github.com/libsidplayfp/libsidplayfp)** (vendored, GPL-2.0+). **Hot-swappable chip model** (MOS 6581 ↔ CSG 8580) restores register state live. Cycle-driven into a lock-free ring; tempo follows emulation speed. Pick the address window in **Settings → A1-SID version & addresses**: standard **$C800-$CFFF** or the **A1-AUDIO SE** variant at **$CC00-$CC1F**. Add tunes via [`tools/sid2apple1.py`](tools/sid2apple1.py) from the [HVSC](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection) archive.

**P-LAB microSD** — DOS-like file system over a 65C22 VIA + emulated ATMEGA. **SD CARD OS ROM** at `$8000-$9FFF` ([firmware](https://github.com/nippur72/apple1-sdcard), CC BY 4.0). Host `sdcard/` mounted as virtual FAT32. **Tagged filenames** `NAME#TTAAAA` (`#06` binary, `#F1` Integer BASIC, `#F8` Applesoft, `AAAA` load address). Shell `LOAD` does fuzzy prefix match **within the current directory only**. With microSD on (CFFA1 off), presets load **Applesoft Lite** at `$6000-$7FFF` — cold start `6000R`.

**P-LAB IEC daughterboard** — Commodore IEC serial bus add-on for the microSD card. Single **SN7406** on the spare 65C22 PORTB pins; no new MMIO. Backed by a virtual **1541** drive on `disks/iec/dev8.d64` (174 848 B). Same SD CARD OS ROM with `@`-prefixed commands (`@DEV`, `@$`, `@L`, `@S`, …). MVP = device 8 only.

**CFFA1** — Rich Dreher's CompactFlash card. 8 KB firmware at `$9000-$AFDF`, ATA/IDE regs `$AFE0-$AFFF`, backed by a host **ProDOS `.po`**. **Mutually exclusive with microSD** (shared `$9000`). Firmware menu with `9006R`; default disk `cfcard/cfcard.po`.

**P-LAB Juke-Box** — memory-mapped flash library acting as an in-address-space program menu. **Flash mode** (16 KB–512 KB, paged) or **EEPROM 28c256** (32 KB, writable via RW jumper). ROM window `$4000-$BFFF` or `$8000-$BFFF`; bank-select latch at `$CA00`. Rebuild via [`build_jukebox_rom.py`](doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py).

**P-LAB Wi-Fi Modem (MODEM BBS)** — 65C51 ACIA + ESP8266, real BBS servers over TCP/TELNET (desktop only). I/O `$B000-$B003`. **Hayes AT**: `ATDT host:port`, `ATH`, `ATE0/1`, `ATZ`, `+++`. To connect: enable Wi-Fi Modem + Terminal Card → `telnet localhost 6502` → load `software/NET/ATmodem.txt` → `0280R` → `ATDT BBS.FOZZTEXX.COM:23`. Full ANSI in the external terminal.

**P-LAB Terminal Card** — passive bidirectional serial bridge (desktop only). TCP server on `localhost:6502`; sniffs `$D012`, injects keys at `$D010`/`$D011` — native screen keeps working. 7-bit (CR→CRLF) and 8-bit raw (`Ctrl-T`) modes; `Ctrl-L` clear, `Ctrl-R` reset, `Ctrl-S` screenshot. Run `telnet localhost 6502` to drive Wozmon, BASIC, everything.

**SWTPC PR-40 printer** *(Jobs 1976)* — Steve Jobs' Oct-76 *Interface Age* hack: tee a 40-column matrix printer off PIA Port B so **every character on the display also prints**. No MMIO (third sniffer on `$D012`). 40-char FIFO, ~0.8 s mechanical cycle, DPDT *Off/Mixed/PrintOnly* switch, scrollable paper roll with Save-to-`.txt`.

**P-LAB I/O Board & RTC** — 65C22 VIA at `$2000-$200F` bridging an emulated ATMEGA32 + DS3231. Regs 0–5 = RTC, reg 6 = die temperature; ADC + digital I/O follow. **⚠ Overlaps `$2000-$3FFF` GEN2 HGR framebuffer — don't enable both at once.**

</details>

---

## 🎮 Software Library

`software/` ships **60+ ready-to-run programs** — load via *File → Load Memory*. Most come from [apple1software.com](https://apple1software.com/), the reference archive. Loading a file from a card-specific folder (`Graphic HGR/`, `Graphic TMS9918/`, `SOUND SID/`, `NET/`, …) auto-plugs the matching card and opens its window.

### 🃏 P-LAB CodeTank cartridge library

Plug the CodeTank daughterboard (preset 9 or *Hardware → CodeTank*), open *File → P-LAB CodeTank Library*, pick a `.rom`, choose a jumper. **3 cartridges shipped:**

| ROM | Lower jumper (`4000R`) | Upper jumper (`4000R`) |
|---|---|---|
| **`Codetank_GAME1.rom`** | Tetris/CodeTank (full bank) | menu → 1=Galaga 2=Sokoban 3=Snake |
| **`Codetank_GAME2.rom`** | TMS_Rogue (dungeon crawler) | TMS_Nyan_CodeTank (12-frame Mode III animation) |
| **`Codetank_GAME3.rom`** | TMS_LOGO V2.6 turtle interpreter | menu → 1=Life 2=Mandel 3=Plasma |

<details><summary><b>🕹️ Other games & demos</b></summary>

| Program | Notes |
|---------|-------|
| ♟️ **Microchess** | Peter Jennings' chess engine — first commercial microcomputer game |
| ♟️ **Chess** | Pure-asm chess inspired by [StewBC/cc65-Chess](https://github.com/StewBC/cc65-Chess) — text source ([asm](sketchs/apple1/game_chess/Chess.asm)). v0.1: HvH, full move-gen + check/mate. AI v1.2 |
| 🏰 **LittleTower** | Text adventure ([asm](sketchs/apple1/game_little_tower/LittleTower-1.0.asm)) |
| 🌙 **Lunar Lander** · 🔢 **2048** · 🔐 **Codebreaker** / 🧠 **Mastermind** · 🎲 **Shut the Box** · 🔵 **Peg Solitaire** · 🧩 **15-Puzzle** · 📝 **Worple** | Classics |
| 🧬 **Game of Life** · 🎂 **30th** · 🐱 **ASCII Cat** · 🍺 **99 Bottles** | Demos |
| 🌀 **Maze** / **Maze 2** | Sidewinder ([asm](sketchs/apple1/game_maze_sidewinder/Maze_Sidewinder.asm)) and Recursive Backtracker ([asm](sketchs/apple1/game_maze_backtracker/Maze2_Backtracker.asm)) |
| 🎨 **HGR Maze** | GEN2 HIRES maze generator ([asm](sketchs/gen2/game_maze/HGR_Maze.asm)) |
| 🌌 **Mandelbrot** · 📊 **Cellular** · 🎨 **PasArt** | Fractal, 1D CA, parametric ASCII |
</details>

<details><summary><b>💻 BASIC programs</b> (<code>software/Integer_basic/</code>) — cold-start Integer BASIC with <code>E000R</code></summary>

🚀 Star Trek · 🃏 Blackjack · 🏛️ Hamurabi · 🌙 Lunar Lander Graphics · ⏱️ Stopwatch · 🔧 Resistor Calculator

These are **Integer BASIC** listings (`software/Integer_basic/*.apl.txt`). Cold-start the interpreter once with `E000R`, then *File → Load Memory* → a `.apl.txt` (each ends with `E2B3R`, so it re-enters BASIC with the program intact), then type `RUN`.
</details>

<details><summary><b>🛠️ Dev tools</b></summary>

👁️ Woz Monitor · 💻 Enhanced BASIC · 📘 fig-FORTH · 🔬 Disassembler · 🔨 A1 Assembler · ✍️ Typewriter · 🎉 Party
</details>

<details><summary><b>🎵 A1-SID music</b> (<code>software/SOUND SID/</code>)</summary>

A dozen tunes (Hubbard, Tel, Daglish, Huelsbeck…). Load at `$0280`, enable SID, `280R`. Add yours via [`tools/sid2apple1.py`](tools/sid2apple1.py) from the [HVSC](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection) archive.
</details>

---

## 🔧 Write your own Apple 1 software

Boot POM1, open **DevBench → POM1 Bench**, type code, hit **▶ Run**. No SDK, no Makefile, no command line — the Bench assembles/compiles/injects your program and boots it on the emulator, with a `HELLO WORLD` starter for every target. New here? → **[`QUICKSTART.md`](QUICKSTART.md)**.

**Pick a machine** (Run auto-switches to its preset):

| Target | Canvas |
|---|---|
| 🍎 **Apple-1 (text)** | The 1976 original — 40×24 through the WOZ Monitor. *Start here.* |
| 🎨 **P-LAB TMS9918** | 256×192, 15 colours, **32 hardware sprites** — flashed into a CodeTank cartridge |
| 🌈 **Uncle Bernie GEN2 HGR** | 280×192 Apple-II-style **HIRES colour** |

**Pick a language** — **Assembly** (`ca65`/`ld65`), **C** (`cc65`/`cl65`), **BASIC** (Integer or Applesoft, types your listing at the prompt — works in the browser too), or **Woz hex** (paste a Monitor dump, zero toolchain).

### 🌈 The party trick: graphics BASIC on a 1976 machine

POM1 ships an Apple-1 **Applesoft** extended with the **Apple II graphics command set** — `HGR`, `HCOLOR=`, `HPLOT … TO …`, `GR`, `COLOR=`, `PLOT`, `HLIN`/`VLIN`, `SCRN()` — and the **same listing** runs on **both** colour cards:

```basic
10 HGR : HCOLOR= 3
20 FOR X = 0 TO 255 STEP 8
30 HPLOT X,0 TO 255 - X,191
40 NEXT X
```

Run it as **Applesoft GEN2 HGR** (`9800R`) and it draws on Uncle Bernie's colour card; run the byte-identical program as **Applesoft TMS9918** (`4000R`) and it draws on the P-LAB VDP. Ready-made demos (Mandelbrot, Sierpinski, 3D Hat, Boy Surface…) live in [`sketchs/basic_applesoft/`](sketchs/basic_applesoft/).

**Toolchain?** The release packages **bundle cc65**, so asm and C compile out of the box — nothing to install. Only a source build needs a system cc65.

Full target matrix and the in-app DevBench tools → **[`doc/DEVBENCH.md`](doc/DEVBENCH.md)**. The complete index of everything written about POM1 — guides, card references, CLI, architecture, the cc65 source tree — is **[`doc/README.md`](doc/README.md)**.

---

## 👏 Credits

- **Arnaud Verhille** — Original POM1 (Java, 2000) & Dear ImGui port (2026)
- **Claudio Parmigiani ([P-LAB](https://p-l4b.github.io/))** — the entire P-LAB Apple-1 expansion family: [microSD](https://p-l4b.github.io/sdcard/), [A1-SID](https://p-l4b.github.io/A1-SID/), [Graphic Card (TMS9918)](https://p-l4b.github.io/graphic/), [I/O Board & RTC](https://p-l4b.github.io/A1-IO_RTC/), [Terminal Card](https://p-l4b.github.io/terminal/), [Wi-Fi Modem](https://p-l4b.github.io/wifi/), [Juke-Box](https://p-l4b.github.io/jukebox/) (with Jacopo Rosselli)
- **Uncle Bernie** — [GEN2 Color Graphics Card](https://www.applefritter.com/content/uncle-bernies-gen2-color-graphics-card-apple-1)
- **Rich Dreher** — **CFFA1** CompactFlash interface
- **Nippur72 (Antonino Porcino)** — [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib) + [apple1-sdcard](https://github.com/nippur72/apple1-sdcard) firmware
- **Ken Wessen** — Upgrades, 65C02 support (2006); **Joe Crobak** — macOS Cocoa port; **John D. Corrado** — C/SDL port (2006–2014); **Lee Davison** — Enhanced BASIC; **Achim Breidenbach** — Sim6502; **Fabrice Frances** — Java Microtan Emulator
- **Tom Owad** — AppleFritter community & Apple 1 resources
- **Steve Wozniak & Steve Jobs** — for creating the Apple 1 🍎

## 🔗 Resources

- [**apple1software.com**](https://apple1software.com/) — definitive Apple 1 software archive (source of most bundled programs).
- [**AppleFritter**](https://applefritter.com/apple1/) — Apple 1 community hub.
- [**P-LAB**](https://p-l4b.github.io/) — all P-LAB expansion docs.
- [**HVSC**](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection) — C64 SID music archive (feeds [`tools/sid2apple1.py`](tools/sid2apple1.py)).
- [POM1 Project Page](https://www.gistlabs.net/Apple1project/) · Architecture → [CLAUDE.md](CLAUDE.md) · Full doc map → [doc/README.md](doc/README.md).

---

## 📄 License

GPL-3.0 — see [LICENSE](LICENSE).

<div align="center">

*Made with ❤️ for the Apple 1 community*

</div>
