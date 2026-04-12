# TODO

Open work only. For shipped features, see `git log` or the version notes
referenced in `README.md`.

## Apple-1 ecosystem — hardware

### Storage & cassette

- [ ] **Uncle Bernie's Improved ACI** — Emulate the ACI Extended firmware page at `$C500-$C5FF` (256 B, started with `C500R`). Adds EOR checksum verification (`R`/`W`), extended format with 8-byte header at `$07F8-$07FF` (`RX`/`WX`), and autostart (`<from>` == `<to>`). Compatible Apple II checksum. **Firmware is proprietary and unpublished** — Uncle Bernie stated on Applefritter: *"The improved firmware I wrote for it will never be published."* PROMs were distributed only with his physical IC kits (now sold out). **Action required:** contact Uncle Bernie via [Applefritter PM](https://www.applefritter.com/user/254186/track) to request cooperation; precedent: he offered to share docs with the HoneyCrisp emulator developer. POM1 already emulates his GEN2 HGR card, which is a good argument. Code-side when ROM obtained: load at `$C500`, update `CassetteDevice` for extended format (header + checksum). Ref: [Applefritter thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [ACI improvements comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).

- [ ] **Uncle Bernie's Woz Machine floppy controller** — 5.25" Disk II-style floppy emulation for Apple-1. Major effort; do last. Replicates the Woz state machine (74LS299 shift register, 74LS259 soft-switches) plus the **Timing Fix Circuit** (GAL16V8) that absorbs Apple-1 DRAM-refresh cycle jitter so Wozniak's RWTS cycle-counting works despite the missing Q3 2 MHz clock. Needs: GCR track/sector emulation, `.dsk`/`.woz` image loading, soft-switch dispatch in `$C0Ex` range, asynchronous 74LS123-based drive clock. Parks at last in the pipeline — only worth it once we have original Apple-1 disk software to run (none known public as of now; would target ports/adaptations from the Apple II disk library).

### Print & voice

- [ ] **SWTPC PR-40 40-col printer** — Parallel-port matrix printer (5×7 dots, 40 col/line, 75 lpm). Render output to a scrolling "paper roll" window + append to a `.txt` file. Tiny effort, high nostalgia. Hooks into an 8-bit parallel output port (to be defined; closest existing socket is the Terminal Card TCP stream, which we can split).

- [ ] **Briel Multi I/O Board — SpeakJet vocal synthesis** — The 6522/6551 portions of the Briel board duplicate what P-LAB MicroSD / Modem already expose. The unique value is the **SpeakJet** phoneme chip socket: route the UART byte stream through a TTS bridge (eSpeak or macOS `say`) to give the Apple-1 a synthetic voice. Implement as a separate optional peripheral so it can coexist with microSD.

### Machine presets

- [ ] **Stock Apple-1 4 KB preset** — The existing *"Woz Apple 1 (1976)"* preset (`MainWindow_ImGui.cpp:192`) is 8 KB RAM with all cards disabled, matching a "stock + ACI card" configuration. Add a tighter **"Apple-1 bare 4 K"** preset for the absolute original (July 1976, pre-ACI): `ramKB = 4`, all flags off, Integer BASIC. This is the authenticity target for the newly-relocated text Sokoban (runs in `$0280-$0FFF` with STATE_GRID at `$0F00`). Bonus: once added, wire `Memory` to return `$FF` on reads above `ramSize*1024` and drop writes, with a flagged one-line warning in the debug panel — so out-of-bounds access is visible instead of silently working.

## Apple-1 ecosystem — software & loaders

- [ ] **TurboType 57 600-baud loader** (Uncle Bernie, via 8BitFlux *Keyboard Serial Terminal*) — Extend `TerminalCard` with a "raw inject" mode. Flow: host sends `.TUR` hybrid file → small Wozmon-speed bootstrap (the `.APL` prefix) installs an in-RAM dropper that disables the Wozmon echo → payload streams direct to RAM at 57 600 baud with running CRC. Loads a 4 KB program in < 30 s vs. the ~2 400-baud ceiling of echo-limited Wozmon. POM1-side: a single "Fast load" menu action that parses `.TUR`, switches the Terminal Card to raw mode for the burst, asserts CRC, and surrenders control back to Wozmon.

- [ ] **More GEN2 programs** — Image viewers, drawing tools, additional 280×192 HIRES demos.

- [ ] **CodeTank daughterboard ROM** — Support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.

- [ ] **Misc programs reference (Angela / P-Lab)** — Curated ports (Dobble, Oregon Trail, etc.) from [angela](https://p-l4b.github.io/angela/).

- [ ] **Wendell Sander's Star Trek (SPACWR)** — Extended 32 K Star Trek port by the Fairchild DRAM lead, demoed to Jobs autumn 1976. Hunt for the listing (Applefritter, Computer History Museum archives). If located, package as a `.txt` bootable on a new "Apple-1 + Sander 32 K" preset. Modification: VMA signal 2.2 kΩ || 100 pF (Sander's fix) — simulate by enabling the upper RAM bank cleanly (no authenticity quirks needed since we don't model bus analog behaviour).

- [ ] **Sokoban follow-ups**
    1. ~~Display the moves counter in HGR/TMS variants~~ — done. Both variants now render an `MV:NNN` HUD directly into the graphics surface (HGR framebuffer top-left, TMS name-table row 0), refreshed after every move/undo so delta redraws don't stomp it.
    2. Re-add Microban #43 and #44 to the text/TMS variants if we later shave ~60 B of code (currently dropped to fit the 3 200 B stock-4K budget; both are preserved in the HGR 72-level set).

## SID converter

- [ ] **Arkanoid (Galway) does not play** — ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split architecture needs more than the converter's static ISR detection.

- [ ] **Some IRQ-driven tunes still fail** — Players using computed or indirect ISR addresses (e.g. BMX Kidz) escape the LDA/LDX/LDY + STA/STX/STY pattern matcher.

## Visuals & UX

- [ ] **Native file dialog** — File loading/saving still uses the in-app browser instead of system file pickers.

- [ ] **Authentic CRT shift-register streaming** — Extension of the existing CRT scanline + phosphor overlay (`Screen_ImGui::drawCRTOverlay`): add an opt-in mode that simulates the 1976 Signetics 2519 timing — characters land at ~60 char/s instead of instantly, the hardware scroll visibly shifts the whole buffer one line at a time, and the display stays frozen during CPU bursts exactly like the original. Pair with the bare-4 K preset for a full-fidelity 1976 experience.

- [ ] **In-app Apple-1 Hardware Reference** — New *Help > Hardware Reference* window housing the architectural digest (memory map, historical + modern peripherals, card addresses, assembly toolchain cheatsheet). Good home for the ecosystem notes currently scattered between `CLAUDE.md` and commit messages. Pure documentation; no emulation change.

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
