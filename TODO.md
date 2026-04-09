# TODO

This file tracks **open** work only. For shipped features, see git log
or the version notes referenced in `README.md`.

## Open

- [ ] **SID: Arkanoid (Galway) does not play** — ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split architecture needs more than the converter's static ISR detection.
- [ ] **SID: some IRQ-driven tunes still fail** — Players using computed or indirect ISR addresses (e.g. BMX Kidz) escape the LDA/LDX/LDY + STA/STX/STY pattern matcher.
- [ ] **SID: implement "PIANO" software** — Bundle/port the P-LAB *SID Keyboard Program* per the [Apple-1 SID Interface Addendum PDF](https://p-l4b.github.io/A1-SID/APPLE-1_SID_PIANO.pdf). Player starts at `$0600` (`0600R`); keyboard mapping (`Z X C V B N M ,` + sharps `S D G H J`; waveforms: `O` noise, `P` pulse, `T` triangle, `W` saw; octaves: numerals); parameters in RAM around `$0280` are copied to SID registers at `$C800+`. Optional Theremin mode (`*`) reads paddles at `$C819/$C81A`.
- [ ] **Applesoft (float) + microSD support** — Integrate [`nippur72/applesoft-lite-sdcard`](https://github.com/nippur72/applesoft-lite-sdcard): an Applesoft variant with floating point and `LOAD`/`SAVE` via the P-LAB microSD interface. Bundle the ROM (e.g. under `software/basic/` or `roms/`), document load/entry points, verify it coexists with POM1's memory map, and add a quick test plan (save → reboot → reload).
- [ ] **GEN2 higher-resolution maze** — 16-bit DFS with smaller pixel blocks (e.g. 34×23 cells). Non-byte-aligned rendering currently produces NTSC color artifacts instead of solid white walls — needs sub-byte rendering.
- [ ] **More GEN2 programs** — image viewers, drawing tools, additional 280×192 HIRES demos.
- [ ] **More P-LAB TMS9918 software** — compile and bundle additional demos (anagram, graphs, life, hello-world) from [apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib). Requires KickC.
- [ ] **CodeTank daughterboard ROM** — support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.
- [ ] **Misc programs reference (Angela / P-Lab)** — curated ports (Dobble, Oregon Trail, etc.) from [angela](https://p-l4b.github.io/angela/).
- [ ] **Native file dialog** — file loading/saving still uses the in-app browser instead of system file pickers.

## Technical debt & code quality (audit April 2026)

### Performance
- [ ] **Snapshot RAM copy 64 KB @ 60 Hz** (`EmulationController::publishSnapshotLocked()`) — full RAM copy under `stateMutex` every frame, ≈ 3.8 MB/s wasted. → Double-buffer + atomic pointer swap, or page-level dirty tracking.
- [ ] **`float tmpBuf[512]`** in `AudioDevice.cpp` — too small; audio callbacks can request up to 2048 frames and the excess is silently truncated. → Pre-allocated member buffer sized dynamically.
- [ ] **TMS9918 VRAM 16 KB copied per frame** (`TMS9918::copySnapshot`) even when the card is disabled. → `vramDirty` flag, copy only on change.
- [ ] **`std::vector<quint8>(0x10000)` allocated per frame** for `EmulationSnapshot` in `MainWindow_ImGui.cpp`. → Persistent pre-allocated snapshot, swap pointers, publish only CPU registers + RAM pointer.

### Thread safety
- [ ] **Race on `runRequested`** (`EmulationController.cpp:565`) between `.load()` and `cpu->run()`: a `stopCpu()` call can be missed. → Capture state under mutex before running the slice.
- [ ] **Implicit lock order `stateMutex` → `keyMutex`** (`processQueuedKeysLocked()`) is undocumented; future inversion could deadlock. → Annotate `REQUIRES(stateMutex)` or merge into a single thread-safe queue.
- [ ] **`Screen_ImGui::instance` is a non-atomic static pointer** (`Screen_ImGui.cpp:21`) — race on concurrent access. → `std::atomic<Screen_ImGui*>` or pass `this` via opaque context.

### Architecture
- [ ] **`Memory` is a god object** — 15+ peripheral `unique_ptr`s, matching enable flags, inline I/O dispatch. → Extract a `PeripheralBus` with dynamic registry; `Memory` only manages the address space.
- [ ] **`EmulationController` SRP violation** — 50+ public methods (CPU, ROMs, snapshots, keyboard, tape, reset…). → Split into `SaveStateManager`, `KeyboardController`, `RomLoader`.
- [ ] **`MainWindow_ImGui.cpp` is monolithic** (~2500 lines) — UI + events + state + dialogs + rendering in one class. → Per-dialog classes; machine presets in external JSON/YAML.
- [ ] **Static `Screen_ImGui::displayCallback`** couples UI to emulation. → `DisplayDevice` interface injected into `Memory`; `Screen_ImGui` implements it.

### Network & peripherals
- [ ] **`connectToHost()` with no timeout** (`WiFiModem.cpp`) — slow networks can block the emulation thread indefinitely. → Non-blocking socket + `select()`/`poll()` with explicit timeout.
- [ ] **Silent ACIA Rx overrun** (`WiFiModem.cpp`, `TerminalCard.cpp`) — overflowed bytes are dropped; the real ACIA's overrun bit is not emulated. → Set the Receiver Overrun flag in STATUS.
- [ ] **Sockets not RAII-wrapped** — an exception between `socket()` and `close()` leaks the FD. → `SocketHandle` class with destructor.

### Code quality
- [ ] **`sscanf` without validation** (`MemoryViewer_ImGui.cpp:71,79,240`) — vulnerable to malformed input. → `std::from_chars`.
- [ ] **Archaic `typedef`** (`Memory.h:37-38`) — `typedef uint8_t quint8` → `using quint8 = uint8_t`.
- [ ] **Direct `std::cout`/`cerr`** — no level filtering, can't redirect to the debug UI. → Minimal `Logger` interface, dependency-injected.
- [ ] **`ma_device` allocated via `new`** (`AudioDevice.cpp:154`). → `std::unique_ptr<ma_device>` with custom deleter.
- [ ] **`stringBuffer` without `reserve()`** (`MicroSD.cpp`) — repeated reallocations during MCU response accumulation. → `stringBuffer.reserve(256)` in the constructor.

### Tests
- [ ] **No unit tests** — refactors risk regressing 6502 emulation silently. → Klaus Dormann's 6502 functional tests as the first smoke test; add GTest or Catch2.
- [ ] **Parsers not fuzzed** — `loadHexDump()` and `executeATCommand()` accept external input untested. → LibFuzzer targets via a CMake option (`ENABLE_FUZZING`).
