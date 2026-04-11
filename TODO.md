# TODO

This file tracks **open** work only. For shipped features, see git log
or the version notes referenced in `README.md`.

## Open

- [ ] **SID: Arkanoid (Galway) does not play** — ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split architecture needs more than the converter's static ISR detection.
- [ ] **SID: some IRQ-driven tunes still fail** — Players using computed or indirect ISR addresses (e.g. BMX Kidz) escape the LDA/LDX/LDY + STA/STX/STY pattern matcher.
- [x] **SID: implement "PIANO" software** — Bundled as `software/sid/piano.bin` (1104 bytes, `$0600-$0A4F`) and `sdcard/PIANO#060600`. Load via SD CARD OS: `RUN PIANO` or direct: `0600R`. Keyboard: `Z X C V B N M ,` + sharps `S D G H J`; waveforms `O P T W`; octaves `1-8`; `*` Theremin. Source: `software/sid/piano.asm`.
- [ ] **Applesoft Lite P-LAB microSD variant** — Current `applesoft-lite-cffa1.rom` is built with `cffa1.s` (txgx42/applesoft-lite); SAVE/LOAD inside Applesoft call CFFA1 firmware ($900C) which doesn't exist on P-LAB microSD presets → "NO CFF" error. Need a separate ROM for P-LAB where Applesoft disk commands route through SD CARD OS (VIA $A000) instead of CFFA1. **Action required:** ask Claudio Parmigiani (P-LAB) for the Applesoft Lite ROM variant shipped with the microSD card, or build one from txgx42/applesoft-lite without `cffa1.o`. Until then, P-LAB users must use the SD CARD OS shell (`8000R` → `ASAVE` / `LOAD` / `RUN`) for file persistence.
- [ ] **Uncle Bernie's Improved ACI** — Emulate the ACI Extended firmware page at `$C500-$C5FF` (256 B, started with `C500R`). Adds EOR checksum verification (`R`/`W`), extended format with 8-byte header at `$07F8-$07FF` (`RX`/`WX`), and autostart (`<from>` == `<to>`). Compatible Apple II checksum. **Firmware is proprietary and unpublished** — Uncle Bernie stated on Applefritter: *"The improved firmware I wrote for it will never be published."* PROMs were distributed only with his physical IC kits (now sold out). **Action required:** contact Uncle Bernie via [Applefritter PM](https://www.applefritter.com/user/254186/track) to request cooperation; precedent: he offered to share docs with the HoneyCrisp emulator developer. POM1 already emulates his GEN2 HGR card, which is a good argument. Code-side when ROM obtained: load at `$C500`, update `CassetteDevice` for extended format (header + checksum). Ref: [Applefritter thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [ACI improvements comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
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
