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

- [ ] **Full bare-4K enforcement (optional)** — The *"Apple-1 bare 4 K (July 1976)"* preset exists and triggers an OOR warning (`Memory::checkOutOfRangeAccess`, status-bar `OOR:N` indicator) when a program touches RAM past the preset's `ramKB*1024`. A stricter "hardware-accurate" mode would actually enforce bounds (reads return `$FF`, writes dropped) instead of just warning. Only worth doing if a specific authenticity-test program needs it — today the warning alone is enough to flag out-of-RAM accesses.

## Apple-1 ecosystem — software & loaders

- [ ] **TurboType 57 600-baud loader** (Uncle Bernie, via 8BitFlux *Keyboard Serial Terminal*) — Extend `TerminalCard` with a "raw inject" mode. Flow: host sends `.TUR` hybrid file → small Wozmon-speed bootstrap (the `.APL` prefix) installs an in-RAM dropper that disables the Wozmon echo → payload streams direct to RAM at 57 600 baud with running CRC. Loads a 4 KB program in < 30 s vs. the ~2 400-baud ceiling of echo-limited Wozmon. POM1-side: a single "Fast load" menu action that parses `.TUR`, switches the Terminal Card to raw mode for the burst, asserts CRC, and surrenders control back to Wozmon.

- [ ] **More GEN2 programs** — Image viewers, drawing tools, additional 280×192 HIRES demos.

- [ ] **CodeTank daughterboard ROM** — Support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.

- [ ] **Misc programs reference (Angela / P-Lab)** — Curated ports (Dobble, Oregon Trail, etc.) from [angela](https://p-l4b.github.io/angela/).

- [ ] **Wendell Sander's Star Trek (SPACWR)** — Extended 32 K Star Trek port by the Fairchild DRAM lead, demoed to Jobs autumn 1976. Hunt for the listing (Applefritter, Computer History Museum archives). If located, package as a `.txt` bootable on a new "Apple-1 + Sander 32 K" preset. Modification: VMA signal 2.2 kΩ || 100 pF (Sander's fix) — simulate by enabling the upper RAM bank cleanly (no authenticity quirks needed since we don't model bus analog behaviour).

- [ ] **Sokoban follow-ups**
    1. Re-add Microban #43 and #44 to the text/TMS variants if we later shave ~60 B of code (currently dropped to fit the 3 200 B stock-4K budget; both are preserved in the HGR 72-level set).
    2. **Swap the HGR HUD/title font for a proper "fat stroke" HGR font.** Current `hud_font` in `software/hgr/HGR6_Sokoban.asm` is a hand-rolled 5×7 thin font: horizontal bars render white, vertical 1-pixel strokes render in NTSC artifact colour (violet on even columns, green on odd), so digits and letters shimmer and shift hue depending on their byte column. Michael Pohoreski's [apple2_hgr_font_tutorial](https://github.com/Michaelangel007/apple2_hgr_font_tutorial) documents a 7×8 fat-stroke HGR font (1 byte per scanline, 8 bytes per glyph — same storage budget as today) whose strokes are ≥ 2 pixels wide so every stroke reads as white. Plan: pull in the digit + `M V : S O K B A N P R E L space L` subset (~24 glyphs × 8 B = 192 B, matches current layout), verify licensing, and drop it in as a direct replacement — no logic change, just a font-data swap. Same idea could carry the same subset into a 16×16 big-title font for a more impressive splash screen.

## SID converter

- [ ] **Arkanoid (Galway) does not play** — ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split architecture needs more than the converter's static ISR detection.

- [ ] **Some IRQ-driven tunes still fail** — Players using computed or indirect ISR addresses (e.g. BMX Kidz) escape the LDA/LDX/LDY + STA/STX/STY pattern matcher.

## Visuals & UX

- [ ] **Native file dialog** — File loading/saving still uses the in-app browser instead of system file pickers.

- [ ] **Authentic CRT shift-register streaming** — Extension of the existing CRT scanline + phosphor overlay (`Screen_ImGui::drawCRTOverlay`): add an opt-in mode that simulates the 1976 Signetics 2519 timing — characters land at ~60 char/s instead of instantly, the hardware scroll visibly shifts the whole buffer one line at a time, and the display stays frozen during CPU bursts exactly like the original. Pair with the bare-4 K preset for a full-fidelity 1976 experience.

## Technical debt & code quality (audit April 2026)

### Performance
- [ ] **Snapshot RAM copy 64 KB @ 60 Hz** — `publishSnapshotLocked()` writes directly into `latestSnapshot` (no stack copy, no per-frame alloc) and skips the TMS9918 16 KB memcpy when the card is unplugged; the full 64 KB RAM `memcpy` still runs every frame under `stateMutex`. Further reduction would need page-level dirty tracking in `Memory::memWrite`. Likely not worth the complexity.

### Architecture
- [ ] **`Memory` is a god object** — owns 9 peripheral `unique_ptr`s + matching enable flags + inline I/O dispatch in `memRead`/`memWrite`. → Extract a `PeripheralBus` with dynamic registry; `Memory` only manages the address space.
- [ ] **`EmulationController` SRP violation** — ~50 public methods (CPU, ROMs, snapshots, keyboard, tape, reset…). → Split into `SaveStateManager`, `KeyboardController`, `RomLoader`.
- [ ] **`MainWindow_ImGui.cpp` is monolithic** (~3200 lines) — UI + events + state + dialogs + rendering in one class. → Per-dialog classes; machine presets in external JSON/YAML.
- [ ] **Static `Screen_ImGui::displayCallback`** couples UI to emulation. → `DisplayDevice` interface injected into `Memory`; `Screen_ImGui` implements it.

### Code quality
- [ ] **Diagnostic `std::cout` scattered across peripherals** (WiFiModem, TerminalCard, MicroSD, CFFA1): no level filtering, can't redirect to the debug UI. → Minimal `Logger` interface, dependency-injected.

### Tests
- [ ] **No unit tests** — refactors risk regressing 6502 emulation silently. → Klaus Dormann's 6502 functional tests as the first smoke test; add GTest or Catch2.
- [ ] **Parsers not fuzzed** — `loadHexDump()` and `executeATCommand()` accept external input untested. → LibFuzzer targets via a CMake option (`ENABLE_FUZZING`).
