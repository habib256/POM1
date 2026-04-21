# CLAUDE.md

Guidance for Claude Code working in this repository. Use `README.md` for the user-facing walkthrough, `TODO.md` for open work, and `git log` for shipped-feature history. This file is the architecture / invariants / gotchas memo for the **emulator side** of POM1.

For writing **Apple 1 software** that runs inside POM1 (new games, BASIC programs, SID tunes, microSD shell tools, …) the companion doc is **`APPLE1DEV.md`** — decision tree, toolchain fast-path, language/mode reference, peripheral commands, deployment channels, and a telnet test skeleton. `APPLE1DEV.md` in turn points at `doc/Programming_Apple1_ASM.md` (the 700-line French deep-dive on 6502/cc65/HGR/TMS9918 by Arnaud) for low-level assembly details.

## Project Overview

POM1 is an Apple 1 emulator built with Dear ImGui. It emulates the MOS 6502, the Apple 1 core (display, keyboard, ACI cassette), and a stack of expansion cards: Uncle Bernie's GEN2 HGR, P-LAB A1-SID (6581/8580), P-LAB TMS9918 VDP, P-LAB microSD (65C22 + ATMEGA), P-LAB MODEM BBS (65C51 + TCP/TELNET), P-LAB Terminal Card (TCP server), P-LAB A1-IO & RTC (65C22 + ATMEGA32 + DS3231), Rich Dreher's CFFA1 (ATA/IDE + ProDOS `.po`), and Claudio Parmigiani's P-LAB Juke-Box (EEPROM library). UI is English. Targets Linux, macOS, Windows, Web (Emscripten).

## Build & Run

```bash
./setup_imgui.sh             # one-time deps (Linux/macOS)
cd build && cmake .. && make # build → build/POM1
./run_emulator.sh            # runs from repo root; copies ROMs first
```

Windows uses `setup_imgui.bat` + vcpkg + `cmake --build . --config Release`. `CMAKE_EXPORT_COMPILE_COMMANDS` is on (build/compile_commands.json symlinked from repo root; clangd picks it up automatically).

### CLI flags (parsed in `CliDispatcher.cpp`, invoked from `main_imgui.cpp`)

Every GUI-reachable runtime action has a corresponding flag. The dispatcher separates verbs into three phases — **A** boot-time (consumed before the GLFW window opens or on the first rendered frame), **B** first-frame preset overrides (fire next to `terminalCardOverride`/`cpuMaxSpeedOnBoot`), **C** deferred, fire once the 15-frame card plug-in completes. Phase-C verbs execute in the order they appear on the command line; one `--load 0300:foo.bin --run 0300 --paste keys.txt` chain composes the obvious way.

| Flag | Phase | Effect |
|------|-------|--------|
| `--list-presets` | A | Print `index: name` for every entry in `kMachinePresets[]` and exit. |
| `--preset <N\|name>` / `-p` | A | Select a preset by index or case-insensitive substring (first match). |
| `--terminal` | A | Force-enable the Terminal Card on top of the preset (binds `127.0.0.1:6502`). |
| `--tape <path>` | A | Preload a tape + auto-press Play. Default probe when omitted: `cassettes/WOZ_talk.mp3` (silent-loaded). |
| `--save-tape <path>` | A | Dump the deck's capture on clean shutdown. `SIGINT`/`SIGTERM` route through `glfwSetWindowShouldClose(1)` so `~MainWindow_ImGui` runs. |
| `--save-tape-format <aci\|wav>` | A | Append the extension to `--save-tape` when the path has none. `.aci`/`.wav` already present wins; `CassetteDevice::saveTape()` always picks format by extension. |
| `--cpu-max` | A | Pin `executionSpeed` to 1 000 000 cycles/frame on boot (MAX button). Scripted ACI tests need this. |
| `--speed <cycles/frame>` | B | Override `executionSpeed` on the first frame. 17045 = 1×, 34091 = 2×. `--cpu-max` beats `--speed`. |
| `--enable <card>[,<card>…]` / `--disable <card>[,<card>…]` | B | Rewrite the preset's deferred plug list. Valid names: `aci, sid, sid-se, microsd, tms9918, a1io-rtc, hgr, cffa1, krusader, wifi, terminal, jukebox` (plus a few aliases — see `kCardNames` in `CliDispatcher.cpp`). Mutual-exclusion invariants (SID↔SID-SE, TMS9918↔SID-SE, …) are enforced by the override consumer alongside existing UI rules. `--disable krusader` is a no-op (ROM unload requires a hard reset — switch to a Krusader-less preset instead). |
| `--sid-chip <6581\|8580>` | B | Switch the A1-SID / A1-AUDIO Special Edition chip model before the deferred plug fires. libresidfp replays the last register state on the new chip. |
| `--jukebox-jumper <ram16\|ram32>` | B | Pick the Juke-Box RAM/ROM split (`RAM16_ROM32` → ROM at $4000-$BFFF, `RAM32_ROM16` → ROM at $8000-$BFFF). |
| `--trace-brk` | C | Enable the M6502 BRK register/stack/trace dump (`EmulationController::setCpuBrkTraceEnabled(true)`). |
| `--load <addr>:<path>` | C | Load a raw binary at the given address, rewrite reset vector, `hardReset` + `start`. Addresses accept hex (`0300`, `0x0300`, `$0300`) or decimal ≥ 5 digits. Repeatable. |
| `--run <addr>` | C | Jump the CPU to `addr` without loading anything — `EmulationController::jumpTo()` stops, rewrites reset vector, hard-resets, re-starts. Combine with `--load`: `--load A:x.bin --run A`. |
| `--paste <file>` | C | Feed up to 4096 characters from `<file>` into the Apple 1 keyboard queue, same filter as the clipboard paste (`\n`→CR, printable ASCII only). |
| `--step <N>` | C | Step the CPU `N` instructions after the deferred plug. Useful with `--trace-brk` for scripted inspection. |
| `--play` / `--rec` / `--rewind` | C | Cassette deck transport. `--rec` arms recording without waiting for a first `$C000` toggle (new `CassetteDevice::armRecording()` wrapper). |
| `--sd-mkdir <path>` | C | `create_directories(sdcard_root/<path>)`. Paths escaping the sdcard root are rejected. Works on any preset (the microSD host root is probed by `Memory`'s ctor independently of whether the card is plugged). |
| `--sd-put <host>:<guest>` | C | Copy `<host>` into `sdcard_root/<guest>`, creating parent dirs. Used for fixture seeding without the Wozmon round-trip. |
| `--sd-get <guest>:<host>` | C | Reverse of `--sd-put`: `sdcard_root/<guest>` → `<host>`. |
| `--rtc-freeze "YYYY-MM-DD HH:MM:SS"` | C | Set `A1IO_RTC::rtcOffsetSeconds` so the virtual clock reads the supplied time. The RTC continues ticking at host-clock rate — fine for sub-minute scripted runs. |
| `--break <addr>` | — | **Reserved**. Exits with an error: `M6502` has no breakpoint infrastructure yet. Use `--step` + `--trace-brk` instead. |

`--enable`/`--disable` are applied after `--terminal` so an explicit `--disable terminal` overrides `--terminal`. Case-insensitive; the card list accepts a comma-separated CSV and can be repeated.

Malformed verbs log `[CLI] ERROR: …` via the shared logger and exit **1** before GLFW opens — matches the pre-existing behaviour of the `--preset` out-of-range check.

Typical telnet workflow:

```bash
./POM1 --list-presets
./POM1 --preset 10 --terminal &
sleep 3 && python3 tools/test_jukebox_telnet.py
```

Two tests launch POM1 themselves and must run from the repo root (not `build/`): `test_aci_telnet.py`, `test_sdcard_subdir_navigation_telnet.py`.

### WASM build

```bash
source /path/to/emsdk/emsdk_env.sh
mkdir -p build-wasm && cd build-wasm
emcmake cmake .. && emmake make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
emrun POM1.html
```

Outputs `POM1.{html,js,wasm,data}`. The `.data` blob is Emscripten **MEMFS** preload (rules in `CMakeLists.txt` under `if(POM1_IS_WASM)`):

| Host path | Mount |
|-----------|-------|
| `roms/` `pic/` `fonts/` `software/` `sdcard/` `cassettes/` | same name |
| `cfcard/cfcard.po` | `cfcard/cfcard.po` (single file — the other `.po` extras are desktop-only, would bloat the bundle by >140 MB) |

Rebuild WASM after any change under those trees (or after editing `build-wasm/shell.html`) so `POM1.data` stays in sync.

A **POST_BUILD** step also copies `pic/icon.png` to `build-wasm/pic/icon.png` as an HTTP-visible twin for the `<link rel="icon">` tag in `shell.html` — the MEMFS copy inside `POM1.data` is unreachable by the browser's favicon loader.

The desktop `Memory` ctor probes `sdcard`, `../sdcard`, `../../sdcard` (cwd-relative) for the SD mount and `cfcard/cfcard.po` similarly for the CFFA1 disk.

### Assembling programs (cc65)

| Config | Purpose |
|--------|---------|
| `software/apple1.cfg` | Standard Apple 1 (plain text video, no framebuffer reserved) |
| `software/hgr/apple1_gen2.cfg` | GEN2 HGR — reserves `$2000-$3FFF` for the framebuffer |
| `software/games/apple1_sok_4k.cfg` / `_8k.cfg` / `_hgr.cfg` | Sokoban variants (stock 4K / 8K + TMS / 8K + GEN2) |

The Sokoban configs define a `LEVELBUF` segment (zp at `$0020`) for the RLE scratch buffer and a `STATEGRID` segment (bss at `$0F00` or `$1F00`) — use `.segment "LEVELBUF": zeropage` in asm to force `zp,X` addressing on the buffer.

## Architecture

Each `.cpp/.h` pair owns one concern. Only the non-obvious bits are called out below.

### Parmigiani's golden rule — "one board at a time"

Claudio PARMIGIANI, designer of the P-LAB Apple-1 expansion family, states his golden rule as **"one board at a time"**. On real Apple-1 hardware you plug exactly ONE P-LAB card at any given moment — never several simultaneously. The 6502 bus has no arbitration, no IRQ routing, and many P-LAB cards deliberately overlap address windows (A1-SID `$C800-$CFFF` vs. TMS9918 `$CC00/$CC01`; A1-IO RTC `$2000-$200F` vs. GEN2 HGR `$2000-$3FFF`; Juke-Box claims `$4000-$BFFF` entirely). Running two at once would require cooperative decoding that simply isn't wired.

POM1 **breaks this rule on purpose** for pedagogical and convenience reasons: the two "Multiplexing Fantasy" presets (#12 *P-LAB Apple-1 Multiplexing Fantasy* and #14 *POM1 Apple-1 Multiplexing Fantasy (2026)*) enable several P-LAB cards at once. The name is literal — this is a **fantasy**, not a buildable machine. The mutual-exclusion rules enforced elsewhere (SID ↔ SID-SE, TMS9918 ↔ SID-SE, GEN2 ↔ A1-IO, Juke-Box ↔ {CFFA1, microSD, Krusader, Wi-Fi Modem}) are not UI politeness; they mirror real bus conflicts that cannot coexist even in the Multiplexing Fantasy. When adding a new card, honour the rule: model each overlap honestly, and document any intentional coexistence as a POM1-only liberty.

### Core
- **M6502.cpp/h** — MOS 6502 CPU. `op` is `quint16` for 16-bit address handling across all addressing modes; `tmp` is `int` for carry/borrow via bit 8. BCD ADC propagates the low→high carry from `(accumulator & 0xF0)` (bit 4 of the BCD-adjusted accumulator), not from the unadjusted sum. **`run(maxCycles)` returns the actual cycle count** (overshoots by up to 6 cycles per iteration — the loop only checks the budget between instructions); callers pacing against wallclock must deduct the returned value. A 12-slot PC ring buffer fills on every instruction (cheap, always on); `dumpPcTrace(tag)` flushes it. `setDebugBrkTrace(true)` dumps registers/stack/trace on every `BRK`. `setProgramCounter(pc)` is the Klaus test harness back-door.
- **CpuClock.h** — Nominal clock **`POM1_CPU_CLOCK_HZ` = 1 022 727** (14.31818 MHz ÷ 14). Exports `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` / `..._2X_60HZ` / `..._PER_MILLISECOND`. Consumed by EmulationController, SID, TMS9918 frame tick, modem baud + `+++` guard, Terminal Card poll, microSD idle, cassette timebase, terminal display delay.
- **Memory.cpp/h** — 64 KB space. Owns every peripheral (`unique_ptr` + enable flag). MMIO dispatches via `PeripheralBus`; `memRead`/`memWrite` only handle PIA 6821 (`$D010/$D011/$D012`) + `$D0xx` aliasing, ROM write-protect (`writeInRom`), OOR strict mode, the cassette-write sniffer, `DisplayDevice::onChar` forwarding + TerminalCard hook, then raw `mem[]`. `setKeyPressedRaw()` bypasses forced uppercase (Terminal Card). **`setTestMode(true)`** collapses everything to `mem[address]` — flat 64 KB for Klaus Dormann's harness; never enable in production. **Redundant-ROM-load guards**: `loadApplesoftLiteSDCard` skips the WOZ reload when `mem[$FF00]==0xD8 && mem[$FF01]==0x58` (WOZ starts `CLD / CLI`); `setMicroSDEnabled(true)` skips the SD CARD OS reload when `mem[$8000]==0xA9 && mem[$8001]==0x00`.
- **DisplayDevice.h** — abstract `onChar(char)` sink for `$D012` writes. `Screen_ImGui` implements it; injected via `Memory::setDisplayDevice()`. Replaces the old C-style callback + atomic singleton so tests and future peripherals can tee display output.
- **PeripheralBus.h/cpp** — central MMIO dispatch table. Each peripheral registers `(name, range, priority, onRead, onWrite)` at `Memory` ctor. Hot path is O(1): `pageMask[256]` bitmap pre-computed on every register/setEnabled → `tryRead`/`tryWrite` read one mask byte, branch on zero (miss = 99 %+ of accesses), otherwise dispatch. `std::stable_sort` by priority so ties respect insertion order (TMS9918 wins over SID at `$CC00/$CC01` via priority 10). Empty `onWrite = {}` = pass-through to RAM; explicit no-op `onWrite` = block (CFFA1 ROM). Cassette write-toggle stays inline in `Memory::memWrite` because it's a sniffer (byte must still land in `mem[]`).

### Emulation orchestration
- **EmulationController.cpp/h** — façade owning M6502 + Memory + the emulation thread. ~55 public methods (CPU control, ROM reload, snapshot, hardware toggles, keyboard, tape). **Mutex order**: `stateMutex > keyboard.keyMutex > publisher.snapshotMutex`. `stateMutex` is a `PriorityMutex` (wraps `std::mutex` + waiter count) — at MAX speed the emulation loop calls `std::this_thread::yield()` after each slice iff `stateMutex.hasWaiters()`, keeping the UI thread from starving. Slice cap: `kMaxSliceCycles = 6000` desktop (~50 µs mutex hold @ ~120 MHz emulated), 50 000 WASM (single-threaded from the main loop, has to burn the whole frame budget per pump). Native = dedicated `emulationThread` consuming `runEmulationSlice()`; WASM = `pumpEmulationMainThread()` from the main loop.
- **EmulationSnapshot.h** — UI-ready immutable picture (memory, CPU regs, peripheral snapshots, OOR flag). Sized once at startup so `SnapshotPublisher::publish()` does an in-place memcpy into `latestSnapshot.memory.data()`, no per-frame alloc.
- **SnapshotPublisher.h/cpp** — SPSC slot. `publish(Memory&, M6502&, bool)` runs under stateMutex and takes its own `snapshotMutex` to serialize the UI-thread `copyTo()`. **Page-level dirty copy**: `Memory::memWrite` sets one bit per 256 B page in `dirtyPages`; `publish()` walks the bitmap, collapses contiguous runs into single memcpy, clears bitmap. Typical running program = ~4-8 pages/frame → 1-2 KB copy vs. full 64 KB. Idle Wozmon = zero copy. TMS9918's 16 KB is skipped when unplugged.
- **KeyboardController.h/cpp** — SPSC key queue. `queueKey()` is UI-safe; `drainTo(Memory&)` runs under stateMutex from the emulation slice and uses `std::swap` to release `keyMutex` before calling `Memory::setKeyPressed()`.
- **RomLoader.h/cpp** — six static helpers (`reloadBasic` / `ApplesoftLite` / `WozMonitor` / `Krusader` / `AciRom` / `CFFA1Rom`) factoring the toggle-`writeInRom` + `Memory::loadXxx()` + restore pattern.
- **Disassembler6502.h/cpp** — standalone `pom1::disassemble6502(mem, pc, instrLen)` for the debug console. 256-entry opcode table, no UI deps.
- **Logger.h/cpp** — process-wide levelled logging (`pom1::log().info("Tag", "msg")`). `StreamLogger` → stdout (Debug/Info) / stderr (Warn/Error); `RingBufferLogger` caches the last N entries for the in-app console; `TeeLogger` chains sinks. `pom1::initDefaultTeeLogger()` (called from `main()`) installs `Tee(stream, uiRingBuffer())`. Thread-safe (mutex per logger).

### UI (ImGui)
- **main_imgui.cpp** — GLFW/OpenGL3 init (GL 3.2 Core, GLSL 150; WebGL 2 / GLSL ES 300 on WASM). `GLFW_OPENGL_FORWARD_COMPAT` is macOS-only. `SIGINT`/`SIGTERM` are rerouted into `glfwSetWindowShouldClose(1)` so `~MainWindow_ImGui` gets to flush `--save-tape` before exit. **OS window icon**: after `glfwCreateWindow`, desktop builds probe `pic/icon.png` via `find_app_icon_path()` (cwd / `../` / exe-relative), decode with stb_image, push via `glfwSetWindowIcon`. `#if !defined(__APPLE__)` — GLFW logs "Regular windows do not have icons on macOS"; the OS pulls its icon from the `.app` bundle instead. The helper is `#ifdef`-guarded out on Apple to avoid unused-function warnings.
- **MainWindow_ImGui.cpp/h** — single class, implementation split across **9 TUs** sharing private helpers via `MainWindow_Internal.h` (namespace `pom1::mainwindow::detail`):
  - `MainWindow_ImGui.cpp` — ctor/dtor, `createPom1`/`destroyPom1`, `render()`, trivial action handlers, status helpers, CPU control.
  - `MainWindow_Layout.cpp` — toolbar icons (cassette, DIP chip, text), monitor-tint cycle, `layoutFitVideoViewport`.
  - `MainWindow_Presets.cpp` — `kMachinePresets[]` + `applyMachineConfig` + `getPresetCount/Name`. Migration target for external `presets.json`.
  - `MainWindow_Menu.cpp` — `renderMenuBar` / `renderToolbar` / `renderStatusBar`.
  - `MainWindow_Dialogs.cpp` — About, Special Thanks, Welcome, Hardware/Software Reference, Display/Memory Settings. Image loading funnels through three twin helpers `ensureAboutPhotoTexture` / `ensureApple50LogoTexture` / `ensureAppIconTexture` — same stb_image → GL_TEXTURE_2D pattern, guarded by a `*LoadTried` flag, released in `destroyPom1()`. About = 128 px icon flush-left + group text right. Welcome = 64 px icon flush-left + group text right, four `Separator()`-separated sections (quick-start Woz / cassette / microSD shell with the "CD before LOAD/DEL" invariant / BASIC variants).
  - `MainWindow_HardwareWindows.cpp` — GraphicsCard / TMS9918 / WiFiModem / TerminalCard / A1IO_RTC / Juke-Box windows. GL texture for TMS9918 (256×192 RGBA, `GL_NEAREST`).
  - `MainWindow_FileDialogs.cpp` — Load/Save Memory + Tape, Cassette Control, Paste Code, auto-enable-by-directory heuristics.
  - `MainWindow_DebugWindows.cpp` — Debug Console + Memory Map (16×16 grid, PC/SP indicators, BRK trace panel).
  - `MainWindow_Keyboard.cpp` — `shortcuts[]` table + `handleGlfw{Char,Key}`. Keys go straight from GLFW to the Apple 1, bypassing `InputQueueCharacters`. PRESS/REPEAT tagged by `handleGlfwKey` into `nextCharIsRepeat` for the following `handleGlfwChar`. With Hardware → *Keyboard autorepeat* off (default, matches TTL keyboards), REPEAT drops printable chars and Enter/Backspace/Escape. F7 (step) honours REPEAT for hold-to-step.

  **`applyMachineConfig(int)` invariants**:
  - Unplugs every card up front, optionally `hardReset()` (skipped on the first invocation — `Memory::Memory()` already initialised), sets UI flags immediately, then queues a deferred *plug* via `pendingCardEnableFrames = kCardEnableDeferFrames` (15 frames ≈ 200 ms). Deferring past the first CPU cycle fixes otherwise-silent cards on boot (the original symptom was a silent SID).
  - **ROM selection is based on preset config, not live flags** — the *microSD-flavoured* Applesoft Lite path (`$6000-$7FFF`) vs the *CFFA1* flavour (`$E000-$FFFF`) is picked from `cfg.microSD`/`cfg.cffa1` directly. Calling the dispatcher `reloadApplesoftLite()` would misroute because `microSDEnabled` / `cffa1Enabled` are still false at ROM-load time (deferred).
  - `applyPendingLayout(const char*)` runs before each window's `Begin()` and sets pos/size with **`ImGuiCond_FirstUseEver`** — preset ships a default, user drags persist to `imgui.ini` and win on subsequent launches (delete `imgui.ini` to restore). **Widgets must not call `SetNextWindowSize(..., FirstUseEver)` themselves** (the Cassette Deck used to) — that call runs after `applyPendingLayout` and overwrites the preset hint; only `SetNextWindowSizeConstraints` stays inside the widget.
  - Default POM1 preset = index 13 (*POM1 Multiplexing Fantasy (2026)*). Shipped layout: **Apple 1 Screen (10, 61) 843×701 / Welcome (858, 61) 313×223 / Cassette Deck (858, 288) 406×444**, mirrored in `build/imgui.ini`. `applyMachineConfig` flips `showCassetteDeck = true` and `showWelcome = true` for that preset only.
  - First frame only: `computePresetLayoutExtent(cfg, fallback)` bounds the preset layout and `glfwSetWindowSize`s the OS window to contain it (POM1 default → **1274 × 801**; same value is the `glfwCreateWindow` initial hint to avoid a resize flash). WASM pumps mirror this in `getWasmCanvasPixelSize`.
  - Auto-enable cards by source directory of a loaded file (`software/sid/`, `software/hgr/`, `software/tms9918/`, `software/net/`, `sdcard/`). Loading from `software/net/` also calls `wifiModemReset()` to drop any live connection.
- **CassetteDeck_ImGui.cpp/h** — procedural cassette deck widget (`pom1::CassetteDeck_ImGui`, owned by `MainWindow_ImGui::cassetteDeck`). Chassis, speaker grille, mechanical counter (000-999 rollover), cassette window with spinning hubs, brand strip, piano-key transport — all via `ImDrawList` (no textures; the Apple 50th logo is an optional blit on the right cassette label). Transport state machine with real interlocks: REC alone = REC+PLAY, PAUSE only latches on Play/Rec, STOP releases, EJECT only from Stopped, REW/FF release PLAY. Transport actions forward to `CassetteDevice` via `EmulationController` — the deck is UI only. Volume tracked locally so rapid clicks accumulate deterministically.
- **Screen_ImGui.cpp/h** — 40×24 grid. Two `characterRenderMode` modes (Apple1Charmap = bitmap from `roms/charmap.rom`, HostAscii = ImGui font). Three `monitorMode` tints (Green/Amber/Monochrome). Blinking `@` cursor (`fmod` avoids float overflow), brightness/contrast. CRT effect = two passes: `drawCRTBackdrop()` paints phosphor-tinted alternating rows **before** glyphs; `drawCRTScanlines()` paints the dark mesh **after** glyphs as 1-px `AddRectFilled` every 2 display pixels at integer Y coords (default `crtScanlineAlpha = 0.50`, batched). `AddLine` is avoided: its AA rasterizer on macOS OpenGL 3.2 and WebGL2 splits sub-2-px thickness across two display rows and halves effective alpha. Integer Y also pins each dark row to a real pixel so the pattern doesn't drift under glyphs on resize.
- **MemoryViewer_ImGui.cpp/h** — Hex editor with color-coded regions, search, bookmarks, inline double-click editing.

### Peripherals
- **CassetteDevice.cpp/h** — ACI. Woz ROM `$C100-$C1FF`, I/O `$C000-$C0FF` (`$C000` output flip-flop, `$C081` tape input). `AudioSource`-compatible; loads `.aci` (raw pulse-duration @ `kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ`), `.wav`, `.mp3`, `.ogg` via vendored miniaudio (resamples + mono-mixes on demand). `kWavFileSampleRate` is the export rate for `saveWavTape`; the tape-saving test pins the `.aci` ↔ `.wav` round-trip. **Side-car `tapeinfo.txt`**: `lookupTapeInfo(path)` reads `<dir>/tapeinfo.txt` (`filename = load-range`, e.g. `APPLE50TH.ogg = 0280.0FFF`) on `loadTape`, stores the value in `loadInfo`, which the cassette deck prints on the jaquette as the ready-to-type Wozmon command *"Type 0280.0FFFR"* (both AUDIO STREAM and PROGRAM TAPE modes).
- **GraphicsCard.cpp/h** — Uncle Bernie's GEN2 HGR. Passively reads RAM `$2000-$3FFF`, renders 280×192 with NTSC artifact colour (violet/green for group 1, blue/orange for group 2, white between). Apple II-compatible non-linear scanline layout (`scanlineAddress()`). `rasterizeLine` is a 14 KB LUT: `kHgrPixels[(colParity << 8) | byte]` returns 7 pre-resolved RGBA pixels per byte (built by `computeIsolatedPixel` assuming no inter-byte neighbours), copied with memcpy; a second pass paints both sides white at the 39 inter-byte seams when bit 6 ↔ bit 0 are both lit (the only case the LUT gets wrong). The 9th index bit is `colParity`, **not** group2 — group2 is already `byte & 0x80`; what flips even↔odd colour assignment is absolute screenX parity (alternates between adjacent columns since 7 is odd).
- **TMS9918.cpp/h** — P-LAB TMS9918A VDP, 16 KB VRAM, I/O `$CC00/$CC01`. `renderToBuffer()` fills a 256×192 RGBA buffer (IM_COL32 byte order = `GL_RGBA + GL_UNSIGNED_BYTE`); UI uploads via `glTexSubImage2D` + `ImGui::Image` (nearest-neighbour).
- **AudioDevice.cpp/h** — Central audio output. Owns miniaudio (desktop) / Web Audio ScriptProcessorNode (WASM). Defines `AudioSource` (`fillAudioBuffer(float*, int)`); mixes registered sources (Cassette, SID) as mono float32 at the OS-negotiated rate. `getActualSampleRate()` returns what miniaudio actually got (44.1 kHz requested, often 48 kHz on Apple Silicon) — **cycle-driven sources must use this value** or their tempo drifts by the rate ratio. WASM always returns 44.1 kHz.
- **SID.cpp/h** — P-LAB A1-SID. Wraps **libresidfp** (GPL-2.0+, vendored, built as `libresidfp_static`). 6581 / 8580 swappable at runtime via Hardware → *A1-SID chip model*; the swap rebuilds the filter chain and restores the last-written register state. I/O `$C800-$CFFF` (29 registers, `addr & 0x1F`). Coexists with TMS9918 at `$CC00-$CC01` (VDP wins via bus priority 10). **A1-AUDIO Special Edition** (Claudio Parmigiani, 10 units) shares the same `pom1::SID` instance but registers a second bus handle at `$CC00-$CC1F` (same `& 0x1F` decode). `setSIDEnabled` / `setSIDSpecialEditionEnabled` evict each other (one chip socket); SE additionally evicts TMS9918 on plug-in.

  **Cycle-driven, SPSC-buffered audio.** `setSamplingParameters(POM1_CPU_CLOCK_HZ, DECIMATE, actualOutputRate)` at boot — the *actual* miniaudio-negotiated rate. `Memory::advanceCycles()` calls `SID::advanceCycles(cycles)` after every CPU slice; that runs `chip->clock(batch, staging)` in 4096-cycle chunks, pushes int16→float samples into a 16 384-sample ring (atomic head/tail, drop-oldest on overflow). The audio callback's `fillAudioBuffer()` only drains the ring — no `chip->clock` on the audio thread, no mutex across the callback. `chipMutex` serialises register writes / `chip->clock()` / `setChipModel()` between UI and emulation threads. SID tempo tracks emulated CPU cycles, so the M6502 `run()` overshoot rule above matters.

  **WASM-specific** (see `third_party/libresidfp/CMakeLists.txt` + patches in `FilterModelConfig{6581,8580}.cpp`):
  - Emscripten link flag `-s STACK_SIZE=4194304` (4 MB) — libresidfp's filter-model tables overflow the default 64 KB stack at first `reSIDfp::SID` construction.
  - The filter-model table builders run **sequentially** under `__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__`. Upstream parallelises with `std::thread`, which throws `system_error 138 "thread constructor failed"` in single-threaded WASM. Sequential adds ~50 ms to cold start, stays under 100 ms.
- **MicroSD.cpp/h** — P-LAB microSD. 65C22 VIA at `$A000-$A00F` bridging an emulated ATMEGA. SD CARD OS ROM (8 KB EEPROM) at `$8000-$9FFF`. Handshake: PORTB bit 0 = CPU_STROBE, bit 7 = MCU_STROBE, PORTA = data. Maps host `sdcard/` as virtual FAT32; tagged filenames `NAME#TTAAAA` encode type + load address. Command handlers (dispatched by command-id from the firmware): `cmdRead` / `cmdLoad` (strict + fuzzy prefix match), `cmdWrite`, `cmdDir` / `cmdLs`, `cmdCd` (only nav op — supports `..`, absolute `/PATH`, relative, fuzzy leaf), `cmdDel`, `cmdMkdir`, `cmdRmdir`, `cmdPwd`, `cmdMount`. **All name-accepting commands resolve against `currentDirectory` only — no recursive search.** `getCurrentDirDisplay()` returns `"/" + currentDirectory` so the Wozmon-side prompt (`/PLAB>`, `/PLAB/MCODE>`) literally is the cwd — CD is the user's only navigation primitive. Regression-pinned by `tools/test_sdcard_subdir_navigation_telnet.py`.
- **CFFA1.cpp/h** — Rich Dreher's CompactFlash. 8 KB firmware ROM `$9000-$AFDF` (ID bytes `$CF`/`$FA` at `$AFDC`/`$AFDD`), ATA/IDE registers `$AFE0-$AFFF` (A4 not decoded → `$AFE0` mirrors `$AFF0`). Backs a ProDOS `.po`; emulates READ/WRITE SECTOR + SET FEATURE only (everything the firmware actually uses). Desktop auto-mount: `Memory` ctor probes `cfcard/cfcard.po` up three directories. Registered as two bus entries: read-only ROM + r/w registers.
- **JukeBox.cpp/h** — P-LAB Juke-Box. 32 kB EEPROM (28c256) at `$4000-$BFFF` (RAM-16 / ROM-32 jumper) or `$8000-$BFFF` (RAM-32 / ROM-16). Program Manager at `$BD00` (`&` prompt); Save Program at `$B800` writes RAM back to EEPROM when the RW jumper is on. v1 models only the 28c256 single-page case. Registered as TWO disjoint bus handles at priority 20 (`$4000-$BFFF` + `$8000-$BFFF`); exactly one enabled at a time, flipped by `setJukeBoxJumper()`. Mutually exclusive with CFFA1, microSD, Krusader, Wi-Fi Modem (all inside the Juke-Box window). Firmware signature: byte at file offset `$7D00` = `$A5` (first byte of the Program Manager per the EEPROM RW manual `BD00: A5`). Missing → Hardware window warns. Build `roms/jukebox.rom` via `doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py` (P-LAB's `2-packer.sh` produces subtly different layouts — prefer the Python script).
- **A1IO_RTC.cpp/h** — P-LAB A1-IO & RTC. 65C22 VIA at `$2000-$200F` (⚠ overlaps GEN2 HGR framebuffer — cards are mutually exclusive at the preset level) bridging an emulated ATMEGA32 driving DS3231 (date/time + temp), DS18B20 probe, 8 analog + 4 digital inputs, 16-bit shift-register digital output. Broadcast protocol: 24 registers pumped on a 100-cycle period with PORTB STROBE handshake.
- **WiFiModem.cpp/h** — P-LAB MODEM BBS. 65C51 ACIA at `$B000-$B003`, ESP8266 AT interpreter, Hayes AT (`AT` / `ATDT host:port` / `ATH` / `ATE0/1` / `ATI` / `ATZ`), TELNET IAC filtering + `CR LF→CR` strip, non-blocking TCP, baud simulation 50-19200, `+++` escape with 1 s guard, 4096-byte circular Rx buffer. `requestDisconnect()` is the UI-safe entry (calls `handleATH()` under `modemMutex`). Desktop only; WASM stubs return `NO CARRIER`.
- **TerminalCard.cpp/h** — P-LAB Terminal Card. Passive bidirectional bridge: sniffs `$D012` writes (hook in `Memory::memWrite`), injects keystrokes into `$D010`/`$D011`. TCP server on IPv4 loopback :6502 (IPv6 `::1` refused — `telnet localhost` falls back to `127.0.0.1`). Modes: 7-bit (CR→CRLF, optional uppercase via Ctrl-O/I) and 8-bit raw (Ctrl-T, fires in either mode so there's a way back). Controls: Ctrl-L clear, Ctrl-R reset. Each control also has an **ESC-prefixed alternate** (ESC T/O/L/R/I) for macOS/BSD tty line disciplines that eat Ctrl-T (status), Ctrl-O (discard), Ctrl-R (rprnt) before telnet sees them. `escapePending` tracks held-ESC state; unknown ESC sequences forward the ESC so ANSI still reaches the Apple 1. On accept: proactive `IAC WILL ECHO` + `IAC WILL SUPPRESS-GO-AHEAD` + `IAC DO SUPPRESS-GO-AHEAD` flips the client to character-at-a-time. Pending reset/clear use `std::atomic<bool>` consumed *outside* `stateMutex` to avoid deadlock with `EmulationController::runEmulationSlice()`. Desktop only.
- **SocketHandle.h** — Move-only RAII wrapper (`NativeSocket` = `SOCKET`/`int`) shared by WiFiModem + TerminalCard. Destructor closes the FD so exceptions between `socket()` and the explicit close don't leak.

## Key implementation details

### Memory-mapped I/O
- `$D010` (KBD) — last key with bit 7 set; reading clears the strobe (matches PIA 6821).
- `$D011` (KBDCR) — bit 7 = 1 when a key is ready.
- `$D012` (DSP) — write triggers the display callback + TerminalCard sniffer. Read returns bit 7 = 0 (ready) after the terminal-speed delay or bit 7 = 1 (busy) during it; busy counter clamped ≥ 0.

PIA 6821 incomplete decoding aliases `$D0xx` to `$D010-$D012` by the low 2 bits (both BASIC variants work). `memRead`/`memWrite` normalise before dispatch. Keyboard input is forced uppercase by default (`setKeyPressed`); the Terminal Card uses `setKeyPressedRaw()` to bypass.

### CPU execution
Three modes via EmulationController: **Stopped**, **Running** (`executionSpeed` cycles/frame — nominal 1× = `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` (17045), 2× = 34091, MAX = 1 000 000), **Step** (`stepCpu()`).

### Addressing modes
`Abs`, `AbsX`, `AbsY`, `Ind`, `IndZeroX`, `IndZeroY`, … store the resolved address in `op` (quint16). `Imm()` stores `programCounter` in `op`, so `memRead(op)` fetches the immediate. Every instruction goes through `memory->memRead(op)` / `memory->memWrite(op, value)` uniformly.

### Loading programs
- `Memory::loadBinary(filename, startAddress)` — raw binary at the given address.
- `Memory::loadHexDump(filename, startAddress)` — Wozmon hex format. Supports comment lines + inline `//` / `#` / `;` (the inline strip is what prevents mnemonic letters like `LDA`/`DEX` being parsed as data), continuation lines, `T` prefix (turbo), `X` marker, `R` suffix (run address). Also handles single-line files where data merges with addresses (e.g. `ED0300:` → data `ED` + address `0300`).
- File dialogs in `MainWindow_FileDialogs.cpp` export a memory range as binary or Woz hex.
- Clipboard paste feeds characters to the Apple 1 keyboard (4096-char cap).

### Out-of-range RAM enforcement
When the preset's `ramKB < 64`, accesses in `[ramKB*1024, $8000)` track as OOR and show in the status bar. **Strict mode** (Memory Settings → *Strict enforcement*) returns `$FF` on read and drops writes — matches a real Apple-1 with no RAM board there. Status-bar suffix: `OOR:N!` (strict + counter) or `[strict]` (strict + nothing yet).

### Memory Map window
16×16 grid (256 pages = 64 KB), colour-coded regions, KB labels, PC/SP indicators, hover tooltips guarded by `IsWindowHovered`, legend, I/O register details, live CPU vector readout. In `MainWindow_DebugWindows.cpp`.

## Memory Map

```
$0000-$00FF  Zero page
$0100-$01FF  Stack
$0200-$1FFF  User RAM (programs typically load at $0280 or $0300)
$2000-$200F  A1-IO RTC VIA 65C22 (mutually exclusive with GEN2 below)
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
$C800-$CFFF  A1-SID (29 registers, `& 0x1F`)
$CC00-$CC1F  A1-AUDIO SE (same chip, window relocated; excludes TMS9918)
$CC00/$CC01  TMS9918 DATA / CTRL (wins over A1-SID; SE evicts TMS9918)
$D010        KBD           (aliases $D0F0, $D030, …)
$D011        KBDCR         (aliases $D0F1, $D031, …)
$D012        DSP           (aliases $D0F2, $D032, …)
$E000-$EFFF  Integer BASIC ROM (4 KB)
$FF00-$FFFF  Woz Monitor ROM + vectors (NMI/Reset/IRQ at $FFFA-$FFFF)
```

## Platform notes

- **CMake** — `find_package(glfw3 CONFIG)` first (vcpkg, Homebrew), falls back to `pkg_check_modules` (apt/dnf/pacman).
- **Windows** — Visual Studio C++ workload + CMake + Git + vcpkg. MSVC flags: `/utf-8`, `_CRT_SECURE_NO_WARNINGS`. `package_windows_release.bat` builds the standalone release archive (DLLs, ROMs, software, fonts, sdcard, cassettes, pic, docs).
- **macOS** — links Cocoa + IOKit + CoreVideo. `GLFW_OPENGL_FORWARD_COMPAT` is set only here. GLFW window icon is a no-op — OS pulls it from the `.app` bundle.
- **Linux** — `setup_imgui.sh` supports apt, dnf, pacman.

`build/`, `build-wasm/`, and `imgui/` are gitignored.

## Testing

Five `ctest` targets registered from `tests/CMakeLists.txt` (native-only, opt-out via `-DPOM1_ENABLE_TESTS=OFF`). Run from `build/`:

```bash
ctest                                    # all five (~10 s)
ctest --output-on-failure                # stdout/stderr on regression
ctest -R klaus -V                        # single test, verbose
```

- **`klaus_6502_functional`** — [Klaus Dormann's 6502 test](https://github.com/Klaus2m5/6502_65C02_functional_tests) vs. `M6502`. `file(DOWNLOAD)` fetches the 64 KB image once at configure time (SHA-256 pinned). Runner flips `Memory::setTestMode(true)` (flat RAM), sets `PC = $0400` (the image's reset-vector hits an error trap — callers must jump directly), steps until a `JMP *` trap. Success = final PC = `$3469`. ~1.5 s at 200 M max steps. Found `PHP` missing bits 4+5 and `BRK` pushing PC+1 instead of PC+2 on first integration; CPU refactors are gated by this test.
- **`peripheral_bus_smoke`** — 6 assertions vs. `PeripheralBus` with fake lambdas, no `Memory`, no peripherals (< 10 ms). Pins: page-mask fast-path miss, basic read routing, priority ordering at SID ↔ TMS9918 overlap, `setEnabled(false/true)` round-trip, sniffer pass-through (`onWrite = {}` → `tryWrite` returns false so the byte lands in RAM).
- **`sid_audio_smoke`** — full `Memory` + peripheral core, enables SID, writes a voice-1 tone through the bus, clocks the CPU, asserts non-silent samples in the SID ring. Catches regressions in the cycle-driven audio path (`SID::advanceCycles`, ring head/tail, `setSamplingParameters` plumbing).
- **`aci_tape_loading`** — full pulse-extraction → ACI ROM → RAM pipeline on `cassettes/BASIC.ogg`; asserts the Integer BASIC signature (`4C B0 E2`) lands at `$E000`. Gate for any change to pulse extraction, ACI ROM emulation, or keyboard-input wiring. Also validates `kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ`. Needs `${CMAKE_SOURCE_DIR}` cwd so `roms/` + the `.ogg` resolve.
- **`aci_tape_saving`** — drives ACI WRITE (Wozmon `<from>.<to>W`), saves the capture to `.aci` and `.wav`, reloads each in a fresh `Memory`, asserts bytes come back byte-for-byte. Catches regressions in `CassetteDevice::toggleOutput()`, `saveAciTape`/`saveWavTape`, `kTapeFileTimebaseHz` ↔ `kWavFileSampleRate` round-trip math.

New invariant tests follow `tests/peripheral_bus_smoke_test.cpp` — `<cassert>` + `add_test` is enough; GTest/Catch2 only earn their keep once multi-threaded tests land.

**Manual telnet tests** in `tools/test_*_telnet.py`. Two of them auto-launch POM1 themselves (the rest expect the user to start it separately):

- **`test_sdcard_subdir_navigation_telnet.py`** — pins the SD CARD OS "commands only search `currentDirectory`" invariant. Launches `build/POM1 --preset 4 --terminal --cpu-max`, creates `sdcard/testdir/HELLO#040300` in setUp, asserts `LOAD HELLO` / `DEL HELLO` fail at `/`, succeed after `CD TESTDIR`, verifies the host file is actually deleted, then `CD ..` → `LOAD HELLO` fails again. Docstring at the top lists every SD CARD OS command bound to `currentDirectory` so future readers don't have to reread `MicroSD.cpp`. Run from the repo root: `python3 tools/test_sdcard_subdir_navigation_telnet.py`.

## Version string locations

When bumping the version, update **all** of these:
- `main_imgui.cpp` — console output + GLFW window title
- `MainWindow_Dialogs.cpp` — About dialog
- `Screen_ImGui.cpp` — Apple 1 welcome screen
- `build-wasm/shell.html` — `<meta description>`, `<title>`, `<h1>` banner (3 occurrences)
- `README.md` — title + intro
- `package_windows_release.bat` — release ZIP filename
