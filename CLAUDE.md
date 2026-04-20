# CLAUDE.md

Guidance for Claude Code working in this repository.

## Project Overview

POM1 is an Apple 1 emulator built with Dear ImGui. It emulates the MOS 6502 CPU and the original Apple 1 hardware (display, keyboard, ACI cassette), plus a stack of expansion cards: Uncle Bernie's GEN2 Color Graphics Card (HIRES, NTSC artifact color), the P-LAB A1-SID Sound Card (MOS 6581/8580), the P-LAB Apple-1 Graphic Card (TMS9918 VDP), the P-LAB microSD Storage Card (65C22 VIA + ATMEGA), the P-LAB MODEM BBS (65C51 ACIA + TCP/TELNET), the P-LAB Terminal Card (TCP server), the P-LAB A1-IO Board & RTC (65C22 VIA + ATMEGA32 + DS3231), the CFFA1 CompactFlash Interface (Rich Dreher, ATA/IDE + ProDOS `.po` image), and the P-LAB Apple-1 Juke-Box (Claudio Parmigiani, memory-mapped EEPROM program library). UI is in English. Builds on Linux, macOS, Windows, and Web (Emscripten/WASM).

User-facing feature list, install instructions, ROM table, keyboard shortcuts, screenshots, and the software library all live in **`README.md`**. Open work and tech debt live in **`TODO.md`**. Past releases are in `git log`.

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

`CMAKE_EXPORT_COMPILE_COMMANDS` is on — `cmake ..` writes `build/compile_commands.json` for clangd. A symlink at the repo root points to it, so no IDE-side config is needed.

### CLI flags for headless / automated runs

`pom1_imgui` parses its CLI in `main_imgui.cpp` (top of `main`). Flags exist so scripts under `tools/test_*_telnet.py` can drive the emulator in the background:

| Flag | Effect |
|------|--------|
| `--list-presets` | Print `index: name` for every preset in `kMachinePresets[]` and exit (0) |
| `--preset <N\|name>` / `-p <N\|name>` | Select preset by numeric index or case-insensitive substring match. Applied on the first render frame |
| `--terminal` | Force-enable the Terminal Card on top of the preset (binds `127.0.0.1:6502`) |
| `--tape <path>` | Preload a cassette file right after the preset applies, and **auto-press Play**. If omitted, `cassettes/WOZ_talk.mp3` is probed and loaded silently (user must press Play). |
| `--save-tape <path>` | Dump the cassette deck's recording to this path on clean shutdown. Works with `SIGINT`/`SIGTERM` — the signal handler asks the main loop to close the window so `~MainWindow_ImGui` runs. |
| `--cpu-max` | Pin `executionSpeed` to 1 000 000 cycles/frame on boot (the MAX button). Scripted ACI tests would otherwise wait ~30 s of wallclock per tape at 1×. |

Typical telnet-test workflow from `build/`:

```bash
./pom1_imgui --list-presets                              # find the index you want
./pom1_imgui --preset 10 --terminal &                    # Juke-Box + Terminal Card on :6502
sleep 3 && python3 ../tools/test_jukebox_telnet.py       # drive it
```

Preset name matching is a substring; the **first** case-insensitive hit wins. Use the numeric index when precision matters.

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
| `pic/`     | `pic/`            |
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

Each `.cpp/.h` pair owns one concern. The descriptions below cover only what isn't obvious from reading the file.

### Core
- **M6502.cpp/h** — MOS 6502 CPU. `op` is `quint16` for 16-bit address handling across all addressing modes; `tmp` is `int` for carry/borrow detection via bit 8. BCD ADC propagates the low→high carry from `(accumulator & 0xF0)` (bit 4 of the BCD-adjusted accumulator), not from the unadjusted sum. **`run(maxCycles)` returns the actual cycle count executed** (overshoots `maxCycles` by up to 6 cycles per loop iteration, since the loop only checks the budget between instructions); callers pacing against a wallclock budget must deduct the returned value, not the requested one — see SID notes below. A 12-slot PC ring buffer is filled on every instruction (cheap, always on); `dumpPcTrace(tag)` flushes it to the logger. `setDebugBrkTrace(true)` additionally dumps registers, stack, and the full trace on every `BRK` — useful for diagnosing stray jumps and reset loops. `setProgramCounter(pc)` is reserved for the Klaus test harness, which jumps directly past the image's reset-vector error trap.
- **CpuClock.h** — Nominal 6502 clock **`POM1_CPU_CLOCK_HZ` = 1 022 727** (~1.022727 MHz, 14.31818 MHz ÷ 14). Exports `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` / `POM1_CPU_CYCLES_PER_FRAME_2X_60HZ` and `POM1_CPU_CYCLES_PER_MILLISECOND`. Consumed by EmulationController/UI speed, SID, TMS9918 frame tick, modem baud timing and `+++` guard, Terminal Card polling, microSD idle timeouts, cassette realtime timebase, and terminal display delay.
- **Memory.cpp/h** — 64 KB address space. Owns every peripheral (`unique_ptr` + enable flag). Memory-mapped I/O dispatches via `PeripheralBus` (see below); `memRead`/`memWrite` only handle PIA 6821 ($D010/$D011/$D012) + `$D0xx` aliasing, ROM write-protection via `writeInRom`, OOR strict mode, cassette write toggle (sniffer), `DisplayDevice::onChar` forwarding + TerminalCard hook, then raw `mem[]`. `setKeyPressedRaw()` bypasses forced uppercase for the Terminal Card. **`setTestMode(true)`** collapses the whole read/write path to a single `mem[address]` load/store — no peripheral bus, no PIA, no ROM protection, no OOR — used exclusively by the Klaus Dormann test harness which expects flat 64 KB RAM; never enable in production.
- **DisplayDevice.h** — abstract `onChar(char)` sink for `$D012` writes. `Screen_ImGui` implements it; `EmulationController::createPom1()` injects the screen into `Memory::setDisplayDevice()`. Replaces the pre-existing C-style callback + atomic singleton — unit tests and future peripherals (Terminal Card mirror, logger) can tee display output without touching `Memory`.
- **PeripheralBus.h/cpp** — central I/O dispatch table. Each peripheral registers `(name, range, priority, onRead, onWrite)` at `Memory` ctor. Hot path is O(1): `pageMask[256]` bitmap pre-computed by `rebuildPageMask()` on every register/setEnabled → `tryRead`/`tryWrite` read one mask byte, branch on zero (fast-path miss = 99 %+ of CPU accesses), otherwise dispatch to the handler(s) hit. Priority-sorted by `std::stable_sort` so ties respect insertion order — TMS9918 wins over SID at `$CC00/$CC01` via priority 10. Empty `onWrite` = pass-through (let raw RAM handle it); explicit no-op `onWrite` = block (CFFA1 ROM). Cassette write toggle stays inline in `Memory::memWrite` because it's a sniffer (the byte must still land in `mem[]`).

### Emulation orchestration
- **EmulationController.cpp/h** — façade owning the M6502 + Memory + emulation thread. Public API exposes ~55 methods (CPU control, ROM reload, snapshot, hardware enable/disable, keyboard, tape) but most logic delegates to focused components below. Mutex order: **`stateMutex` > `keyboard.keyMutex` > `publisher.snapshotMutex`**. `stateMutex` is a `PriorityMutex` (custom wrapper around `std::mutex` exposing a waiter count) — in MAX speed the emulation loop calls `std::this_thread::yield()` after each slice iff `stateMutex.hasWaiters()`, which prevents the scheduler from re-granting the lock to the emulation thread while the UI thread is queued. Desktop slice cap is `kMaxSliceCycles = 6000` (~50 µs mutex hold at ~120 MHz emulated throughput); WASM uses 50 000 because it runs single-threaded from the main loop and needs to burn its whole frame budget per pump call. The native build runs an `emulationThread` consuming `runEmulationSlice()`; WASM has no thread — `pumpEmulationMainThread()` advances from the main loop.
- **EmulationSnapshot.h** — UI-ready immutable picture of the emulator state (memory, CPU registers, peripheral snapshots, OOR flag). Sized once at startup so `SnapshotPublisher::publish()` does an in-place memcpy into `latestSnapshot.memory.data()`, avoiding 64 KB alloc/free per frame.
- **SnapshotPublisher.h/cpp** — single-producer/single-consumer slot for `EmulationSnapshot`. `publish(Memory&, M6502&, bool)` runs under stateMutex and takes its own `snapshotMutex` to serialize the UI-thread `copyTo()`. **Page-level dirty copy:** `Memory::memWrite` sets one bit per touched 256 B page in `dirtyPages`; `publish()` walks the bitmap, collapses contiguous runs into single `memcpy`s, and clears the bitmap. A typical running program touches ~4-8 pages per frame → copy cost drops from 64 KB/frame to ~1-2 KB/frame. Idle Wozmon leaves the bitmap empty → zero copy. TMS9918's 16 KB copy is similarly skipped when the card is unplugged.
- **KeyboardController.h/cpp** — thread-safe key queue. `queueKey()` is UI-thread-safe; `drainTo(Memory&)` runs under stateMutex from the emulation slice and uses a `std::swap` to release `keyMutex` before calling `Memory::setKeyPressed()`.
- **RomLoader.h/cpp** — six static helpers (`reloadBasic/ApplesoftLite/WozMonitor/Krusader/AciRom/CFFA1Rom`) factoring the toggle-`writeInRom` + `Memory::loadXxx()` + restore pattern.
- **Disassembler6502.h/cpp** — standalone `pom1::disassemble6502(mem, pc, instrLen)` used by the debug console. 256-entry opcode table + addressing-mode formatter, no UI dependency.
- **Logger.h/cpp** — process-wide levelled logging (`pom1::log().info("Tag", "msg")`). `StreamLogger` writes to stdout (Debug/Info) / stderr (Warn/Error); `RingBufferLogger` captures the last N entries for the in-app debug console; `TeeLogger` chains two sinks. `pom1::initDefaultTeeLogger()` (called from `main()`) installs `Tee(stream, uiRingBuffer())` so every subsystem message lands in both places. The Debug Console "System Log" panel snapshots `pom1::uiRingBuffer()` with per-level colour and a min-level filter. All implementations are thread-safe (one mutex per logger).

### UI (ImGui)
- **main_imgui.cpp** — GLFW/OpenGL3 init (GL 3.2 Core, GLSL 150; WebGL 2 / GLSL ES 300 on WASM). `GLFW_OPENGL_FORWARD_COMPAT` is macOS-only. `SIGINT`/`SIGTERM` are rerouted into `glfwSetWindowShouldClose(1)` so `--save-tape` runs its shutdown dump from `~MainWindow_ImGui` instead of being killed mid-flight.
- **MainWindow_ImGui.cpp/h** — single class whose implementation is split across **9 TUs** sharing private helpers via `MainWindow_Internal.h` (namespace `pom1::mainwindow::detail`):
  - `MainWindow_ImGui.cpp` — ctor/dtor, `createPom1`/`destroyPom1`, `render()`, trivial action handlers (quit/reset/hardReset/configX/about), status helpers, CPU control.
  - `MainWindow_Layout.cpp` — toolbar icons (cassette, DIP chip, text label), monitor-tint cycle button, `layoutFitVideoViewport`.
  - `MainWindow_Presets.cpp` — `kMachinePresets[]` + `applyMachineConfig` + `getPresetCount/Name`. Migration target for external `presets.json`.
  - `MainWindow_Menu.cpp` — `renderMenuBar`/`renderToolbar`/`renderStatusBar`.
  - `MainWindow_Dialogs.cpp` — About (with 50th-anniversary Apple logo texture + Steve Jobs photo), Special Thanks, Welcome, Hardware/Software Reference, Display/Memory Settings. The Welcome panel is the FR/EN greeting shown right of the Apple 1 Screen on the first-boot default preset (flipped on by `applyMachineConfig` for the last preset only).
  - `MainWindow_HardwareWindows.cpp` — GraphicsCard/TMS9918/WiFiModem/TerminalCard/A1IO_RTC/Juke-Box windows. GL textures for TMS9918 (256×192 RGBA, `GL_NEAREST`).
  - `MainWindow_FileDialogs.cpp` — Load/Save Memory + Tape, Cassette Control (legacy), Paste Code, auto-enable-by-directory heuristics.
  - `MainWindow_DebugWindows.cpp` — Debug Console + Memory Map (16×16 grid, PC/SP indicators, BRK trace panel).
  - `MainWindow_Keyboard.cpp` — `shortcuts[]` table + `handleGlfw{Char,Key}`. Keys flow straight from GLFW callbacks to the Apple 1, bypassing `InputQueueCharacters`. PRESS vs. REPEAT is tagged by `handleGlfwKey` into `nextCharIsRepeat` for the immediately following `handleGlfwChar`; with Hardware → "Keyboard autorepeat" off (default, matches TTL keyboards), REPEAT events are dropped for printable chars and Enter/Backspace/Escape. F7 (single-step) honours REPEAT for hold-to-step.
  
  **`applyMachineConfig(int)`** unplugs every card, optionally `hardReset()`s (skipped on the very first invocation — `Memory::Memory()` already initialised), sets UI flags immediately, then queues a deferred *plug* via `pendingCardEnableFrames = kCardEnableDeferFrames` (15 frames ≈ 200 ms). The defer prevents a card from latching onto the mixer/bus before the CPU has run any cycle — the symptom first appeared as a silent SID on boot. `applyPendingLayout(const char*)` runs before each window's `Begin()` and sets position + size with **`ImGuiCond_FirstUseEver`**: the preset ships a default layout, but once a user has dragged/resized a window their position is persisted to `imgui.ini` and takes precedence on every subsequent launch (delete `imgui.ini` to restore the shipped layout). For the default POM1 preset, `applyMachineConfig` also flips `showCassetteDeck = true` and `showWelcome = true` so the cassette deck and the Welcome greeting panel appear right of the Apple 1 Screen on first boot. On first frame only (`firstFrame` block in `render()`), `computePresetLayoutExtent(cfg, appleScreenFallbackSize)` collects the bounding box of every sized placement in the preset and `glfwSetWindowSize`s the OS window to contain them all. Auto-enable hardware cards by source directory of a loaded file (`software/sid/`, `software/hgr/`, `software/tms9918/`, `software/net/`, `sdcard/`); reloading from `software/net/` calls `wifiModemReset()` to drop any live connection.
- **CassetteDeck_ImGui.cpp/h** — procedural "real-looking" cassette deck widget (`class pom1::CassetteDeck_ImGui`, owned by `MainWindow_ImGui::cassetteDeck`). Draws chassis, speaker grille, mechanical counter (000–999 rollover), cassette window with spinning hubs, brand strip and piano-key transport (STOP/PLAY/REC/REW/FF + PAUSE) entirely with `ImDrawList` (no texture asset required — the Apple 50th-anniversary logo is an optional blit on the right-hand cassette label). Owns a small transport state machine with real interlock semantics: REC alone = REC+PLAY, PAUSE only latches on Play/Rec, STOP releases everything, EJECT only from Stopped, REW/FF release PLAY. Transport actions forward to `CassetteDevice` through `EmulationController`; the deck is strictly a UI layer — no emulation state lives here. Volume slider is tracked locally so rapid clicks accumulate deterministically.
- **Screen_ImGui.cpp/h** — 40×24 character grid. Two `characterRenderMode` modes (Apple1Charmap = bitmap glyphs from `roms/charmap.rom`; HostAscii = ImGui font). Three `monitorMode` tints (Green/Amber/Monochrome). Blinking `@` cursor (`fmod` to avoid float overflow), brightness/contrast. CRT effect is two passes: `drawCRTBackdrop()` paints phosphor-tinted alternating bright/dim rows **before** glyphs (colour bands don't bisect characters); `drawCRTScanlines()` paints the dark mesh **after** glyphs as 1-px `AddRectFilled` every 2 display pixels at integer Y coords (default `crtScanlineAlpha = 0.50`, all rects batch into a single draw call). `AddLine` is avoided: its AA rasterizer on macOS OpenGL 3.2 and WASM WebGL2 splits sub-2-px thickness over two display rows at partial coverage and halves the effective alpha. Integer Y also pins each dark row to a real display pixel so the pattern doesn't drift under glyphs when the window is resized.
- **MemoryViewer_ImGui.cpp/h** — Hex editor with color-coded regions, search, bookmarks, inline double-click editing.

### Peripherals
- **CassetteDevice.cpp/h** — Apple Cassette Interface (ACI). Woz ROM at `$C100-$C1FF`, I/O at `$C000-$C0FF` (`$C000` output flip-flop, `$C081` tape input). `AudioSource`-compatible: playback mixes into the shared audio stream via AudioDevice. Loads `.aci` (raw pulse-duration capture at `kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ`), `.wav`, and general audio formats (`.mp3`, `.ogg`) via vendored **miniaudio** decoder (resamples and mono-mixes on demand). `kWavFileSampleRate` is the export rate for `saveWavTape`; the tape-saving test pins the `.aci` ↔ `.wav` round-trip.
- **GraphicsCard.cpp/h** — Uncle Bernie's GEN2 Color Graphics Card. Passively reads RAM `$2000-$3FFF`; renders 280×192 HIRES with NTSC artifact color (violet/green for group 1, blue/orange for group 2, white between). Apple II-compatible non-linear scanline layout (`scanlineAddress()`). `rasterizeLine` is a 14 KB LUT lookup: `kHgrPixels[(colParity << 8) | byte]` returns 7 pre-resolved RGBA pixels per byte (built once via `computeIsolatedPixel` assuming the byte has no inter-byte neighbours), copied with `memcpy`; a second pass over the 39 inter-byte seams paints both sides white when bit 6 ↔ bit 0 are both lit (the only case the isolated-byte LUT gets wrong). The 9th index bit is `colParity`, **not** group2 — group2 is already `byte & 0x80`; what flips even↔odd colour assignment is the absolute screenX parity, which alternates between adjacent columns since 7 is odd.
- **TMS9918.cpp/h** — P-LAB Apple-1 Graphic Card. TMS9918A VDP, 16 KB VRAM, I/O at `$CC00`/`$CC01`. `renderToBuffer()` fills a 256×192 RGBA buffer (IM_COL32 byte order matches `GL_RGBA + GL_UNSIGNED_BYTE`); the UI uploads via `glTexSubImage2D` and displays with `ImGui::Image` for nearest-neighbour scaling at any window size. Compatible with [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib).
- **AudioDevice.cpp/h** — Central audio output. Owns the hardware (miniaudio on desktop, Web Audio ScriptProcessorNode on WASM). Defines `AudioSource` (`fillAudioBuffer(float*, int)`); mixes registered sources (CassetteDevice, SID) as mono float32 at the rate negotiated with the OS. `getActualSampleRate()` reports what miniaudio actually got (44.1 kHz requested, often 48 kHz on Apple Silicon) — cycle-driven sources like SID must use this value or their tempo drifts by the rate ratio. WASM always returns 44.1 kHz.
- **SID.cpp/h** — P-LAB A1-SID. Wraps **libresidfp** (cycle-accurate SID engine, GPL-2.0+, vendored under `third_party/libresidfp/` and built as `libresidfp_static`). 6581 / 8580 chip variants selectable at runtime via Hardware → "A1-SID chip model"; the swap rebuilds the filter chain and restores the last-written register state so a tune in flight keeps playing. I/O `$C800-$CFFF` (29 registers, addr `& 0x1F`). Coexists with TMS9918 at `$CC00-$CC01` (TMS9918 wins via PeripheralBus priority 10). **A1-AUDIO Special Edition** (Claudio Parmigiani's 10-unit run) shares the same `pom1::SID` instance but registers a second `PeripheralBus` handle at `$CC00-$CC1F` — same `addr & 0x1F` decode, no `baseAddress` plumbing in the chip. `setSIDEnabled` and `setSIDSpecialEditionEnabled` evict each other (single chip socket); SE additionally evicts TMS9918 on plug-in (shared `$CC00/$CC01` addresses).

  **Cycle-driven, SPSC-buffered audio path.** `setSamplingParameters(POM1_CPU_CLOCK_HZ, DECIMATE, actualOutputRate)` at boot — the *actual* miniaudio-negotiated rate, not 44.1 kHz (Apple Silicon often runs at 48 kHz). `Memory::advanceCycles()` calls `SID::advanceCycles(cycles)` after every CPU slice; that runs `chip->clock(batch, staging)` in 4096-cycle chunks and pushes int16→float samples into a 16384-sample ring buffer (atomic head/tail, drop-oldest on overflow). The audio callback's `fillAudioBuffer()` only drains the ring — no `chip->clock` on the audio thread, no mutex held across the callback. `chipMutex` serialises register writes / `chip->clock()` / `setChipModel()` between the UI and emulation threads. SID tempo is therefore tied directly to emulated CPU cycles — see the M6502 entry above for the `run()` overshoot rule that keeps the cycle budget honest.

  **WASM-specific notes** (see `third_party/libresidfp/CMakeLists.txt` and the patches inside `FilterModelConfig{6581,8580}.cpp`):
  - Emscripten link flag `-s STACK_SIZE=4194304` (4 MB) — libresidfp's filter-model tables overflow the default 64 KB stack at first `reSIDfp::SID` construction.
  - `FilterModelConfig{6581,8580}.cpp` carry a small POM1 patch that runs the 4–6 lookup-table builders **sequentially** under `__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__`. Upstream parallelises with `std::thread`, which throws `system_error 138 "thread constructor failed"` in single-threaded WASM. The sequential path adds ~50 ms to cold start but stays under 100 ms.
- **MicroSD.cpp/h** — P-LAB microSD Storage Card. 65C22 VIA at `$A000-$A00F` bridging the CPU to an emulated ATMEGA MCU. SD CARD OS ROM (8 KB EEPROM) at `$8000-$9FFF`. Handshake: PORTB bit 0 = CPU_STROBE, bit 7 = MCU_STROBE, PORTA = bidirectional data bus. Maps host `sdcard/` as virtual FAT32. Tagged filenames `NAME#TTAAAA` encode type + load address. Firmware: [apple1-sdcard](https://github.com/nippur72/apple1-sdcard).
- **CFFA1.cpp/h** — Rich Dreher's CompactFlash Interface for Apple-1. 8 KB firmware ROM at `$9000-$AFDF` (with ID bytes `$CF`/`$FA` at `$AFDC`/`$AFDD`), ATA/IDE registers at `$AFE0-$AFFF` (A4 not decoded → `$AFE0` mirrors `$AFF0`). Backs a ProDOS `.po` disk image; emulates READ/WRITE SECTOR + SET FEATURE only (everything else the firmware actually uses). Desktop auto-mount: the `Memory` constructor probes `cfcard/cfcard.po` up three directories. Registered as two PeripheralBus entries: read-only ROM + read/write registers.
- **JukeBox.cpp/h** — P-LAB Apple-1 Juke-Box (Claudio Parmigiani). Memory-mapped 32 kB EEPROM (28c256) wired at `$4000-$BFFF` (RAM-16/ROM-32 jumper) or `$8000-$BFFF` (RAM-32/ROM-16 jumper). The firmware **Program Manager** at `$BD00` exposes an `&` prompt for selecting programs bundled into the ROM; the **Save Program** at `$B800` writes BASIC / binary programs back to the EEPROM when the RW jumper is on. v1 models only the 28c256 single-page case — multi-page 29c020/29c040 bank switching (`P0..PF` / `S0..S1`) is not modelled because the MMIO bank-select register address isn't documented on P-LAB's public site. Registered as TWO disjoint `PeripheralBus` handles at priority 20 (`$4000-$BFFF` and `$8000-$BFFF`); exactly one is enabled at a time, flipped by `setJukeBoxJumper()`. Mutually exclusive with CFFA1, microSD, Krusader, and the Wi-Fi Modem (they sit inside the Juke-Box's address window). Firmware signature: byte at file offset `$7D00` = `$A5` (LDA zp — first byte of the Program Manager per the EEPROM RW manual screenshot `BD00: A5`). Missing signature → Hardware window warns the user. Firmware blob installed as `roms/jukebox.rom`; built via `doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py` (P-LAB's `2-packer.sh` produces subtly different layouts — prefer the Python script).
- **A1IO_RTC.cpp/h** — P-LAB A1-IO Board & RTC. 65C22 VIA at `$2000-$200F` (⚠ overlaps the GEN2 HGR framebuffer — the two cards are mutually exclusive at the preset level) bridging to an emulated ATMEGA32 that drives a DS3231 RTC (date/time + internal temperature), a DS18B20 probe, 8 analog inputs, 4 digital inputs, and a 16-bit shift-register digital output. Broadcast protocol: 24 registers pumped on a 100-cycle period with PORTB STROBE handshake. Board reference: [A1-IO_RTC](https://p-l4b.github.io/A1-IO_RTC/).
- **WiFiModem.cpp/h** — P-LAB MODEM BBS. 65C51 ACIA at `$B000-$B003`, ESP8266 AT command interpreter, Hayes AT (AT, ATDT host:port, ATH, ATE0/1, ATI, ATZ), TELNET IAC (WILL/WONT/DO/DONT, subnegotiation filter, CR+LF→CR strip), non-blocking TCP, baud-rate simulation 50–19200, `+++` escape with 1 s guard (`POM1_CPU_CLOCK_HZ` cycles), 4096-byte circular Rx buffer. Public `requestDisconnect()` is the UI-thread-safe entry point (calls `handleATH()` under `modemMutex`). Desktop only (`#if !POM1_IS_WASM`); WASM stubs return `NO CARRIER`.
- **TerminalCard.cpp/h** — P-LAB Terminal Card. Passive bidirectional bridge: eavesdrops on `$D012` writes (sniffer hook in `Memory::memWrite`), injects keystrokes into `$D010`/`$D011`. TCP server bound to IPv4 loopback on port 6502 (IPv6 `::1` connections refused — `telnet localhost` falls back to `127.0.0.1` cleanly). 7-bit (CR→CRLF, optional uppercase via Ctrl-O/I) and 8-bit raw (Ctrl-T) modes; control: Ctrl-L clear, Ctrl-R reset. Ctrl-T fires even in 8-bit mode (otherwise no way back to 7-bit). Each control has an **ESC-prefixed alternate** (ESC T/O/L/R/I) — needed on macOS/BSD where the tty line discipline eats `Ctrl-T` (status), `Ctrl-O` (discard) and `Ctrl-R` (rprnt) before telnet can send them. The alternate state is `escapePending`; unrecognised ESC-sequences forward the held ESC and fall through so ANSI escape codes still reach the Apple 1. On accept, sends proactive `IAC WILL ECHO` + `IAC WILL SUPPRESS-GO-AHEAD` + `IAC DO SUPPRESS-GO-AHEAD` to flip the client into character-at-a-time mode. Pending reset/clear use `std::atomic<bool>` flags consumed outside `stateMutex` to avoid deadlock with `EmulationController::runEmulationSlice()`. Desktop only.
- **SocketHandle.h** — Move-only RAII wrapper (`NativeSocket` = `SOCKET`/`int`) used by WiFiModem and TerminalCard. Closes the FD in the destructor so exceptions between `socket()` and the explicit close cannot leak.

## Key implementation details

### Memory-mapped I/O
- **`$D010` (KBD)** — last key with bit 7 set; reading clears the keyboard strobe (matches PIA 6821).
- **`$D011` (KBDCR)** — bit 7 = 1 when a key is ready.
- **`$D012` (DSP)** — write triggers the display callback + TerminalCard sniffer. Read returns bit 7 = 0 (ready) after the terminal-speed delay, or bit 7 = 1 (busy) during the delay; busy counter clamped to ≥ 0.

PIA 6821 incomplete address decoding aliases all `$D0xx` to `$D010-$D012` based on the low 2 bits, so both BASIC variants work. `Memory::memRead`/`memWrite` normalise before dispatch. Keyboard input is forced uppercase by default (`setKeyPressed`); the Terminal Card uses `setKeyPressedRaw()` to bypass this.

### CPU execution
Three modes managed by MainWindow_ImGui via EmulationController: **Stopped**, **Running** (`executionSpeed` cycles per frame — nominal ~1.022727 MHz = `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` (17045), double = `POM1_CPU_CYCLES_PER_FRAME_2X_60HZ` (34091), Max = 1 000 000), and **Step** (`stepCpu()`). CPU getters are for the debug UI.

### Addressing modes
`Abs`, `AbsX`, `AbsY`, `Ind`, `IndZeroX`, `IndZeroY`, … store the resolved address in `op` (quint16). `Imm()` stores `programCounter` in `op`, so `memRead(op)` fetches the immediate value. All instructions go through `memory->memRead(op)` / `memory->memWrite(op, value)` uniformly.

### Loading programs
- `Memory::loadBinary(filename, startAddress)` — raw binary at the given address.
- `Memory::loadHexDump(filename, startAddress)` — Woz Monitor hex format. Supports comment lines and inline `//`, `#`, `;` comments (the inline strip is what prevents mnemonic letters like `LDA`, `DEX` being parsed as data), continuation lines, `T` prefix (turbo), `X` marker, `R` suffix (run address). Also handles single-line files where data merges with addresses (e.g. `ED0300:` is split into data `ED` + address `0300`).
- File dialogs in `MainWindow_FileDialogs.cpp` export a memory range as binary or Woz Monitor hex dump.
- Clipboard paste feeds characters to the Apple 1 keyboard (capped at 4096 chars).

### Out-of-range RAM enforcement
When the active preset's `ramKB < 64`, accesses in `[ramKB*1024, $8000)` are tracked as OOR and shown in the status bar. **Strict mode** (Memory Settings → "Strict enforcement") makes those reads return `$FF` and silently drops writes — matches a real Apple-1 with no RAM board in that region. Status-bar suffix: `OOR:N!` (strict + counter) or `[strict]` (strict + nothing yet).

### Memory Map window
16×16 grid (256 pages = 64 KB), color-coded regions, KB labels, PC/SP indicators, hover tooltips guarded by `IsWindowHovered`, legend, I/O register details, real-time CPU vector readout. Implementation in `MainWindow_DebugWindows.cpp`.

## Memory Map

```
$0000-$00FF  Zero page
$0100-$01FF  Stack
$0200-$1FFF  User RAM (programs typically load at $0280 or $0300)
$2000-$200F  A1-IO RTC VIA 65C22 (when the A1-IO Board is plugged — mutually exclusive with GEN2 below)
$2000-$3FFF  GEN2 HGR framebuffer (8 KB — when Uncle Bernie's GEN2 card is plugged)
$4000-$BFFF  Juke-Box ROM window (32 KB — when P-LAB Juke-Box is plugged, RAM-16/ROM-32 jumper)
$4000-$5FFF  User RAM (otherwise)
$6000-$7FFF  Applesoft Lite SD ROM (8 KB — loaded at $6000 by the Applesoft+microSD preset; `roms/applesoft-lite-microsd.rom` = Claudio Parmigiani's SD1.3 build, aligned with the SD1.3 `sdcard.rom` firmware. Cold start via `6000R` in the Woz Monitor.)
$8000-$BFFF  Juke-Box ROM window (16 KB upper-half — when Juke-Box RAM-32/ROM-16 jumper is selected)
$8000-$9FFF  SD CARD OS ROM (8 KB — when P-LAB microSD is plugged)
$9000-$AFDF  CFFA1 firmware ROM (~8 KB — when CFFA1 is plugged; shadows microSD ROM + BASIC low page)
$A000-$A00F  VIA 65C22 I/O (when P-LAB microSD is plugged)
$A010-$AFDF  User RAM (when neither microSD nor CFFA1 is plugged)
$AFE0-$AFFF  CFFA1 ATA/IDE registers (A4 undecoded: $AFE0 mirrors $AFF0; ID at $AFDC/$AFDD)
$B000-$B003  ACIA 65C51 I/O (when P-LAB MODEM BBS is plugged)
$B004-$BFFF  User RAM
$C000-$C0FF  Apple Cassette Interface I/O ($C081 tape input, $C000 output flip-flop)
$C100-$C1FF  Woz ACI ROM
$C800-$CFFF  A1-SID I/O (prototype, 29 registers, addr & $1F, when plugged)
$CC00-$CC1F  A1-AUDIO Special Edition I/O (same MOS chip, register window relocated; mutually exclusive with TMS9918)
$CC00/$CC01  TMS9918 DATA / CTRL (when plugged — overrides A1-SID prototype at those two addresses; A1-AUDIO SE evicts TMS9918)
$D010        KBD - Keyboard data        (aliases: $D0F0, $D030, ...)
$D011        KBDCR - Keyboard control   (aliases: $D0F1, $D031, ...)
$D012        DSP - Display              (aliases: $D0F2, $D032, ...)
$E000-$EFFF  Apple BASIC ROM (4 KB)
$FF00-$FFFF  Woz Monitor ROM (256 B) + vectors (NMI/Reset/IRQ at $FFFA-$FFFF)
```

## Platform notes

- **CMake** — `find_package(glfw3 CONFIG)` first (vcpkg / Homebrew), falls back to `pkg_check_modules` (apt/dnf/pacman).
- **Windows** — Visual Studio C++ workload + CMake + Git + vcpkg. MSVC flags: `/utf-8`, `_CRT_SECURE_NO_WARNINGS`. `package_windows_release.bat` builds a standalone release archive (DLLs, ROMs, software, fonts, sdcard, cassettes, docs).
- **macOS** — links Cocoa, IOKit, CoreVideo. `GLFW_OPENGL_FORWARD_COMPAT` is set only on macOS.
- **Linux** — `setup_imgui.sh` supports apt, dnf, and pacman.

`build/`, `build-wasm/`, and `imgui/` are gitignored.

## Testing

Five `ctest` targets registered from `tests/CMakeLists.txt` (native-only, opt-out via `-DPOM1_ENABLE_TESTS=OFF`). Run from the build dir:

```bash
ctest                                    # all five (~10 s total)
ctest --output-on-failure                # show stdout/stderr on regression
ctest -R klaus -V                        # single test, verbose
```

- **`klaus_6502_functional`** — runs [Klaus Dormann's 6502 functional test](https://github.com/Klaus2m5/6502_65C02_functional_tests) against the M6502 core. `file(DOWNLOAD)` fetches the 64 KB image once at configure time (SHA-256 pinned). Runner: `Memory::setTestMode(true)` (flat RAM), set `PC = $0400` (reset vector points at an error trap, so callers must jump directly), step until a `JMP *` trap is reached. Success = final PC equals `$3469`. ~1.5 s at 200 M max steps (~30 M instructions). Found `PHP` missing bits 4+5 and `BRK` pushing PC+1 instead of PC+2 on first integration; CPU refactors are gated by this test.
- **`peripheral_bus_smoke`** — 5 assertions against `PeripheralBus` with fake lambdas, no `Memory`, no peripherals (self-contained, < 10 ms). Pins: `pageMask` fast-path miss at unrelated addresses, basic read routing, priority ordering at SID ↔ TMS9918 overlap, `setEnabled(false/true)` round-trip, and sniffer pass-through (`onWrite = {}` → `tryWrite` returns `false` so the byte lands in RAM).
- **`sid_audio_smoke`** — instantiates the full `Memory` + peripheral core, enables SID, writes a voice-1 tone through the PeripheralBus, clocks the CPU, and asserts that the SID ring buffer produced non-silent samples. Catches regressions in the cycle-driven audio path (`SID::advanceCycles`, ring-buffer head/tail, `setSamplingParameters` plumbing).
- **`aci_tape_loading`** — runs the full pulse-extraction → ACI ROM → RAM pipeline on `cassettes/BASIC.ogg` and asserts the Integer BASIC signature (`4C B0 E2`) ends up at `$E000`. Gate for any change to pulse extraction timing, ACI ROM emulation, or keyboard-input wiring. Also validates the `kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ` invariant. Needs to run from `${CMAKE_SOURCE_DIR}` so `roms/` and the `.ogg` resolve.
- **`aci_tape_saving`** — drives the ACI WRITE routine (Wozmon `<from>.<to>W`), saves the pulse capture to both `.aci` and `.wav`, reloads each in a fresh `Memory`, and asserts bytes come back byte-for-byte. Catches regressions in `CassetteDevice::toggleOutput()`, `saveAciTape`/`saveWavTape`, and the `kTapeFileTimebaseHz` ↔ `kWavFileSampleRate` round-trip math.

The pattern for new invariant tests is in `tests/peripheral_bus_smoke_test.cpp`: `<cassert>` + `add_test` is enough for now. GTest/Catch2 only earn their keep once multi-threaded tests land.

## Version string locations

When bumping the version number, update **all** of these:
- `main_imgui.cpp` — console output and GLFW window title
- `MainWindow_Dialogs.cpp` — About dialog
- `Screen_ImGui.cpp` — Apple 1 welcome screen
- `build-wasm/shell.html` — `<meta description>`, `<title>`, and `<h1>` banner (3 occurrences)
- `README.md` — title and intro
- `package_windows_release.bat` — release ZIP filename
