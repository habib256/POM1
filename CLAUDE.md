# CLAUDE.md

Architecture / invariants / gotchas for the **emulator side** of POM1. User walkthrough → `README.md`; open work → `TODO.md`; history → `git log`.

**Contents:** [Overview](#project-overview) · [Build](#build--run) · [CLI](doc/CLI.md) · [WASM / cc65](#wasm-build) · [Architecture](#architecture) · [MMIO / CPU](#key-implementation-details) · [Memory map](#memory-map) · [Platforms](#platform-notes) · [Testing](#testing) · [Version bump](#version-string-locations)

- Apple 1 software (BASIC, SID tunes, microSD shell tools, games) → **`dev/APPLE1DEV.md`** + **`dev/Programming_Apple1_ASM.md`**.
- **CLI flags** (full table) → **`doc/CLI.md`** · implementation **`CliDispatcher.cpp`**.
- 6502 ASM sources for every shipped program → **`dev/`** (`lib/{apple1,m6502,tms9918,hgr,sokoban}/`, `projects/<name>/`, `cc65/`). Compiled `.bin`/`.txt` land under `software/<dir>/` — that is what POM1 loads. Release bundles omit `dev/`; clone the repo for sources.

## Project Overview

Apple 1 emulator (Dear ImGui, MOS 6502 + display + keyboard + ACI cassette) plus expansion cards: Uncle Bernie's GEN2 HGR, P-LAB A1-SID (6581/8580), TMS9918, microSD (65C22+ATMEGA), **IEC daughterboard** (1541 drive over SN7406 on microSD's spare VIA pins), MODEM BBS (65C51+TCP), Terminal Card, A1-IO & RTC (65C22+ATMEGA32+DS3231), Juke-Box, CodeTank, Rich Dreher's CFFA1, SWTPC GT-6144 (1976) and PR-40 (1976, Jobs' *Interface Age* mod). Linux / macOS / Windows / Web (Emscripten).

**Preset truth:** `--preset N` indexes `kMachinePresets[]` in `MainWindow_Presets.cpp` (RAM size, cards, BASIC type). The README preset table must stay in lockstep with that array.

## Build & Run

```bash
./setup_imgui.sh             # one-time deps (Linux/macOS)
cd build && cmake .. && make # build → build/POM1
./run_emulator.sh            # runs from repo root
```

Windows: `setup_imgui.bat` + vcpkg + `cmake --build . --config Release`. `compile_commands.json` symlinked for clangd.

### CLI

Full flag table (phases A/B/C, every verb, telnet helpers) → **[`doc/CLI.md`](doc/CLI.md)**. Implemented in **`CliDispatcher.cpp`**. Malformed verbs log `[CLI] ERROR:` and exit 1.

### WASM build

```bash
source /path/to/emsdk/emsdk_env.sh
mkdir -p build-wasm && cd build-wasm
emcmake cmake .. && emmake make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
emrun POM1.html
```

MEMFS preload (`if(POM1_IS_WASM)` in `CMakeLists.txt`) mounts `roms/ pic/ fonts/ software/ sdcard/ cassettes/` and the single `cfcard/cfcard.po` (other `.po` extras are desktop-only, >140 MB). Rebuild WASM after any change under those trees or `build-wasm/shell.html`. POST_BUILD copies `pic/icon.png` to `build-wasm/pic/` as an HTTP-visible favicon (MEMFS unreachable to the browser). Desktop `Memory` ctor probes `sdcard`, `../sdcard`, `../../sdcard` and `cfcard/cfcard.po`.

### Assembling programs (cc65)

Sources in `dev/`. Per-project Makefiles call `ca65` + `ld65`, then `python3 emit_*_txt.py` lands `.bin` + Woz-hex `.txt` under `software/<original_dir>/`. Configs: `dev/cc65/apple1_4k.cfg` (text / TMS9918), `apple1_gen2.cfg` (HGR — reserves `$2000-$3FFF`), `codetank.cfg` (standalone CodeTank ROM at `$4000-$7FFF`), `pom1_fantasy.cfg` (Multiplexing Fantasy preset), `dev/projects/games_sokoban/apple1_sok_{4k,8k,hgr}.cfg`. Sokoban configs define `LEVELBUF` (zp `$0020`) + `STATEGRID` (bss); use `.segment "LEVELBUF": zeropage` to force `zp,X`. Reusable libraries under `dev/lib/` (apple1 equates, m6502 math, tms9918 driver, hgr tables, plus `dev/lib/games/{chess,sokoban}/` for shared engine + level data); Makefiles pass `-I ../../lib/<lib>` so `.include "apple1.inc"` works.

## Architecture

All C++ sources live under **`src/`** (vendored deps in **`src/third_party/`**). `imgui/`, `tests/`, `tools/` stay at the repo root. The root `CMakeLists.txt` collects sources via `${POM1_SRC_DIR}/...`. File references below are bare basenames — find them under `src/`.

Each `.cpp/.h` pair owns one concern. Only non-obvious bits are called out.

### Parmigiani's golden rule — "one board at a time"

Claudio PARMIGIANI (P-LAB designer): on real hardware exactly ONE P-LAB card is plugged. The 6502 bus has no arbitration and many P-LAB cards overlap address windows (A1-SID `$C800-$CFFF` vs TMS9918 `$CC00/$CC01`; A1-IO RTC `$2000-$200F` vs GEN2 HGR `$2000-$3FFF`; Juke-Box claims `$4000-$BFFF`). POM1 **breaks this on purpose** in "Multiplexing Fantasy" presets (#12, #14) — fantasy, not buildable. Mutex rules elsewhere mirror real bus conflicts. When adding a card, honour the rule and document any intentional coexistence.

### Core

- **M6502** — `op` is `quint16` (16-bit addresses), `tmp` is `int` (carry/borrow via bit 8). BCD ADC propagates the low→high carry from `(accumulator & 0xF0)`, not the unadjusted sum. **`run(maxCycles)` returns actual cycle count** — overshoots up to 6 cycles per iteration; wallclock pacers must deduct returned value. 12-slot PC ring + `dumpPcTrace(tag)`. `setDebugBrkTrace(true)` dumps regs/stack on every BRK. `setProgramCounter(pc)` is the Klaus harness back-door.
- **CpuClock.h** — `POM1_CPU_CLOCK_HZ = 1 022 727` (14.31818 MHz ÷ 14). Used by EmulationController, SID, TMS9918, modem baud, microSD idle, cassette timebase, terminal display delay.
- **Memory** — 64 KB. Owns every peripheral (`unique_ptr` + enable flag). MMIO via **`PeripheralBus`**; `memRead`/`memWrite` only handle PIA `$D0xx` aliasing, ROM write-protect, OOR strict mode, the cassette write-sniffer, `DisplayDevice::onChar` + TerminalCard hook, then raw `mem[]`. `setKeyPressedRaw()` bypasses forced-uppercase (Terminal Card). **`setTestMode(true)`** = flat 64 KB for Klaus harness only. **Redundant-ROM-load guards**: skip Applesoft Lite reload when `mem[$FF00..$FF01] == D8 58`; skip SD CARD OS reload when `mem[$8000..$8001] == A9 00`.
- **PeripheralBus** — central MMIO dispatch. Peripherals register `(name, range, priority, onRead, onWrite)`. Hot path O(1) via `pageMask[256]` bitmap (miss = 99 %+). `std::stable_sort` by priority — TMS9918 wins over SID at `$CC00/$CC01` via priority 10. `onWrite = {}` → pass-through to RAM; explicit no-op = block (CFFA1 ROM). Cassette write-toggle stays inline in `memWrite` (sniffer must still land in RAM).

### Emulation orchestration

- **EmulationController** — façade over M6502 + Memory + emulation thread. **Mutex order**: `stateMutex > keyboard.keyMutex > publisher.snapshotMutex`. `stateMutex` is a `PriorityMutex` — at MAX speed the loop yields after each slice iff `hasWaiters()`. Slice cap: `kMaxSliceCycles = 6000` desktop, 50 000 WASM (single-threaded). Native = dedicated thread; WASM = `pumpEmulationMainThread()`.
- **SnapshotPublisher** — SPSC slot. **Page-level dirty copy**: `memWrite` sets one bit per 256 B page; `publish()` collapses contiguous runs into single memcpy (4-8 pages/frame typical). Idle Wozmon = zero copy. TMS9918's 16 KB skipped when unplugged.
- **KeyboardController** — SPSC. `queueKey()` UI-safe; `drainTo(Memory&)` swaps to release `keyMutex` before `setKeyPressed()`.
- **DisplayDevice** — abstract `onChar(char)` sink for `$D012`. Injected via `Memory::setDisplayDevice()` — replaces old singleton so tests/peripherals can tee.
- **RomLoader** / **Disassembler6502** / **Logger** — RomLoader factors toggle-`writeInRom` + load + restore. `pom1::log().info("Tag", "msg")` → `StreamLogger` (stdout/stderr) + `RingBufferLogger` (in-app); `pom1::initDefaultTeeLogger()` wires both. Thread-safe.

### UI (ImGui)

- **main_imgui.cpp** — GLFW/OpenGL3 (GL 3.2 Core / GLSL 150; WebGL 2 / GLSL ES 300 on WASM). `GLFW_OPENGL_FORWARD_COMPAT` macOS-only. SIGINT/SIGTERM → `glfwSetWindowShouldClose(1)` so `--save-tape` flushes. Desktop probes `pic/icon.png` (cwd / `../` / exe-relative) for `glfwSetWindowIcon`; `#if !defined(__APPLE__)` (macOS pulls icon from `.app`).
- **MainWindow_ImGui** — single class across **8 TUs** sharing `MainWindow_Internal.h`: `_Layout`, `_Presets` (`kMachinePresets[]`), `_Menu`, `_Dialogs`, `_HardwareWindows` (TMS9918 GL texture 256×192 RGBA `GL_NEAREST`), `_FileDialogs` (auto-enable-by-directory), `_DebugWindows`, `_Keyboard`. Keyboard bypasses `InputQueueCharacters` — keys go GLFW → Apple 1. `handleGlfwKey` tags PRESS/REPEAT into `nextCharIsRepeat`. With autorepeat off (default, matches TTL keyboards), REPEAT drops printable + Enter/Backspace/Escape; F7 hold-to-step honours REPEAT.

  **`applyMachineConfig(int)` invariants**:
  - Unplugs every card, optional `hardReset()` (skipped first invocation), sets UI flags immediately, queues deferred plug via `pendingCardEnableFrames = kCardEnableDeferFrames` (15 frames ≈ 200 ms). Deferring past first CPU cycle fixes silent-card-on-boot (original symptom: silent SID).
  - **ROM selection uses preset config, not live flags** — microSD Applesoft Lite (`$6000-$7FFF`) vs CFFA1 flavour picked from `cfg.microSD`/`cfg.cffa1` directly (live flags still false at deferred plug time).
  - `applyPendingLayout(const char*)` runs before `Begin()` with **`ImGuiCond_FirstUseEver`**. **Widgets must not call `SetNextWindowSize(..., FirstUseEver)`** — overrides preset. `SetNextWindowSizeConstraints` is OK.
  - **Per-preset ini** `ini/imgui_preset_NN.ini` + `ini/preset_NN.size`. `io.IniFilename = nullptr`; POM1 manages files via `savePresetLayout`/`loadPresetLayout`. `applyMachineConfig(N)` saves outgoing then loads incoming before setting `activePresetIndex = N`. Clean shutdown saves again.
  - **Default preset = last entry** (*POM1 Multiplexing Fantasy*, currently index 14). First-time use writes `preset_14.ini` + `glfwSetWindowSize` to `max(computePresetLayoutExtent(cfg, fallback), Fantasy extent)` — Fantasy floor 1206×807 means switching never shrinks the OS window. **Keep Fantasy last** — default-picker, banner (`screen->setShowBanner(presetIndex == kMachinePresetCount - 1)`), and window-size floor all key off the terminal index.
  - Auto-enable by source dir of loaded file: `software/Graphic HGR/` → GEN2, `Graphic TMS9918/` → TMS9918, `software/Apple-1_TMS_CC65/` → TMS9918 (cc65 CodeTank drop-ins), `SOUND SID/` → A1-SID (evicts A1-AUDIO SE + Juke-Box per the menu mutex), `NET/` → Wi-Fi Modem (resets on reload), `sdcard/` → microSD, `a1io_rtc/` → A1-IO & RTC, `Graphic gt-6144/` → GT-6144. Each branch always raises the corresponding `show*` window flag (even when the card was already plugged) so loading from the folder pops the panel — required because the Fantasy preset leaves graphic cards unplugged by default.
- **Screen_ImGui** — 40×24 grid. Two `characterRenderMode` (Apple1Charmap via `roms/charmap.rom`, HostAscii). Three `monitorMode` tints (Green/Amber/Mono). CRT effect = `drawCRTBackdrop()` phosphor-tint + `drawCRTScanlines()` 1-px `AddRectFilled` every 2 display pixels at integer Y (default `crtScanlineAlpha = 0.50`). `AddLine` avoided — its AA on macOS GL 3.2 / WebGL2 splits sub-2-px thickness across two rows and halves alpha.
- **CassetteDeck_ImGui** — procedural (`ImDrawList`, no textures). Transport: REC alone = REC+PLAY, PAUSE latches only on Play/Rec, STOP releases, EJECT only from Stopped, REW/FF release PLAY. Forwards to `CassetteDevice` via `EmulationController`.

### Peripherals

- **CassetteDevice** — ACI. Woz ROM `$C100-$C1FF`, I/O `$C000-$C0FF` (`$C000` flip-flop, `$C081` input). Loads `.aci` (raw pulse-duration @ `kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ`), `.wav`, `.mp3`, `.ogg` via vendored miniaudio. Side-car `tapeinfo.txt`: `<dir>/tapeinfo.txt` lists `filename = load-range`; deck jaquette prints ready-to-type Wozmon command.
- **GraphicsCard** — Uncle Bernie's GEN2 HGR. Reads RAM `$2000-$3FFF`, renders 280×192 NTSC artifact colour. Apple II non-linear scanlines. `rasterizeLine` is a 14 KB LUT `kHgrPixels[(colParity << 8) | byte]` (no inter-byte neighbours), memcpy-copied; second pass paints white at 39 inter-byte seams when bit 6 ↔ bit 0 both lit. 9th index bit is `colParity` (absolute screenX parity), **not** group2.
- **TMS9918** — VDP, 16 KB VRAM, I/O `$CC00/$CC01`. `renderToBuffer()` → 256×192 RGBA (IM_COL32 = `GL_RGBA + GL_UNSIGNED_BYTE`); UI uploads via `glTexSubImage2D`.
- **AudioDevice** — miniaudio (desktop) / Web Audio ScriptProcessorNode (WASM). `AudioSource::fillAudioBuffer(float*, int)` mixes Cassette + SID mono float32. **`getActualSampleRate()` returns miniaudio-negotiated rate** (44.1 kHz requested, often 48 kHz on Apple Silicon) — cycle-driven sources must use this or tempo drifts. WASM always 44.1 kHz.
- **SID** — wraps **libresidfp** (GPL-2.0+, vendored). 6581/8580 swappable; swap rebuilds filter chain, restores last register state. I/O `$C800-$CFFF` (29 regs, `addr & 0x1F`); coexists with TMS9918 at `$CC00-$CC01` (VDP wins via priority 10). **A1-AUDIO SE**: same `pom1::SID` instance, second bus handle at `$CC00-$CC1F`. SID/SE evict each other; SE additionally evicts TMS9918. **Cycle-driven, SPSC-buffered**: `Memory::advanceCycles()` → `SID::advanceCycles(cycles)`; `chip->clock` runs in 4096-cycle chunks into a 16 384-sample ring (drop-oldest on overflow). Audio callback only drains. `chipMutex` serialises register writes / `clock` / `setChipModel` between UI + emulation threads. **WASM**: `-s STACK_SIZE=4194304` (4 MB) — filter-model tables overflow default 64 KB stack. Filter-model builders run **sequentially** under `__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__` (upstream `std::thread` throws `system_error 138` in single-threaded WASM); adds ~50 ms cold start.
- **MicroSD** — 65C22 VIA `$A000-$A00F`, SD CARD OS ROM 8 KB at `$8000-$9FFF`. Handshake: PORTB bit 0 = CPU_STROBE, bit 7 = MCU_STROBE, PORTA = data. Host `sdcard/` as virtual FAT32; tagged `NAME#TTAAAA` filenames encode type + load addr. Commands: `cmdRead`/`Load` (strict + fuzzy prefix), `Write`, `Dir`/`Ls`, `Cd` (only nav op — `..`, absolute `/PATH`, relative, fuzzy leaf), `Del`, `Mkdir`, `Rmdir`, `Pwd`, `Mount`. **All name-accepting commands resolve against `currentDirectory` only — no recursive search.** Wozmon prompt literally is the cwd. Pinned by `test_sdcard_subdir_navigation_telnet.py`. **VIA T2 timer** is decremented per-cycle (one-shot; underflow sets IFR bit 5) so SD OS 1.3 IEC frame timeouts work.
- **IECCard** — P-LAB IEC daughterboard for microSD. **No new MMIO window**: piggybacks on microSD's VIA via PORTB bits 2 (ATN_OUT), 3 (CLK_OUT), 4 (DATA_OUT), 5 (CLK_IN), 6 (DATA_IN). SN7406 inverts the OUT bits; IN bits track bus level directly. Wired-AND open-collector model in `IECBus`; byte-frame FSM in `IECCard` (cycle-driven for talker; edge-driven for listener); virtual 1541 in `Drive1541` backed by `D64Image` reading/writing standard 174 848 B `.d64` files (`disks/iec/dev8.d64`). Cascade: enabling IEC auto-enables microSD; disabling microSD cascades-disables IEC (mirror of CodeTank/TMS9918). Firmware: SD CARD OS 1.3 (`nippur72/apple1-sdcard`, CC BY 4.0) — already shipped at `roms/sdcard.rom`. User-facing surface: `@DEV/@$/@DIR/@L/@S/@R/@BL/@BR/@BS/@ERR/@CMD`. MVP = device 8 only.
- **CFFA1** — 8 KB ROM `$9000-$AFDF` (ID `CF`/`FA` at `$AFDC`/`$AFDD`), ATA/IDE regs `$AFE0-$AFFF` (A4 not decoded — `$AFE0` mirrors `$AFF0`). Backs ProDOS `.po`; READ/WRITE SECTOR + SET FEATURE only. Auto-mount probes `cfcard/cfcard.po` up three dirs. Two bus entries: read-only ROM + r/w regs.
- **JukeBox** — Parmigiani & Rosselli. Chip modes: **Flash** (paged read-only, 16 KB–512 KB) / **EEPROM 28c256** (32 KB single-page, writable). ROM at `$4000-$BFFF` (RAM-16/ROM-32 jumper) or `$8000-$BFFF` (RAM-32/ROM-16); Program Manager `$BD00`, Save Program `$B800`. **Bank-select latch `$CA00`**: bits 0-3 = Px page, bit 4 = Sx sub-page. Bus: priority 20 ROM window + priority 15 `$CA00`. Mutex with CodeTank, CFFA1, microSD, Krusader, Wi-Fi Modem, A1-SID; A1-AUDIO SE coexists. Boot page = lowest with `$A5` at file offset `$7D00`. Pinned by `jukebox_paged_rom_smoke`.
- **CodeTank** — P-LAB ROM **daughterboard** of the TMS9918 Graphic Card (split from JukeBox April 2026). No standalone edge connector / address decoder on the Apple 1 bus. `Memory::setCodeTankEnabled(true)` auto-plugs TMS9918; `setTMS9918Enabled(false)` cascade-unplugs CodeTank. Single 32 KB 28c256, jumper → 16 KB half at `$4000-$7FFF`; no `$CA00`, read-only. ROM probe order: `roms/codetank/Codetank_GAME1.rom`, then `codetank.rom` / `roms/codetank.rom`.
  - **Shipped library** (`tools/build_codetank_rom.py` produces 4 ROMs in `roms/codetank/`):
    - `Codetank_GAME1.rom` — lower = menu($4000) + Galaga($4100) + Sokoban($6200) + Snake($7600); upper = TMS_LOGO V2.6 ($4000-$7FFF). Default boot ROM.
    - `Codetank_GAME2.rom` — lower = TMS_Rogue full bank; upper = TMS_Nyan_CodeTank full bank (drop-in `software/Apple-1_TMS_CC65/TMS_Nyan_CodeTank.bin`, padded to 16 KB).
    - `Codetank_GAME3.rom` — lower = Tetris/CodeTank full bank (drop-in `software/Apple-1_TMS_CC65/tetris_codetank.bin`); upper = menu($4000) + Life($4100) + Mandel($4900) + Plasma($5100).
    - `Codetank_TEST.rom` — lower = TMS_SilBench (29-test silicon-validation suite, regression fixture for May 2026 silicon model); upper = menu($4000) + Clone($4100) + Split($4500) (silicon-bug demos: sprite clone bug N.8 / 5th-sprite palette split).
    - All slots silicon-strict-clean via `tools/silicon_strict_patch.py`; background in `dev/SILICONBUGS.md` section 17. PROCBSS in Parmigiani RAM `$E000-$EFFF` for Logo.
    - Per-cart menus live in `dev/projects/tms9918_codetank_{menu,game3_menu,test_menu}/`.
  - **UI / bus**: **File → P-LAB CodeTank Library** scans `roms/codetank/*.{rom,bin}`; priority 20; mutex with Juke-Box; always with TMS9918. Preset 8 plugs both. Tests: `codetank_smoke`, `codetank_tms9918_dependency`.
  - **Silicon padding**: `dev/lib/tms9918/tms9918_pad.asm` + patcher inject `JSR tms9918_pad12`; macros / auto-link details → same SILICONBUGS section and `build_codetank_rom.py` / `emit_woz.py`.
- **A1IO_RTC** — 65C22 VIA `$2000-$200F` (⚠ overlaps GEN2 — preset-level mutex). Emulated ATMEGA32 drives DS3231, DS18B20, 8 analog + 4 digital inputs, 16-bit shift-register output. 24 regs broadcast on 100-cycle period with PORTB STROBE.
- **WiFiModem** — 65C51 ACIA `$B000-$B003`, ESP8266 AT (`AT`, `ATDT host:port`, `ATH`, `ATE0/1`, `ATI`, `ATZ`). TELNET IAC filtering + `CR LF→CR` strip, non-blocking TCP, baud 50-19200, `+++` 1 s guard, 4096 B circular Rx. `requestDisconnect()` is the UI-safe entry. Desktop only; WASM stubs return `NO CARRIER`.
- **TerminalCard** — passive bridge sniffing `$D012` writes, injecting keys at `$D010`/`$D011`. TCP server IPv4 loopback :6502 (IPv6 `::1` refused). Modes: 7-bit (CR→CRLF, optional uppercase via Ctrl-O/I) and 8-bit raw (Ctrl-T). Controls Ctrl-L clear, Ctrl-R reset; ESC-prefixed alternates (ESC T/O/L/R/I) for ttys that eat Ctrl. **Ctrl-S / ESC S — Screenshot**: arms `screenshotPending`; main render thread runs `glReadPixels` between `RenderDrawData()` and `glfwSwapBuffers()`, Y-flips, writes `screenshots/pom1_latest.png`; emits `\r\n[SCREENSHOT: /abs/path]\r\n` on next `advanceCycles`. Result string uses own `screenshotResultMutex` so render thread never blocks on `cardMutex`. Unknown ESC sequences forward the ESC. On accept: `IAC WILL ECHO` + `IAC WILL SUPPRESS-GO-AHEAD` + `IAC DO SUPPRESS-GO-AHEAD` flips client to character-at-a-time. Pending reset/clear via `std::atomic<bool>` consumed **outside** `stateMutex`. Desktop only.
- **SocketHandle** — move-only RAII (`NativeSocket` = `SOCKET`/`int`) shared by WiFiModem + TerminalCard.
- **GT6144** — SWTPC 64×96 mono on 6× Intel 2102, write-only `$D00A` (PIA `A3` chip-select). 4-phase FSM: `0..63` latch X + pixel OFF; `64..127` latch X + pixel ON; `128..223` commit Y; `224..255` control opcode (`byte & 0x07`: 0=INVERTED, 1=NORMAL, 4=UNBLANK, 5=BLANK, 7=NORMAL alias; bits 3-4 don't-cares so `224/232/240/248` all mean INVERTED). Inversion + blanking in render path — framebuffer untouched (matches analog XOR). Bus `{0xD00A,0xD00A}` priority 0 — `memWrite` dispatches before PIA aliasing; reads fall through (no read-back on real hardware). Power-on framebuffer seeded with `mt19937` for Intel 2102 bistable noise; reseeds on every `setGT6144Enabled(true)`. **Display aspect**: 64×96 fed a 4:3 CRT, SWTPC describes pixels as 2:1 W:H. Render at native 64×96 but pass `displayAspectW = kWidth * 2` to `layoutFitVideoViewport` — GL_NEAREST does 2× horizontal stretch. Don't rewrite to square pixels.
- **PR40Printer** — SWTPC PR-40 per Jobs' *Interface Age* Oct-76 mod. **No MMIO** — third sniffer on `$D012` (after `DisplayDevice::onChar` + `TerminalCard::onDisplayWrite`). 40-char FIFO, flush on CR or full; arms `kMechCycleCpu = POM1_CPU_CLOCK_HZ * 4 / 5` (~0.8 s) decremented from `advanceCycles()`. DPDT switch: **Off** = disconnected; **Mixed** = Jobs' 2-pos, PB7 = video-busy OR printer-busy; **PrintOnly** = community 3-pos, PB7 = printer-busy alone. Busy-OR merge inline in `memRead($D012)`. Paper roll = unbounded `vector<string>`; UI uses `ImGuiListClipper`. Pinned by `pr40_printer_smoke`.

## Key implementation details

### MMIO

- `$D010` (KBD) — last key with bit 7 set; read clears strobe.
- `$D011` (KBDCR) — bit 7 = 1 when key ready.
- `$D012` (DSP) — write triggers display callback + busy-counter except **raw `$7F`** (WOZ Monitor's reset-time `LDY #$7F / STY $D012` DDR setup, would paint a spurious `_` before `\` prompt). Real hardware latches only on PB7=1; POM1 is permissive so emulator-era demos (e.g. `software/tms9918/demo.bin`) calling WOZ ECHO with bit 7 clear still render — keep this. `$DF` via ECHO still yields `_` (`& 0x7F = $5F`); filter only drops the DDR write. TerminalCard + PR-40 sniffers get every write unfiltered. Read: bit 7 = 0 (ready) after terminal-speed delay, else 1 (busy). PR-40 busy OR-merged on read when switch ≠ Off.
- PIA 6821 incomplete decoding aliases `$D0xx` → `$D010-$D012` by low 2 bits. `memRead`/`memWrite` normalise before dispatch. Keyboard forced uppercase via `setKeyPressed`; Terminal Card uses `setKeyPressedRaw()`.

### CPU execution

Three modes via EmulationController: **Stopped**, **Running** (`executionSpeed` cycles/frame — 1× = 17045, 2× = 34091, MAX = 1 000 000), **Step**. All addressing modes store resolved address in `op` (quint16); `Imm()` stores `programCounter`. Every instruction goes through `memory->memRead(op)` / `memWrite(op, value)` uniformly.

### Loading programs

- `Memory::loadBinary(filename, startAddress)` — raw.
- `Memory::loadHexDump(filename, startAddress)` — Wozmon hex. Supports comments (`//` `#` `;`, strips inline — prevents `LDA`/`DEX` mnemonics being parsed as data), continuation lines, `T` prefix (turbo), `X` marker, `R` suffix (run address). Handles single-line files where data merges with addresses (`ED0300:` → data `ED` + addr `0300`).
- File dialogs export memory range as binary or Woz hex.
- Clipboard paste feeds keyboard (4096-char cap).

### Out-of-range RAM

Preset `ramKB < 64` tracks accesses in `[ramKB*1024, $8000)` as OOR. **Strict mode** returns `$FF` and drops writes — matches real Apple-1 with no RAM board. Status bar: `OOR:N!` (strict + counter) or `[strict]`.

## Memory Map

```
$0000-$00FF  Zero page
$0100-$01FF  Stack
$0200-$1FFF  User RAM (programs typically load at $0280 or $0300)
$2000-$200F  A1-IO RTC VIA 65C22 (mutex with GEN2 below)
$2000-$3FFF  GEN2 HGR framebuffer (8 KB)
$4000-$7FFF  CodeTank ROM (16 KB half of 32 KB 28c256; daughterboard of TMS9918, mutex with Juke-Box)
$4000-$BFFF  Juke-Box ROM (RAM-16/ROM-32 jumper; up to 512 KB paged via $CA00)
$6000-$7FFF  Applesoft Lite SD ROM (microSD preset, cold-start `6000R`)
$8000-$BFFF  Juke-Box ROM (RAM-32/ROM-16 jumper; Sx bit 4 of $CA00)
$8000-$9FFF  SD CARD OS ROM (microSD; same EEPROM serves the IEC daughterboard)
$9000-$AFDF  CFFA1 firmware ROM
$A000-$A00F  microSD VIA 65C22
$AFE0-$AFFF  CFFA1 ATA/IDE regs (A4 undecoded; ID at $AFDC/$AFDD)
$B000-$B003  MODEM BBS ACIA 65C51
$C000-$C0FF  ACI I/O ($C081 input, $C000 flip-flop)
$C100-$C1FF  Woz ACI ROM
$C800-$CFFF  A1-SID (29 regs, & 0x1F)
$CA00        Juke-Box Px/Sx bank-select latch (write-only; mutex with SID)
$CC00-$CC1F  A1-AUDIO SE (excludes TMS9918)
$CC00/$CC01  TMS9918 DATA / CTRL (priority 10, wins over A1-SID)
$D00A        SWTPC GT-6144 command port (write-only; bus wins over PIA alias)
$D010-$D012  KBD / KBDCR / DSP (alias $D0F0/F1/F2, $D030/31/32, …)
$E000-$EFFF  Integer BASIC ROM
$FF00-$FFFF  Woz Monitor ROM + vectors ($FFFA-$FFFF)
```

## Platform notes

- **CMake** — `find_package(glfw3 CONFIG)` first (vcpkg, Homebrew), falls back to `pkg_check_modules`.
- **Windows** — VS C++ workload + CMake + Git + vcpkg. MSVC: `/utf-8`, `_CRT_SECURE_NO_WARNINGS`. `package_windows_release.bat` → release ZIP.
- **macOS** — links Cocoa + IOKit + CoreVideo. `package_macos_release.sh` puts read-only assets in `POM1.app/Contents/Resources/`. At startup, `pom1_macos_provision_user_data_dir()` creates `~/Library/Application Support/POM1/` with symlinks for read-only dirs + seeded `sdcard/`/`cfcard/`/`ini/`, then chdirs there. Symlinks refresh each launch (handles Gatekeeper App Translocation + `/Applications` drag-installs). Dev flow falls back to bundle-parent chdir when `Contents/Resources/roms` absent. DMG carries `.VolumeIcon.icns` + `SetFile -a C`.
- **Linux** — `setup_imgui.sh` supports apt, dnf, pacman.

`build/`, `build-wasm/`, `imgui/` gitignored.

## Testing

`ctest` from `build/` (native-only, opt-out `-DPOM1_ENABLE_TESTS=OFF`). Inventory lives in `tests/CMakeLists.txt` — after configure, `ctest -N` lists exact names. CMake never invokes `dev/projects/*/Makefile`; those are developer-only build steps. Tests load whatever artefact happens to live under `software/`.

```bash
ctest                       # full suite (~5–30 s wall time; Klaus + TMS9918 tests dominate)
ctest --output-on-failure
ctest -R klaus -V
```

- **`klaus_6502_functional`** — [Klaus Dormann's 6502 test](https://github.com/Klaus2m5/6502_65C02_functional_tests) vs M6502. SHA-256-pinned download. `setTestMode(true)` (flat RAM), `PC = $0400`, step until `JMP *`; success = final PC `$3469`. Gates all CPU refactors.
- **`preset_ram_profiles_smoke`** — parses `MainWindow_Presets.cpp`: `ramKB`, `BasicType`, and Fantasy/non-Fantasy presets obey the documented rules (see test source).
- **`memory_dualram_smoke`** — 8 KB dual-bank strict map: `$0000-$0FFF` + `$E000-$EFFF` writable; gap OOR-strict (`$FF` reads).
- **`reset_vectors_smoke`** — `Memory::configureResetVectors(addr)` only writes `$FFFC/$FFFD` (RES). NMI (`$FFFA/$FFFB`=$0F00) and IRQ (`$FFFE/$FFFF`=$0000) stay at the authentic WozMonitor.rom values so P-LAB programs installing an IRQ trampoline at `$0000` route correctly.
- **`d64_parse_smoke`** — D64Image: 35-track geometry (21/19/18/17 sectors), wildcard match, `format()` → `writeFile` → `readFile` → `deleteFile` round-trip + save/mount persistence.
- **`iec_bus_wired_and_smoke`** — IECBus open-collector wired-AND truth table: any device pulled = LOW; both released = HIGH.
- **`iec_snapshot_smoke`** — IEC daughterboard round-trips through `saveSnapshot`/`loadSnapshot`. Pins `kFlagIECCard` (bit 15) + cascade `kFlagMicroSD` + `Drive1541` error-channel survival.
- **`peripheral_bus_smoke`** — page-mask miss, read routing, SID↔TMS9918 priority, `setEnabled` round-trip, sniffer pass-through.
- **`sid_audio_smoke`** — cycle-driven SID ring non-silent after bus writes + CPU clocks.
- **`aci_tape_loading`** / **`aci_tape_saving`** — load path on `cassettes/APPLE50TH.ogg` (`${CMAKE_SOURCE_DIR}` cwd); save `.aci`/`.wav` round-trip.
- **`pr40_printer_smoke`** — DPDT / PB7 merge, FIFO, mechanical delay.
- **`gt6144_smoke`** — FSM, inversion-without-SRAM-touch, power-on noise uniqueness.
- **`jukebox_paged_rom_smoke`** — `roms/jukebox.rom`, `$CA00`, flash vs EEPROM behaviour.
- **`codetank_smoke`** / **`codetank_tms9918_dependency`** — ROM size, jumper offsets, read-only, TMS9918 cascade.
- **`tms9918_sprite_status`** / **`tms9918_silicon_strict_runtime`** / **`tms9918_per_scanline`** / **`tms9918_advanced_silicium`** — VDP behaviour + strict timing pins (`dev/SILICONBUGS.md`).
- **`snapshot_smoke`** — `SnapshotIO` + selected card flags / RAM round-trip; extend when peripherals gain `serialize`.
- **`hex_dump_multi_zone`** — `Memory::loadHexDump` disjoint zones (e.g. dual-bank `.txt` with a `$0280` lo block + `$E000` hi block).
- **`cpu_breakpoint_smoke`** — `--break <addr>` halt-before-instruction, clear, `hardReset` disarm (see [`doc/CLI.md`](doc/CLI.md)).

New invariant tests follow `tests/peripheral_bus_smoke_test.cpp` — `<cassert>` + `add_test` suffices; GTest/Catch2 only once multi-threaded tests land.

**Manual telnet tests** in `tools/test_*_telnet.py`. `test_sdcard_subdir_navigation_telnet.py` pins SD CARD OS "commands only search `currentDirectory`" — run from repo root.

## Version string locations

Bump version in **all**:
- `src/main_imgui.cpp` (console + window title)
- `src/MainWindow_Dialogs.cpp` (About — 2 occurrences, with-photo + no-photo branches)
- `src/Screen_ImGui.cpp` (welcome)
- `build-wasm/shell.html` (3 occurrences: `<meta>`, `<title>`, `<h1>`)
- `README.md` (title)
- `CMakeLists.txt` (`MACOSX_BUNDLE_BUNDLE_VERSION` + `MACOSX_BUNDLE_SHORT_VERSION_STRING`)
- `package_windows_release.bat` (ZIP filename)
- `package_macos_release.sh` (`VERSION="…"` → DMG filename)
- `packaging/linux/build_appimage.sh` (`VERSION="${POM1_VERSION:-…}"` fallback)
- `packaging/windows/README.txt` (header)
