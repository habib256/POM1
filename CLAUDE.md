# CLAUDE.md

Architecture / invariants / gotchas for the **emulator side** of POM1. User walkthrough → `README.md`; open work → `TODO.md`; history → `CHANGELOG.md` / `git log`. **Full doc map → [`doc/README.md`](doc/README.md)**.

**Contents:** [Overview](#project-overview) · [Build](#build--run) · [Architecture](#architecture) · [Invariants](#invariants--gotchas) · [Memory map](#memory-map) · [Testing](#testing) · [Version bump](#version-string-locations)

- Apple 1 software (BASIC, SID tunes, microSD shell tools, games) → `sketchs/doc/APPLE1DEV.md` + `sketchs/doc/Programming_Apple1_ASM.md`.
- CLI flags → [`doc/CLI.md`](doc/CLI.md) (impl: `CliDispatcher.cpp`).
- DevBench / cc65 details → [`doc/DEVBENCH.md`](doc/DEVBENCH.md) + [`doc/CC65_WASM.md`](doc/CC65_WASM.md).
- GEN2 HGR card → [`doc/GEN2_RELEASE.md`](doc/GEN2_RELEASE.md).
- 6502 ASM sources for every shipped program → `dev/` (`lib/{apple1,m6502,tms9918,gen2,gen2c,games,…}/`, `projects/<card>/<name>/`, `cc65/`). Compiled artefacts land under `software/<dir>/` — that's what POM1 loads. Release bundles omit `dev/`.

## Project Overview

Apple 1 emulator (Dear ImGui, MOS 6502 + display + keyboard + ACI cassette) plus expansion cards: Uncle Bernie's GEN2 HGR, P-LAB A1-SID (6581/8580), TMS9918, microSD (65C22+ATMEGA), **IEC daughterboard** (1541 drive on microSD's spare VIA pins), MODEM BBS (65C51+TCP), Terminal Card, A1-IO & RTC, Juke-Box, CodeTank, Rich Dreher's CFFA1, SWTPC GT-6144 + PR-40 (1976, Jobs' *Interface Age* mod). Linux / macOS / Windows / Web (Emscripten).

## Build & Run

```bash
./setup_pom1.sh             # one-time deps (Linux/macOS)
cd build && cmake .. && make
./run_emulator.sh            # runs from repo root
```

Windows: `setup_pom1.bat` + vcpkg + `cmake --build . --config Release`. `compile_commands.json` symlinked for clangd.

**WASM:** `source emsdk_env.sh && emcmake cmake .. && emmake make && emrun POM1.html`. MEMFS preloads `roms/ pic/ fonts/ software/ sdcard/ cassettes/ cfcard/cfcard.po` (other `.po` are desktop-only, >140 MB). Rebuild after any change under those trees or `build-wasm/shell.html`.

**cc65:** per-project Makefiles call `ca65`+`ld65`, then `python3 emit_*_txt.py` lands `.bin`+Woz-hex `.txt` under `software/<dir>/`. `make -C dev/projects` is the CI gate. Linker configs in `dev/cc65/`. The gen2c runtime is split into per-family C modules (`gen2_init.c`, `gen2_pixel.c`, `gen2_rect.c`, `gen2_text.c`, `gen2_sprites.c`, `gen2_geom.c`, `gen2_lores.c` — see `dev/lib/gen2c/gen2c.mk`) so ld65 dead-strips per family; hot paths stay in `gen2_blit.s`, which any project linking those modules must also assemble.

## Architecture

All C++ sources under **`src/`** (vendored deps in `src/third_party/`). `imgui/`, `tests/`, `tools/` stay at repo root. File references below are bare basenames — find them under `src/`.

### Parmigiani's golden rule — "one board at a time"

Claudio PARMIGIANI (P-LAB designer): on real hardware exactly ONE P-LAB card is plugged. The 6502 bus has no arbitration and many P-LAB cards overlap windows (A1-SID `$C800-$CFFF` vs TMS9918 `$CC00/$CC01`; A1-IO RTC `$2000-$200F` vs GEN2 HGR `$2000-$3FFF`; Juke-Box claims `$4000-$BFFF`). POM1 **breaks this on purpose** in the "Multiplexing Fantasy" preset — fantasy, not buildable. Mutex rules elsewhere mirror real bus conflicts. When adding a card, honour the rule and document any intentional coexistence.

### Core layers

- **M6502** (CPU). `op = quint16`, `tmp = int` (carry/borrow via bit 8). BCD ADC propagates low→high carry from `(A & 0xF0)`, not the unadjusted sum. `run(maxCycles)` **returns actual cycle count** (overshoot up to 6) — wallclock pacers must deduct. 12-slot PC ring + `dumpPcTrace(tag)`. `setTestMode(true)` = flat 64 KB for the Klaus harness only.
- **CpuClock.h** — `POM1_CPU_CLOCK_HZ = 1 022 727` (14.31818 MHz ÷ 14). Single source of truth.
- **Memory** — 64 KB. Owns every peripheral (`unique_ptr` + enable flag). MMIO dispatched via **`PeripheralBus`**; `memRead/memWrite` only handle PIA `$D0xx` aliasing, ROM write-protect, OOR strict mode, the cassette write-sniffer, `DisplayDevice::onChar` + TerminalCard hook, then raw `mem[]`. **Redundant-ROM-load guards**: skip Applesoft Lite reload when `mem[$FF00..$FF01] == D8 58`; skip SD CARD OS reload when `mem[$8000..$8001] == A9 00`.
- **PeripheralBus** — central MMIO dispatch. Peripherals register `(name, range, priority, onRead, onWrite)`. O(1) hot path via `pageMask[256]` bitmap. `std::stable_sort` by priority — TMS9918 wins over SID at `$CC00/$CC01` via priority 10. `onWrite = {}` → pass-through to RAM; explicit no-op = block (CFFA1 ROM).
- **EmulationController** — façade over CPU + Memory + emulation thread. **Mutex order**: `stateMutex > keyboard.keyMutex > publisher.snapshotMutex`. `stateMutex` is a `PriorityMutex` (MAX speed yields on `hasWaiters()`). Slice cap: 6000 cycles desktop, 50 000 WASM (single-threaded). Native = dedicated thread; WASM = `pumpEmulationMainThread()`.
- **SnapshotPublisher** — SPSC slot, page-level dirty copy (1 bit per 256 B page, contiguous runs → single memcpy). Idle Wozmon = zero copy. TMS9918's 16 KB skipped when unplugged.
- **RewindBuffer** — microM8-style timeline; operates on snapshot blobs (`Memory::saveSnapshotToBuffer`, same byte layout as the `.snap` file). KEYFRAME + DELTA frames anchored in segments; eviction drops whole leading segments past the budget (128 MB default). Touched only under `stateMutex`. Pinned by `rewind_buffer_smoke`.
- **DisplayDevice** — abstract `onChar(char)` sink for `$D012`. Injected via `Memory::setDisplayDevice()` so tests/peripherals can tee.

### UI (ImGui)

- **main_imgui.cpp** — GLFW/OpenGL3 (GL 3.2 Core / GLSL 150; WebGL 2 / GLSL ES 300 on WASM). `GLFW_OPENGL_FORWARD_COMPAT` macOS-only. SIGINT/SIGTERM → `glfwSetWindowShouldClose(1)` so `--save-tape` flushes.
- **MainWindow_ImGui** — single class across 9 TUs sharing `MainWindow_Internal.h`: `_Layout`, `_Presets` (`kMachinePresets[]`), `_Menu`, `_Dialogs`, `_HardwareWindows`, `_FileDialogs` (auto-enable-by-directory), `_DebugWindows`, `_Keyboard`, `_TMS9918Inspector`. Keyboard bypasses `InputQueueCharacters` — keys go GLFW → Apple 1 directly. With autorepeat off (default, matches TTL keyboards) REPEAT drops printable+Enter/Backspace/Escape; F7 hold-to-step honours REPEAT.
- **Screen_ImGui** — 40×24 grid; 2 charmap modes (Apple1/HostAscii), 3 monitor tints (Green/Amber/Mono). CRT scanlines = 1-px `AddRectFilled` every 2 display pixels at integer Y. `AddLine` avoided — its AA on macOS GL 3.2 / WebGL2 splits sub-2-px thickness across two rows and halves alpha.
- **Bench** (`Pom1BenchHost.cpp` here, portable `bench/CodeBench.cpp` + `BenchLang.cpp` + `IBenchHost.h` at repo root). Details → [`doc/DEVBENCH.md`](doc/DEVBENCH.md), WASM specifics → [`doc/CC65_WASM.md`](doc/CC65_WASM.md). Desktop uses bundled cc65 (`ca65/ld65/cl65`) resolved exe-relative first (`<exe>/cc65/bin`, macOS `<exe>/../Resources/cc65/bin`, AppImage `<exe>/../share/POM1/cc65/bin`) so packaged builds are self-contained. **WASM exposes only the Wozmon-hex target.**

### Peripherals

One `.cpp/.h` pair per card under `src/`. Bus windows + priorities are listed in the [Memory map](#memory-map) below; cycle/timing details live in each peripheral header. Card-specific docs:

- **GEN2 HGR** beam-race renderer + soft-switches → `doc/GEN2_RELEASE.md`, `doc/GEN2_RELEASE_questions.md`. Pinned by `gen2_floatingbus_smoke`, `gen2_softswitch_msb_smoke`, `gen2_beam_race_smoke`, `gen2_horizontal_split_smoke`.
- **TMS9918** sprite rules → `sketchs/doc/TMS9918-SPRITE_BEST_PRACTICES.md`, `sketchs/doc/TMS9918-SPRITE_INIT.md`, `sketchs/doc/Programming_TMS9918.md`.
- **CodeTank** — P-LAB ROM **daughterboard** of the TMS9918 card (no standalone bus presence). `Memory::setCodeTankEnabled(true)` cascade-plugs TMS9918; `setTMS9918Enabled(false)` cascade-unplugs CodeTank. ROM library at `roms/codetank/` (GAME1-3) rebuilt by `tools/build_codetank_rom.py`. GAME1 lower = Tetris drop-in (`software/Apple-1_TMS_CC65/tetris_codetank.bin`); GAME3 lower = LOGO V2.6 (`-D CODETANK_BUILD`). TEST + GAME4/LightCorridor carts retired June 2026.
- **SWTPC GT-6144 / PR-40** → `doc/SWTPC_GT-6144.md`, `doc/SWTPC_PR-40.md`.
- **IEC daughterboard** — no new MMIO; piggybacks on microSD's VIA PORTB bits 2-6 (SN7406 inverters). Wired-AND open-collector in `IECBus`; virtual 1541 in `Drive1541` backed by 174 848 B `.d64` files. Enabling IEC cascade-enables microSD.
- **SID** — wraps libresidfp (GPL-2.0+, vendored). Cycle-driven, SPSC ring (4096-cycle chunks → 16 384-sample ring, drop-newest on overflow so producer never touches `ringTail`). `chipMutex` serialises register writes / `clock` / `setChipModel`. **WASM**: 4 MB stack (`-s STACK_SIZE=4194304`) — filter tables overflow default 64 KB; filter builders run sequentially under `__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__` (~50 ms cold start).
- **TerminalCard** — TCP loopback :6502 (IPv6 `::1` refused). 7-bit and 8-bit raw modes. `Ctrl-S` / `ESC S` arms a screenshot; main render thread runs `glReadPixels` between `RenderDrawData` and `glfwSwapBuffers`, Y-flips, writes `screenshots/pom1_latest.png`. Result string uses `screenshotResultMutex` (render thread never blocks on `cardMutex`). Desktop only.
- **WiFiModem** — 65C51 + ESP8266 AT emulation, TELNET IAC filtering, non-blocking TCP. Desktop only; WASM stubs return `NO CARRIER`.

## Invariants & gotchas

### Presets (`kMachinePresets[]` in `MainWindow_Presets.cpp`)

`--preset N` indexes this array — **13 entries, indices 0-12**. The README preset table must stay in lockstep. Pinned by `preset_ram_profiles_smoke`.

- **Indices 0-2 are DevBench profiles** (CC65 / TMS9918 / GEN2 HGR Development Bench). Each MIRRORS an existing preset's machine config (cards + RAM + BASIC): CC65 ≡ "ACI & BASIC cassette" (8 KB), TMS9918 ≡ "TMS9918 (CodeTank)" (8 KB), GEN2 ≡ "GEN2 HGR Color" (48 KB). `kP1Targets[].preset` in `Pom1BenchHost.cpp` maps each (language × machine) target back to one of these.
- Listed FIRST in the Presets menu, but the **array still ends with POM1 Fantasy** so "default = last" holds. **Keep Fantasy last** — default-picker, banner, and OS window-size floor all key off the terminal index (`kMachinePresetCount - 1`).
- A1-SID / A1-AUDIO SE, A1-IO & RTC, Wi-Fi Modem, Juke-Box have **no dedicated preset** (removed June 2026) — plug from Hardware menu or `--enable {sid,rtc,wifi,jukebox}`. A1-SID card variant + I/O window (`$C800-$CFFF` vs `$CC00-$CC1F`) picked at runtime from Settings → "A1-SID version & addresses".
- **No TMS9918 preset without CodeTank** (daughterboard rule).

### `applyMachineConfig(int)`

- Unplugs every card, optional `hardReset()` (skipped first invocation), sets UI flags immediately, queues deferred plug via `pendingCardEnableFrames = kCardEnableDeferFrames` (15 frames ≈ 200 ms). Deferring past first CPU cycle fixes silent-card-on-boot.
- **ROM selection uses preset config, not live flags** — live flags still false at deferred plug time; microSD Applesoft Lite vs CFFA1 flavour picked from `cfg.microSD`/`cfg.cffa1` directly.
- `applyPendingLayout()` runs before `Begin()` with **`ImGuiCond_FirstUseEver`**. **Widgets must not call `SetNextWindowSize(..., FirstUseEver)`** — overrides preset. `SetNextWindowSizeConstraints` is OK.
- **Per-preset ini** `ini/imgui_preset_NN.ini` + `ini/preset_NN.size`. `io.IniFilename = nullptr`; POM1 manages files via `savePresetLayout`/`loadPresetLayout`.
- **Auto-enable by source dir** of loaded file: `software/Graphic HGR/` → GEN2; `Graphic TMS9918/` and `software/Apple-1_TMS_CC65/` → TMS9918; `SOUND SID/` → A1-SID (evicts A1-AUDIO SE + Juke-Box); `NET/` → Wi-Fi Modem; `sdcard/` → microSD; `a1io_rtc/` → A1-IO & RTC; `Graphic gt-6144/` → GT-6144. Each branch always raises the corresponding `show*` window flag.

### MMIO

- `$D010` (KBD) — last key with bit 7 set; read clears strobe.
- `$D011` (KBDCR) — bit 7 = 1 when key ready.
- `$D012` (DSP) — write triggers display callback + busy-counter except **raw `$7F`** (WOZ Monitor's reset-time `LDY #$7F / STY $D012` DDR setup would paint a spurious `_`). Real hardware latches only on PB7=1; POM1 is permissive so emulator-era demos calling WOZ ECHO with bit 7 clear still render. `$DF` via ECHO still yields `_`. TerminalCard + PR-40 sniffers get every write unfiltered.
- PIA 6821 incomplete decoding aliases `$D0xx` → `$D010-$D012` by low 2 bits. `setKeyPressed` forces uppercase; `setKeyPressedRaw()` bypasses it (Terminal Card).
- **`Memory::configureResetVectors(addr)` only writes `$FFFC/$FFFD` (RES)**. NMI (`$FFFA/$FFFB`=$0F00) and IRQ (`$FFFE/$FFFF`=$0000) stay at the authentic WozMonitor.rom values so P-LAB programs installing an IRQ trampoline at `$0000` route correctly.

### Other gotchas

- **`AudioDevice::getActualSampleRate()` returns the miniaudio-negotiated rate** (44.1 kHz requested, often 48 kHz on Apple Silicon) — cycle-driven sources must use this or tempo drifts. WASM always 44.1 kHz.
- **OOR (`ramKB < 64`)**: strict mode returns `$FF` and drops writes in `[ramKB*1024, $8000)` — matches real Apple-1 with no RAM board. Status bar: `OOR:N!` or `[strict]`.
- **`loadHexDump`** supports comments (`//` `#` `;`, strips inline — prevents `LDA`/`DEX` mnemonics being parsed as data), continuation lines, `T` prefix (turbo), `X` marker, `R` suffix (run address), and single-line files where data merges with addresses (`ED0300:` → data `ED` + addr `0300`).
- **ACI TAPE OUT vs bundled cassette** — `cassettes/WOZ_talk.mp3` is preloaded only when the **POM1 Multiplexing Fantasy (2026)** preset is applied (last preset index); Integer-BASIC cassette presets load `BASIC.aci` instead. While a stream-mode tape is inserted, live `$C0xx` toggles (`$C030` chiptune in GEN2 *A-1-CrazyCycle*) stay silent because `CassetteDevice::fillAudioBuffer()` never mixes the pulse queue. DevBench Run auto-ejects on ACI presets without the Integer-BASIC program tape.
- macOS: `pom1_macos_provision_user_data_dir()` creates `~/Library/Application Support/POM1/` with symlinks for read-only dirs + seeded `sdcard/`/`cfcard/`/`ini/` then chdirs there. Refreshed each launch (handles Gatekeeper App Translocation + `/Applications` drag-installs).

## Memory Map

```
$0000-$00FF  Zero page
$0100-$01FF  Stack
$0200-$1FFF  User RAM (programs typically load at $0280 or $0300)
$2000-$200F  A1-IO RTC VIA 65C22 (mutex with GEN2 below)
$2000-$3FFF  GEN2 HGR page 1 framebuffer (8 KB)
$4000-$5FFF  GEN2 HGR page 2 framebuffer ($C255 selects; mutex with CodeTank/Juke-Box windows)
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
$C250-$C257  GEN2 soft switches (READ toggles + returns HST0 in D7; writes no-op;
             mirrors across $C2/$C3/$C6/$C7xx where A4=1 — SEL = $Cxxx & !A11 & A9 & A4)
$C800-$CFFF  A1-SID (29 regs, & 0x1F)
$CA00        Juke-Box Px/Sx bank-select latch (write-only; mutex with SID)
$CC00-$CC1F  A1-AUDIO SE (excludes TMS9918)
$CC00/$CC01  TMS9918 DATA / CTRL (priority 10, wins over A1-SID)
$D00A        SWTPC GT-6144 command port (write-only; bus wins over PIA alias)
$D010-$D012  KBD / KBDCR / DSP (alias $D0F0/F1/F2, $D030/31/32, …)
$E000-$EFFF  Integer BASIC ROM
$FF00-$FFFF  Woz Monitor ROM + vectors ($FFFA-$FFFF)
```

## Testing

`ctest` from `build/` (native-only, opt-out `-DPOM1_ENABLE_TESTS=OFF`). Inventory in `tests/CMakeLists.txt`; `ctest -N` lists exact names. CMake never invokes `dev/projects/*/Makefile`; those are developer-only build steps. Tests load whatever artefact lives under `software/`.

```bash
ctest                       # full suite (~5–30 s wall time; Klaus + TMS9918 tests dominate)
ctest --output-on-failure
ctest -R klaus -V
```

Load-bearing pins worth knowing:

- **`klaus_6502_functional`** — Klaus Dormann's 6502 functional test. Gates all CPU refactors (functional only — no cycle counts).
- **`cpu_harte_smoke`** — Tom Harte "65x02 ProcessorTests" (`tests/cpu/harte_6502.bin`, 100 cases × 151 documented opcodes). **Cycle-exact** per-opcode oracle; complements Klaus by pinning page-cross/branch penalties + decimal-mode ADC/SBC + PLP/RTI P-bits.
- **`cpu_interrupt_smoke`** — IRQ/NMI line timing (7-cycle entry, vectors, pushed-P B bit, edge-cleared NMI, RTI restore).
- **`gfx_regress_gen2_testcard`** — headless golden-image graphics regression (`--dump-gen2-frame` + sha256 vs committed golden PNG; TMS9918 counterpart `--dump-tms-frame`). Harness `tools/test_gfx_regress.py` skips if Python3 / `build/POM1` absent.

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
