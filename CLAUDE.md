# CLAUDE.md

Architecture / invariants / gotchas for the **emulator side** of POM1. User-facing walkthrough → `README.md`; open work → `TODO.md`; shipped history → `git log`.

For **Apple 1 software** (games, BASIC, SID tunes, microSD shell tools) see **`APPLE1DEV.md`** → `doc/Programming_Apple1_ASM.md` (Arnaud's 6502/cc65/HGR/TMS9918 deep-dive).

## Project Overview

Apple 1 emulator (Dear ImGui) of the MOS 6502 + core (display, keyboard, ACI cassette) + expansion cards: Uncle Bernie's GEN2 HGR, P-LAB A1-SID (6581/8580), P-LAB TMS9918, P-LAB microSD (65C22+ATMEGA), P-LAB MODEM BBS (65C51+TCP), P-LAB Terminal Card, P-LAB A1-IO & RTC (65C22+ATMEGA32+DS3231), Rich Dreher's CFFA1 (ATA/IDE+ProDOS), P-LAB Juke-Box, SWTPC GT-6144 (1976 — first commercial Apple-1 graphics card, 64×96 mono). English UI. Linux / macOS / Windows / Web (Emscripten).

## Build & Run

```bash
./setup_imgui.sh             # one-time deps (Linux/macOS)
cd build && cmake .. && make # build → build/POM1
./run_emulator.sh            # runs from repo root; copies ROMs first
```

Windows: `setup_imgui.bat` + vcpkg + `cmake --build . --config Release`. `CMAKE_EXPORT_COMPILE_COMMANDS` on (`compile_commands.json` symlinked for clangd).

### CLI flags (`CliDispatcher.cpp`, invoked from `main_imgui.cpp`)

Every GUI-reachable action has a flag. Three phases: **A** boot-time (before GLFW window), **B** first-frame preset overrides, **C** deferred — fire once the 15-frame card plug-in completes, in command-line order. `--load 0300:foo.bin --run 0300 --paste keys.txt` composes naturally. Malformed verbs log `[CLI] ERROR: …` and exit 1.

| Flag | Phase | Effect |
|------|-------|--------|
| `--list-presets` | A | Print `index: name` for `kMachinePresets[]` and exit. |
| `--preset <N\|name>` / `-p` | A | Select by index or case-insensitive substring (first match). |
| `--terminal` | A | Force-enable Terminal Card (`127.0.0.1:6502`). |
| `--tape <path>` | A | Preload tape + auto-Play. Default probe: `cassettes/WOZ_talk.mp3` (silent-loaded). |
| `--save-tape <path>` | A | Dump deck capture on clean shutdown. SIGINT/SIGTERM → `glfwSetWindowShouldClose(1)` so `~MainWindow_ImGui` runs. |
| `--save-tape-format <aci\|wav>` | A | Append extension when `--save-tape` has none. Existing `.aci`/`.wav` wins; `CassetteDevice::saveTape()` picks format by extension. |
| `--cpu-max` | A | Pin `executionSpeed = 1 000 000` cycles/frame (MAX). Scripted ACI tests need this. |
| `--speed <cycles/frame>` | B | Override `executionSpeed`. 17045 = 1×, 34091 = 2×. `--cpu-max` beats `--speed`. |
| `--enable <card>[,…]` / `--disable <card>[,…]` | B | Rewrite deferred plug list. Names: `aci, sid, sid-se, microsd, tms9918, a1io-rtc, hgr, cffa1, krusader, wifi, terminal, jukebox, pr40, gt6144` (aliases in `kCardNames`). Mutual-exclusion (SID↔SID-SE, TMS9918↔SID-SE, …) enforced at override. `--disable krusader` is a no-op (ROM unload needs hard reset). |
| `--sid-chip <6581\|8580>` | B | Swap A1-SID / SE chip model; libresidfp replays last register state. |
| `--jukebox-jumper <ram16\|ram32>` | B | `RAM16_ROM32` → ROM `$4000-$BFFF`; `RAM32_ROM16` → ROM `$8000-$BFFF`. |
| `--trace-brk` | C | Enable M6502 BRK reg/stack/trace dump. |
| `--load <addr>:<path>` | C | Load raw binary, rewrite reset vector, `hardReset` + `start`. Addresses: hex (`0300`, `0x0300`, `$0300`) or decimal ≥ 5 digits. Repeatable. |
| `--run <addr>` | C | `EmulationController::jumpTo()` — stops, rewrites reset vector, hard-resets, restarts. |
| `--paste <file>` | C | Up to 4096 chars into keyboard queue; same filter as clipboard (`\n`→CR, printable ASCII). |
| `--step <N>` | C | Step N instructions after deferred plug. Pairs with `--trace-brk`. |
| `--play` / `--rec` / `--rewind` | C | Cassette transport. `--rec` uses `CassetteDevice::armRecording()` (no wait for `$C000` toggle). |
| `--sd-mkdir <path>` | C | `create_directories(sdcard_root/<path>)`. Escape paths rejected. SD root probed independently of card plug state. |
| `--sd-put <host>:<guest>` / `--sd-get <guest>:<host>` | C | Fixture seeding without Wozmon round-trip. |
| `--rtc-freeze "YYYY-MM-DD HH:MM:SS"` | C | Set `A1IO_RTC::rtcOffsetSeconds`. Clock keeps ticking at host rate — fine for sub-minute runs. |
| `--break <addr>` | — | **Reserved**, errors out: no breakpoint infra in M6502. Use `--step` + `--trace-brk`. |

`--enable`/`--disable` run after `--terminal` (so `--disable terminal` overrides `--terminal`). Case-insensitive CSV, repeatable.

Typical telnet workflow:

```bash
./POM1 --list-presets
./POM1 --preset 10 --terminal &
sleep 3 && python3 tools/test_jukebox_telnet.py
```

Two tests launch POM1 themselves and must run from repo root (not `build/`): `test_aci_telnet.py`, `test_sdcard_subdir_navigation_telnet.py`.

### WASM build

```bash
source /path/to/emsdk/emsdk_env.sh
mkdir -p build-wasm && cd build-wasm
emcmake cmake .. && emmake make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
emrun POM1.html
```

Outputs `POM1.{html,js,wasm,data}`. `.data` is Emscripten **MEMFS** preload (rules in `CMakeLists.txt` under `if(POM1_IS_WASM)`): mounts `roms/ pic/ fonts/ software/ sdcard/ cassettes/` same-name and `cfcard/cfcard.po` (single file — other `.po` extras are desktop-only, would add >140 MB). Rebuild WASM after any change under those trees or `build-wasm/shell.html`.

POST_BUILD copies `pic/icon.png` to `build-wasm/pic/icon.png` as an HTTP-visible twin for `<link rel="icon">` — the MEMFS copy is unreachable by the browser's favicon loader.

Desktop `Memory` ctor probes `sdcard`, `../sdcard`, `../../sdcard` (cwd-relative) and `cfcard/cfcard.po` similarly.

### Assembling programs (cc65)

| Config | Purpose |
|--------|---------|
| `software/apple1.cfg` | Standard Apple 1 (text, no framebuffer) |
| `software/hgr/apple1_gen2.cfg` | GEN2 HGR — reserves `$2000-$3FFF` |
| `software/games/apple1_sok_{4k,8k,hgr}.cfg` | Sokoban (stock 4K / 8K+TMS / 8K+GEN2) |

Sokoban configs define `LEVELBUF` (zp `$0020`) and `STATEGRID` (bss `$0F00`/`$1F00`). Use `.segment "LEVELBUF": zeropage` to force `zp,X` addressing.

## Architecture

Each `.cpp/.h` pair owns one concern. Only non-obvious bits are called out.

### Parmigiani's golden rule — "one board at a time"

Claudio PARMIGIANI, designer of the P-LAB Apple-1 family, states **"one board at a time"**: on real hardware exactly ONE P-LAB card is plugged at any given moment. The 6502 bus has no arbitration, and many P-LAB cards deliberately overlap address windows (A1-SID `$C800-$CFFF` vs TMS9918 `$CC00/$CC01`; A1-IO RTC `$2000-$200F` vs GEN2 HGR `$2000-$3FFF`; Juke-Box claims `$4000-$BFFF` entirely).

POM1 **breaks this rule on purpose** in the "Multiplexing Fantasy" presets (#12, #14) — literally a *fantasy*, not a buildable machine. The mutual-exclusion rules elsewhere (SID↔SID-SE, TMS9918↔SID-SE, GEN2↔A1-IO, Juke-Box↔{CFFA1, microSD, Krusader, Wi-Fi Modem}) mirror real bus conflicts. When adding a card, honour the rule and document any intentional coexistence as a POM1-only liberty.

### Core

- **M6502** — 6502 CPU. `op` is `quint16` for 16-bit address handling; `tmp` is `int` for carry/borrow via bit 8. BCD ADC propagates the low→high carry from `(accumulator & 0xF0)` (bit 4 of the BCD-adjusted accumulator), not the unadjusted sum. **`run(maxCycles)` returns actual cycle count** — overshoots by up to 6 cycles per iteration (budget check is between instructions); wallclock pacers must deduct returned value. 12-slot PC ring always on; `dumpPcTrace(tag)` flushes. `setDebugBrkTrace(true)` dumps regs/stack/trace on every BRK. `setProgramCounter(pc)` is the Klaus harness back-door.
- **CpuClock.h** — `POM1_CPU_CLOCK_HZ = 1 022 727` (14.31818 MHz ÷ 14). Exports `..._CYCLES_PER_FRAME_{1X,2X}_60HZ` / `..._PER_MILLISECOND`. Used by EmulationController, SID, TMS9918 tick, modem baud + `+++` guard, Terminal Card poll, microSD idle, cassette timebase, terminal display delay.
- **Memory** — 64 KB. Owns every peripheral (`unique_ptr` + enable flag). MMIO via `PeripheralBus`; `memRead`/`memWrite` handle only PIA 6821 + `$D0xx` aliasing, ROM write-protect (`writeInRom`), OOR strict mode, the cassette write-sniffer, `DisplayDevice::onChar` forwarding + TerminalCard hook, then raw `mem[]`. `setKeyPressedRaw()` bypasses forced-uppercase (Terminal Card). **`setTestMode(true)`** collapses everything to `mem[address]` — flat 64 KB for Klaus harness, never in production. **Redundant-ROM-load guards**: `loadApplesoftLiteSDCard` skips WOZ reload when `mem[$FF00..$FF01] == D8 58` (WOZ `CLD/CLI`); `setMicroSDEnabled(true)` skips SD CARD OS reload when `mem[$8000..$8001] == A9 00`.
- **DisplayDevice** — abstract `onChar(char)` sink for `$D012` writes. Injected via `Memory::setDisplayDevice()`. Replaces old C-callback + atomic singleton so tests/peripherals can tee output.
- **PeripheralBus** — central MMIO dispatch. Peripherals register `(name, range, priority, onRead, onWrite)` at `Memory` ctor. Hot path O(1): `pageMask[256]` bitmap pre-computed on register/setEnabled → `tryRead`/`tryWrite` read one byte, branch on zero (miss = 99 %+). `std::stable_sort` by priority so ties respect insertion order (TMS9918 wins over SID at `$CC00/$CC01` via priority 10). `onWrite = {}` = pass-through to RAM; explicit no-op = block (CFFA1 ROM). Cassette write-toggle stays inline in `memWrite` (sniffer — byte must still land in RAM).

### Emulation orchestration

- **EmulationController** — façade: M6502 + Memory + emulation thread. ~55 public methods. **Mutex order**: `stateMutex > keyboard.keyMutex > publisher.snapshotMutex`. `stateMutex` is a `PriorityMutex` (mutex + waiter count) — at MAX speed the emulation loop yields after each slice iff `hasWaiters()`. Slice cap: `kMaxSliceCycles = 6000` desktop (~50 µs hold @ ~120 MHz emulated), 50 000 WASM (single-threaded, burns frame budget per pump). Native = dedicated `emulationThread`; WASM = `pumpEmulationMainThread()` from the main loop.
- **EmulationSnapshot** — UI-ready immutable picture. Sized once at startup so `SnapshotPublisher::publish()` is in-place memcpy; no per-frame alloc.
- **SnapshotPublisher** — SPSC slot. `publish()` runs under stateMutex + own `snapshotMutex` to serialize UI `copyTo()`. **Page-level dirty copy**: `memWrite` sets one bit per 256 B page in `dirtyPages`; `publish()` walks bitmap, collapses contiguous runs into single memcpy. Running program ≈ 4-8 pages/frame → 1-2 KB vs 64 KB. Idle Wozmon = zero copy. TMS9918's 16 KB skipped when unplugged.
- **KeyboardController** — SPSC queue. `queueKey()` is UI-safe; `drainTo(Memory&)` runs under stateMutex, uses `std::swap` to release `keyMutex` before `Memory::setKeyPressed()`.
- **RomLoader** — six static helpers (`reloadBasic` / `ApplesoftLite` / `WozMonitor` / `Krusader` / `AciRom` / `CFFA1Rom`) factoring the toggle-`writeInRom` + `Memory::loadXxx()` + restore pattern.
- **Disassembler6502** — standalone `pom1::disassemble6502(mem, pc, instrLen)` for debug console. 256-entry table, no UI deps.
- **Logger** — levelled (`pom1::log().info("Tag", "msg")`). `StreamLogger` → stdout (Debug/Info) / stderr (Warn/Error); `RingBufferLogger` for in-app console; `TeeLogger` chains sinks. `pom1::initDefaultTeeLogger()` installs `Tee(stream, uiRingBuffer())`. Thread-safe.

### UI (ImGui)

- **main_imgui.cpp** — GLFW/OpenGL3 init (GL 3.2 Core / GLSL 150; WebGL 2 / GLSL ES 300 on WASM). `GLFW_OPENGL_FORWARD_COMPAT` is macOS-only. SIGINT/SIGTERM → `glfwSetWindowShouldClose(1)` so `--save-tape` flushes. **OS window icon**: desktop probes `pic/icon.png` via `find_app_icon_path()` (cwd / `../` / exe-relative), pushes via `glfwSetWindowIcon`; `#if !defined(__APPLE__)` (macOS pulls icon from `.app` bundle).
- **MainWindow_ImGui** — single class across **9 TUs** sharing helpers via `MainWindow_Internal.h` (`pom1::mainwindow::detail`): `MainWindow_ImGui` (ctor/dtor, render, CPU control), `_Layout`, `_Presets` (`kMachinePresets[]`; migration target for external `presets.json`), `_Menu`, `_Dialogs` (About/Thanks/Welcome/Hardware/Display — texture loaders `ensureAboutPhotoTexture`/`ensureApple50LogoTexture`/`ensureAppIconTexture`, released in `destroyPom1()`), `_HardwareWindows` (GraphicsCard / TMS9918 / WiFiModem / TerminalCard / A1IO_RTC / Juke-Box; TMS9918 GL texture 256×192 RGBA `GL_NEAREST`), `_FileDialogs` (auto-enable-by-directory), `_DebugWindows`, `_Keyboard`.

  Keyboard path bypasses `InputQueueCharacters` — keys go straight GLFW → Apple 1. `handleGlfwKey` tags PRESS/REPEAT into `nextCharIsRepeat` for the following `handleGlfwChar`. With Hardware → *Keyboard autorepeat* off (default, matches TTL keyboards), REPEAT drops printable chars + Enter/Backspace/Escape. F7 step honours REPEAT for hold-to-step.

  **`applyMachineConfig(int)` invariants**:
  - Unplugs every card, optionally `hardReset()` (skipped on first invocation — `Memory::Memory()` already initialised), sets UI flags immediately, then queues deferred plug via `pendingCardEnableFrames = kCardEnableDeferFrames` (15 frames ≈ 200 ms). Deferring past the first CPU cycle fixes otherwise-silent cards on boot (original symptom was a silent SID).
  - **ROM selection uses preset config, not live flags** — microSD Applesoft Lite (`$6000-$7FFF`) vs CFFA1 flavour (`$E000-$FFFF`) picked from `cfg.microSD`/`cfg.cffa1` directly. `reloadApplesoftLite()` would misroute (microSD/cffa1 Enabled still false at ROM-load time — deferred).
  - `applyPendingLayout(const char*)` runs before each `Begin()` with **`ImGuiCond_FirstUseEver`** — preset ships defaults, user drags persist to `imgui.ini` and win on subsequent launches (delete to restore). **Widgets must not call `SetNextWindowSize(..., FirstUseEver)` themselves** — that runs after `applyPendingLayout` and overrides preset. `SetNextWindowSizeConstraints` is OK.
  - Default POM1 preset = last entry of `kMachinePresets[]` (*POM1 Multiplexing Fantasy (2026)*; picked via `kMachinePresetCount - 1`). Shipped layout: Screen (10,61) 843×701 / Welcome (858,61) 338×223 / Cassette Deck (858,288) 338×476, mirrored in `build/imgui.ini`. Preset flips `showCassetteDeck = true` + `showWelcome = true`. **When adding a new preset, keep POM1 Fantasy last** — the default-picker uses the terminal index, and it also drives the banner (`screen->setShowBanner(presetIndex == kMachinePresetCount - 1)`).
  - First frame only: `computePresetLayoutExtent(cfg, fallback)` → `glfwSetWindowSize` (POM1 default 1274×801; same as `glfwCreateWindow` hint to avoid resize flash). WASM mirrors this in `getWasmCanvasPixelSize`.
  - Auto-enable by source dir of loaded file: `software/hgr/` → GEN2, `software/tms9918/` → TMS9918, `software/net/` (or `/wifi/`) → Wi-Fi Modem (also `wifiModemReset()` on reload), `sdcard/` → microSD, `software/a1io_rtc/` → A1-IO & RTC, `software/gt-6144/` → GT-6144 (case-sensitive match on both lower- and upper-case `GT-6144/`). **`software/sid/` is intentionally NOT in the list** — SID auto-plug interacts badly with the audio-mixer + bus state machine across hardReset (UI flag vs Memory state can desync → card plugged-but-silent). SID programs rely on a preset with `sid=true`.
- **CassetteDeck_ImGui** — procedural widget (`ImDrawList`, no textures; Apple 50th logo is optional blit). Transport interlocks: REC alone = REC+PLAY, PAUSE latches only on Play/Rec, STOP releases, EJECT only from Stopped, REW/FF release PLAY. Forwards to `CassetteDevice` via `EmulationController` — deck is UI only.
- **Screen_ImGui** — 40×24 grid. Two `characterRenderMode` (Apple1Charmap via `roms/charmap.rom`, HostAscii via ImGui font). Three `monitorMode` tints (Green/Amber/Monochrome). CRT effect = two passes: `drawCRTBackdrop()` phosphor-tint rows before glyphs; `drawCRTScanlines()` 1-px `AddRectFilled` every 2 display pixels at integer Y (default `crtScanlineAlpha = 0.50`, batched). `AddLine` avoided — its AA on macOS OpenGL 3.2 / WebGL2 splits sub-2-px thickness across two rows and halves effective alpha. Integer Y pins each dark row to a real pixel so the pattern doesn't drift.
- **MemoryViewer_ImGui** — hex editor with color-coded regions, search, bookmarks, inline editing.

### Peripherals

- **CassetteDevice** — ACI. Woz ROM `$C100-$C1FF`, I/O `$C000-$C0FF` (`$C000` output flip-flop, `$C081` tape input). Loads `.aci` (raw pulse-duration @ `kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ`), `.wav`, `.mp3`, `.ogg` via vendored miniaudio. `kWavFileSampleRate` is `saveWavTape` export rate; tape-saving test pins `.aci` ↔ `.wav` round-trip. **Side-car `tapeinfo.txt`**: `lookupTapeInfo(path)` reads `<dir>/tapeinfo.txt` (`filename = load-range`), stored in `loadInfo`; deck jaquette prints ready-to-type Wozmon command e.g. *"Type 0280.0FFFR"*.
- **GraphicsCard** — Uncle Bernie's GEN2 HGR. Passively reads RAM `$2000-$3FFF`, renders 280×192 NTSC artifact colour. Apple II-compatible non-linear scanlines (`scanlineAddress()`). `rasterizeLine` is a 14 KB LUT: `kHgrPixels[(colParity << 8) | byte]` returns 7 pre-resolved RGBA pixels/byte (built by `computeIsolatedPixel`, no inter-byte neighbours), memcpy-copied; second pass paints white at 39 inter-byte seams when bit 6 ↔ bit 0 both lit. The 9th index bit is `colParity` (absolute screenX parity), **not** group2 — group2 is already `byte & 0x80`.
- **TMS9918** — P-LAB VDP, 16 KB VRAM, I/O `$CC00/$CC01`. `renderToBuffer()` → 256×192 RGBA (IM_COL32 = `GL_RGBA + GL_UNSIGNED_BYTE`); UI uploads via `glTexSubImage2D`.
- **AudioDevice** — miniaudio (desktop) / Web Audio ScriptProcessorNode (WASM). Defines `AudioSource::fillAudioBuffer(float*, int)`; mixes Cassette + SID mono float32. `getActualSampleRate()` returns miniaudio-negotiated rate (44.1 kHz requested, often 48 kHz on Apple Silicon) — **cycle-driven sources must use this value** or tempo drifts by the rate ratio. WASM always returns 44.1 kHz.
- **SID** — P-LAB A1-SID. Wraps **libresidfp** (GPL-2.0+, vendored as `libresidfp_static`). 6581 / 8580 swappable at runtime; swap rebuilds filter chain, restores last-written register state. I/O `$C800-$CFFF` (29 regs, `addr & 0x1F`). Coexists with TMS9918 at `$CC00-$CC01` (VDP wins via priority 10). **A1-AUDIO SE** (Parmigiani, 10 units): same `pom1::SID` instance, second bus handle at `$CC00-$CC1F` (same `& 0x1F`). `setSIDEnabled` / `setSIDSpecialEditionEnabled` evict each other (one socket); SE additionally evicts TMS9918.

  **Cycle-driven, SPSC-buffered audio.** `setSamplingParameters(POM1_CPU_CLOCK_HZ, DECIMATE, actualOutputRate)` at boot (actual miniaudio rate). `Memory::advanceCycles()` → `SID::advanceCycles(cycles)` after every CPU slice; `chip->clock(batch, staging)` in 4096-cycle chunks, int16→float into a 16 384-sample ring (atomic head/tail, drop-oldest on overflow). Audio callback only drains — no `chip->clock` on audio thread, no mutex across the callback. `chipMutex` serialises register writes / `chip->clock()` / `setChipModel()` between UI and emulation threads. SID tempo tracks emulated cycles, so the M6502 `run()` overshoot rule matters.

  **WASM specifics** (see `third_party/libresidfp/CMakeLists.txt` + patches in `FilterModelConfig{6581,8580}.cpp`):
  - Emscripten `-s STACK_SIZE=4194304` (4 MB) — filter-model tables overflow the default 64 KB stack at first `reSIDfp::SID` construction.
  - Filter-model builders run **sequentially** under `__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__`. Upstream parallelises with `std::thread`, which throws `system_error 138 "thread constructor failed"` in single-threaded WASM. Sequential adds ~50 ms to cold start (under 100 ms total).
- **MicroSD** — P-LAB microSD. 65C22 VIA at `$A000-$A00F` bridging emulated ATMEGA. SD CARD OS ROM (8 KB) at `$8000-$9FFF`. Handshake: PORTB bit 0 = CPU_STROBE, bit 7 = MCU_STROBE, PORTA = data. Host `sdcard/` as virtual FAT32; tagged filenames `NAME#TTAAAA` encode type + load address. Commands: `cmdRead`/`cmdLoad` (strict + fuzzy prefix), `cmdWrite`, `cmdDir`/`cmdLs`, `cmdCd` (only nav op — `..`, absolute `/PATH`, relative, fuzzy leaf), `cmdDel`, `cmdMkdir`, `cmdRmdir`, `cmdPwd`, `cmdMount`. **All name-accepting commands resolve against `currentDirectory` only — no recursive search.** `getCurrentDirDisplay()` returns `"/" + currentDirectory`; Wozmon prompt (`/PLAB>`, `/PLAB/MCODE>`) literally is the cwd. Regression-pinned by `tools/test_sdcard_subdir_navigation_telnet.py`.
- **CFFA1** — Rich Dreher's CompactFlash. 8 KB ROM `$9000-$AFDF` (ID bytes `CF`/`FA` at `$AFDC`/`$AFDD`), ATA/IDE regs `$AFE0-$AFFF` (A4 not decoded → `$AFE0` mirrors `$AFF0`). Backs a ProDOS `.po`; emulates READ/WRITE SECTOR + SET FEATURE only (everything the firmware uses). Desktop auto-mount probes `cfcard/cfcard.po` up three dirs. Two bus entries: read-only ROM + r/w regs.
- **JukeBox** — P-LAB Juke-Box. 32 KB EEPROM (28c256) at `$4000-$BFFF` (RAM-16/ROM-32) or `$8000-$BFFF` (RAM-32/ROM-16). Program Manager at `$BD00` (`&` prompt); Save Program at `$B800` writes RAM→EEPROM when RW jumper on. v1 models the single-page 28c256 only. TWO disjoint bus handles at priority 20 (`$4000-$BFFF` + `$8000-$BFFF`); exactly one enabled, flipped by `setJukeBoxJumper()`. Mutex with CFFA1, microSD, Krusader, Wi-Fi Modem. Firmware signature: byte at file offset `$7D00` = `$A5` (first byte of Program Manager per EEPROM RW manual `BD00: A5`). Build `roms/jukebox.rom` via `doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py` (P-LAB's `2-packer.sh` produces subtly different layouts — prefer the Python script).
- **A1IO_RTC** — P-LAB A1-IO & RTC. 65C22 VIA at `$2000-$200F` (⚠ overlaps GEN2 HGR — mutex at preset level). Emulated ATMEGA32 drives DS3231 (date/time + temp), DS18B20, 8 analog + 4 digital inputs, 16-bit shift-register digital out. Broadcast protocol: 24 regs pumped on 100-cycle period with PORTB STROBE handshake.
- **WiFiModem** — P-LAB MODEM BBS. 65C51 ACIA at `$B000-$B003`, ESP8266 AT interpreter, Hayes AT (`AT` / `ATDT host:port` / `ATH` / `ATE0/1` / `ATI` / `ATZ`), TELNET IAC filtering + `CR LF→CR` strip, non-blocking TCP, baud 50-19200, `+++` with 1 s guard, 4096 B circular Rx. `requestDisconnect()` is the UI-safe entry (calls `handleATH()` under `modemMutex`). Desktop only; WASM stubs return `NO CARRIER`.
- **TerminalCard** — passive bridge: sniffs `$D012` writes (hook in `memWrite`), injects keys into `$D010`/`$D011`. TCP server on IPv4 loopback :6502 (IPv6 `::1` refused — `telnet localhost` falls back to `127.0.0.1`). Modes: 7-bit (CR→CRLF, optional uppercase via Ctrl-O/I) and 8-bit raw (Ctrl-T, fires in either mode). Controls: Ctrl-L clear, Ctrl-R reset; each has an **ESC-prefixed alternate** (ESC T/O/L/R/I) for tty line disciplines that eat Ctrl-T/O/R. Unknown ESC sequences forward the ESC so ANSI still reaches the Apple 1. On accept: `IAC WILL ECHO` + `IAC WILL SUPPRESS-GO-AHEAD` + `IAC DO SUPPRESS-GO-AHEAD` flips the client to character-at-a-time. Pending reset/clear use `std::atomic<bool>` consumed **outside** `stateMutex` to avoid deadlock with `runEmulationSlice()`. Desktop only.
- **SocketHandle** — move-only RAII wrapper (`NativeSocket` = `SOCKET`/`int`) shared by WiFiModem + TerminalCard. Destructor closes the FD so exceptions between `socket()` and explicit close don't leak.
- **GT6144** — SWTPC GT-6144 Graphic Terminal (1976, $98.50). 64×96 mono framebuffer on 6× Intel 2102 SRAM, **write-only** I/O at `$D00A` (PIA `A3` chip-select). 4-phase FSM on a single port: `0..63` latch X + pixel OFF; `64..127` latch X + pixel ON; `128..223` commit Y (uses latched X + state); `224..255` control opcode (`byte & 0x07`: 0=INVERTED / 1=NORMAL / 4=UNBLANK / 5=BLANK / 2,3=CT-1024 mix (no-op standalone) / 6=reserved / 7=NORMAL alias; bits 3-4 are don't-cares so `224/232/240/248` all mean INVERTED). Inversion and blanking live in the render path — framebuffer untouched (matches analog XOR at video output). Bus `{0xD00A,0xD00A}` priority 0 is enough because `memWrite` dispatches the bus **before** `$D0xx` PIA aliasing; reads fall through to PIA alias (no read-back on the real card). Power-on framebuffer is pseudo-random (`std::random_device`-seeded `mt19937`) to reproduce the Intel 2102 bistable "rectangles aléatoires"; `setGT6144Enabled(true)` reseeds on every plug-in. No bus overlap with other POM1 peripherals — composes freely. **Display aspect**: the 64×96 matrix (logically 2:3) was fed to a stock 4:3 CRT, so SWTPC's manual describes the visible pixels as "petits rectangles" — each logical pixel maps to a 2:1 (W:H) rectangle on screen. `renderGT6144Window` uploads the texture at native 64×96 but passes a stretched `displayAspectW = kWidth * 2` to `layoutFitVideoViewport`, letting GL_NEAREST do the 2× horizontal blit-time stretch. Any rewrite that "simplifies" back to square pixels (logical aspect) is a bug.

## Key implementation details

### MMIO

- `$D010` (KBD) — last key with bit 7 set; read clears the strobe.
- `$D011` (KBDCR) — bit 7 = 1 when key ready.
- `$D012` (DSP) — write triggers display callback + TerminalCard sniffer. Read returns bit 7 = 0 (ready) after terminal-speed delay, else bit 7 = 1 (busy); counter clamped ≥ 0.

PIA 6821 incomplete decoding aliases `$D0xx` → `$D010-$D012` by the low 2 bits. `memRead`/`memWrite` normalise before dispatch. Keyboard forced uppercase via `setKeyPressed`; Terminal Card uses `setKeyPressedRaw()`.

### CPU execution

Three modes via EmulationController: **Stopped**, **Running** (`executionSpeed` cycles/frame — 1× = 17045, 2× = 34091, MAX = 1 000 000), **Step** (`stepCpu()`).

### Addressing modes

All addressing modes store the resolved address in `op` (quint16). `Imm()` stores `programCounter` in `op`. Every instruction goes through `memory->memRead(op)` / `memWrite(op, value)` uniformly.

### Loading programs

- `Memory::loadBinary(filename, startAddress)` — raw binary.
- `Memory::loadHexDump(filename, startAddress)` — Wozmon hex. Supports comment lines + inline `//` / `#` / `;` (strips inline comments — prevents mnemonic letters like `LDA`/`DEX` being parsed as data), continuation lines, `T` prefix (turbo), `X` marker, `R` suffix (run address). Also handles single-line files where data merges with addresses (e.g. `ED0300:` → data `ED` + address `0300`).
- File dialogs export a memory range as binary or Woz hex.
- Clipboard paste feeds characters to the keyboard (4096-char cap).

### Out-of-range RAM

When preset `ramKB < 64`, accesses in `[ramKB*1024, $8000)` track as OOR. **Strict mode** (Memory Settings → *Strict enforcement*) returns `$FF` on read and drops writes — matches a real Apple-1 with no RAM board. Status bar: `OOR:N!` (strict + counter) or `[strict]` (strict + nothing yet).

### Memory Map window

16×16 grid (256 pages = 64 KB), colour-coded regions, KB labels, PC/SP indicators, hover tooltips guarded by `IsWindowHovered`, legend, I/O register details, live CPU vectors. In `MainWindow_DebugWindows.cpp`.

## Memory Map

```
$0000-$00FF  Zero page
$0100-$01FF  Stack
$0200-$1FFF  User RAM (programs typically load at $0280 or $0300)
$2000-$200F  A1-IO RTC VIA 65C22 (mutex with GEN2 below)
$2000-$3FFF  GEN2 HGR framebuffer (8 KB)
$4000-$BFFF  Juke-Box ROM (32 KB, RAM-16/ROM-32 jumper)
$4000-$5FFF  User RAM (otherwise)
$6000-$7FFF  Applesoft Lite SD ROM — `applesoft-lite-microsd.rom` (microSD preset, cold-start `6000R`)
$8000-$BFFF  Juke-Box ROM (16 KB upper, RAM-32/ROM-16 jumper)
$8000-$9FFF  SD CARD OS ROM (P-LAB microSD)
$9000-$AFDF  CFFA1 firmware ROM (shadows microSD ROM + BASIC low page)
$A000-$A00F  microSD VIA 65C22
$A010-$AFDF  User RAM (when neither microSD nor CFFA1)
$AFE0-$AFFF  CFFA1 ATA/IDE registers (A4 undecoded: $AFE0 mirrors $AFF0; ID at $AFDC/$AFDD)
$B000-$B003  MODEM BBS ACIA 65C51
$B004-$BFFF  User RAM
$C000-$C0FF  ACI I/O ($C081 tape input, $C000 output flip-flop)
$C100-$C1FF  Woz ACI ROM
$C800-$CFFF  A1-SID (29 regs, `& 0x1F`)
$CC00-$CC1F  A1-AUDIO SE (same chip, window relocated; excludes TMS9918)
$CC00/$CC01  TMS9918 DATA / CTRL (wins over A1-SID; SE evicts TMS9918)
$D00A        SWTPC GT-6144 command port (write-only; bus wins over PIA alias)
$D010        KBD           (aliases $D0F0, $D030, …)
$D011        KBDCR         (aliases $D0F1, $D031, …)
$D012        DSP           (aliases $D0F2, $D032, …)
$E000-$EFFF  Integer BASIC ROM (4 KB)
$FF00-$FFFF  Woz Monitor ROM + vectors (NMI/Reset/IRQ at $FFFA-$FFFF)
```

## Platform notes

- **CMake** — `find_package(glfw3 CONFIG)` first (vcpkg, Homebrew), falls back to `pkg_check_modules` (apt/dnf/pacman).
- **Windows** — Visual Studio C++ workload + CMake + Git + vcpkg. MSVC: `/utf-8`, `_CRT_SECURE_NO_WARNINGS`. `package_windows_release.bat` builds the standalone release archive.
- **macOS** — links Cocoa + IOKit + CoreVideo. `GLFW_OPENGL_FORWARD_COMPAT` macOS-only. Window icon is a no-op (OS pulls from `.app` bundle).
- **Linux** — `setup_imgui.sh` supports apt, dnf, pacman.

`build/`, `build-wasm/`, `imgui/` gitignored.

## Testing

`ctest` targets from `tests/CMakeLists.txt` (native-only, opt-out via `-DPOM1_ENABLE_TESTS=OFF`). Run from `build/`:

```bash
ctest                                    # all seven (~5 s)
ctest --output-on-failure                # stdout/stderr on regression
ctest -R klaus -V                        # single test, verbose
```

- **`klaus_6502_functional`** — [Klaus Dormann's 6502 test](https://github.com/Klaus2m5/6502_65C02_functional_tests) vs M6502. `file(DOWNLOAD)` at configure time (SHA-256 pinned). Flips `Memory::setTestMode(true)` (flat RAM), sets `PC = $0400` (reset vector hits a trap — callers must jump directly), steps until `JMP *`. Success = final PC = `$3469`. ~1.5 s at 200 M max steps. Gates all CPU refactors.
- **`peripheral_bus_smoke`** — 6 assertions vs `PeripheralBus` with fake lambdas, no `Memory` (< 10 ms). Pins: page-mask miss, read routing, priority ordering at SID↔TMS9918, `setEnabled` round-trip, sniffer pass-through.
- **`sid_audio_smoke`** — enables SID, writes a voice-1 tone via the bus, clocks the CPU, asserts non-silent samples in the SID ring. Catches cycle-driven audio regressions (`advanceCycles`, ring head/tail, `setSamplingParameters`).
- **`aci_tape_loading`** — pulse extraction → ACI ROM → RAM on `cassettes/APPLE50TH.ogg`; asserts first three bytes (`A9 FF 48` = `LDA #$FF / PHA`) land at `$0280`. Also validates `kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ`. Needs `${CMAKE_SOURCE_DIR}` cwd.
- **`aci_tape_saving`** — drives ACI WRITE (Wozmon `<from>.<to>W`), saves `.aci` and `.wav`, reloads each in fresh `Memory`, asserts byte-for-byte. Catches regressions in `toggleOutput()`, `saveAciTape`/`saveWavTape`, `kTapeFileTimebaseHz` ↔ `kWavFileSampleRate` round-trip math.
- **`pr40_printer_smoke`** — pins PR-40 DPDT switch wiring to PB7 (`$D012` read busy-OR merge), 40-char FIFO + CR flush, ~0.8 s mechanical stall (`POM1_CPU_CLOCK_HZ * 4 / 5` cycles).
- **`gt6144_smoke`** — self-contained (only `GT6144` + `PeripheralBus`). Pins 4-phase FSM, pixel commit math (`(26, 22)` from `POKE -12278, 90 / 150`), control-opcode alias matrix (`224/232/240/248` all mean INVERTED), "inversion doesn't touch SRAM" invariant, and the visible SRAM power-on noise (two fresh cards must not have byte-identical framebuffers).

New invariant tests follow `tests/peripheral_bus_smoke_test.cpp` — `<cassert>` + `add_test` suffices; GTest/Catch2 only once multi-threaded tests land.

**Manual telnet tests** in `tools/test_*_telnet.py`. Two auto-launch POM1 (rest expect user start):

- **`test_sdcard_subdir_navigation_telnet.py`** — pins SD CARD OS "commands only search `currentDirectory`" invariant. Launches `build/POM1 --preset 4 --terminal --cpu-max`, creates `sdcard/testdir/HELLO#040300`, asserts `LOAD HELLO` / `DEL HELLO` fail at `/`, succeed after `CD TESTDIR`, verifies host file actually deleted, `CD ..` → `LOAD HELLO` fails again. Run from repo root.

## Version string locations

Bumping the version requires updating **all**:
- `main_imgui.cpp` — console output + GLFW window title
- `MainWindow_Dialogs.cpp` — About dialog
- `Screen_ImGui.cpp` — Apple 1 welcome screen
- `build-wasm/shell.html` — `<meta description>`, `<title>`, `<h1>` banner (3 occurrences)
- `README.md` — title + intro
- `package_windows_release.bat` — release ZIP filename
