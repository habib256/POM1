# TODO

Open work only. For shipped features, see `git log` or the version notes in `README.md`.

## Apple-1 ecosystem — hardware

### Storage & cassette
- [ ] **Uncle Bernie's Improved ACI** — emulate the ACI Extended firmware page at `$C500-$C5FF` (256 B, started with `C500R`): EOR checksum (`R`/`W`), extended format with 8-byte header at `$07F8-$07FF` (`RX`/`WX`), and autostart (`<from>` == `<to>`). **Firmware is unpublished** — Uncle Bernie stated on Applefritter it will never be released, PROMs were distributed only with his physical IC kits (sold out). **Action**: contact him via [Applefritter PM](https://www.applefritter.com/user/254186/track) (precedent: he shared docs with the HoneyCrisp emulator dev). Code-side once obtained: load at `$C500`, update `CassetteDevice` for header + checksum. Refs: [Applefritter thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).

- [ ] **Uncle Bernie's Woz Machine floppy** — 5.25" Disk II-style emulation: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing Apple-1 DRAM-refresh jitter so RWTS cycle-counting works without a Q3 2 MHz clock. Needs GCR track/sector emulation, `.dsk`/`.woz` loading, soft-switch dispatch in `$C0Ex`, async 74LS123 drive clock. Major effort, do last — only worth it once original Apple-1 disk software surfaces.

### Print & voice
- [ ] **SWTPC PR-40 40-col printer** — parallel-port matrix printer (5×7, 40 col/line, 75 lpm). Render output to a scrolling "paper roll" window + append to `.txt`. Tiny effort, high nostalgia. Hooks into an 8-bit parallel output port (closest existing socket: split off the Terminal Card TCP stream).

- [ ] **Briel Multi I/O Board — SpeakJet** — the 6522/6551 portions duplicate microSD/MODEM. Unique value: route the UART byte stream through a TTS bridge (eSpeak or macOS `say`) to give the Apple-1 a synthetic voice. Implement as a separate optional peripheral so it can coexist with microSD.

## Apple-1 software & loaders

- [ ] **TurboType 57 600-baud loader** (Uncle Bernie, via 8BitFlux *Keyboard Serial Terminal*) — extend `TerminalCard` with a "raw inject" mode. Flow: host sends `.TUR` hybrid → small Wozmon-speed bootstrap (`.APL` prefix) installs an in-RAM dropper that disables Wozmon echo → payload streams direct to RAM at 57 600 baud with running CRC. Loads 4 KB in < 30 s vs. the ~2 400-baud ceiling of echo-limited Wozmon. POM1-side: a single "Fast load" menu action that parses `.TUR`, switches the Terminal Card to raw mode for the burst, asserts CRC, surrenders back to Wozmon.

- [ ] **More GEN2 programs** — image viewers, drawing tools, additional 280×192 HIRES demos.

- [ ] **Finalise [`software/hgr/HGR8_BBFont.inc`](software/hgr/HGR8_BBFont.inc)** — unify BB rendering (`fontbb.s`, `$20`–`$7F`) vs PNG (controls + extended `$80`–`$FF`), document/regenerate from sources (`fonts/fontbb.s`, `fonts/font_codepage_437_8x8.png`), refine problematic glyphs (NTSC / stroke weight). Optionally add a generator script under `tools/`.

- [ ] **CodeTank daughterboard ROM** — support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.

- [ ] **Misc programs (Angela / P-Lab)** — curated ports (Dobble, Oregon Trail, etc.) from [angela](https://p-l4b.github.io/angela/).

- [ ] **Wendell Sander's Star Trek (SPACWR)** — extended 32 K Star Trek port by the Fairchild DRAM lead, demoed to Jobs autumn 1976. Hunt the listing (Applefritter, CHM archives). If located: package as bootable `.txt` on a new "Apple-1 + Sander 32 K" preset. Sander's VMA fix (2.2 kΩ ‖ 100 pF) — simulate by enabling the upper RAM bank cleanly (no analog modelling needed).

- [ ] **Sokoban follow-ups**
    1. Re-add Microban #43 and #44 to the text/TMS variants if we shave ~60 B (currently dropped to fit the 3 200 B stock-4K budget; both preserved in the HGR 72-level set).
    2. Swap the HGR HUD/title font for a "fat stroke" HGR font. Current `hud_font` (`software/hgr/HGR6_Sokoban.asm`) is a 5×7 thin font: horizontal bars render white, vertical 1-pixel strokes shimmer in NTSC artifact colour. [Pohoreski's apple2_hgr_font_tutorial](https://github.com/Michaelangel007/apple2_hgr_font_tutorial) documents a 7×8 fat-stroke font (1 byte/scanline, same storage budget) where every stroke is ≥ 2 pixels wide → reads as white. Pull in the digit + `M V : S O K B A N P R E L space L` subset (~24 glyphs × 8 B = 192 B), verify licensing, swap as a direct replacement (no logic change). Same idea could feed a 16×16 big-title splash font.

## SID converter (`tools/sid2apple1.py`)

The converter is now the only bottleneck — the SID chip itself is cycle-accurate libresidfp since v1.8.

- [ ] **Arkanoid (Galway) silent** — ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split player exceeds the converter's static ISR detection.
- [ ] **IRQ-driven tunes with computed ISR addresses** — e.g. BMX Kidz (Hubbard). Players that build the ISR vector dynamically escape the LDA/LDX/LDY + STA/STX/STY pattern matcher.
- [ ] **PSID `flags` field ignored** — `pal = (speed_bit == 0)` (line ~597) confuses the PSID *speed* bit (vsync vs CIA timer) with the *video standard* (PAL vs NTSC, encoded in the `flags` word at offset `0x76`, bits 2–3). The `delay_outer` table (`0x4E` PAL / `0x41` NTSC) keys off the wrong bit, so some tunes get the wrong tempo on the Apple-1 (NTSC-clocked) target. Read `flags` directly and key the delay off bit 3.

## Visuals & UX

- [ ] **Native file dialog** — load/save still uses the in-app browser instead of system pickers.

- [ ] **Authentic CRT shift-register streaming** — extend `Screen_ImGui::drawCRTOverlay` with an opt-in mode simulating 1976 Signetics 2519 timing: characters land at ~60 char/s instead of instantly, hardware scroll visibly shifts the buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset for full 1976 fidelity.

## Technical debt & code quality

### Performance
- [ ] **64 KB RAM `memcpy` @ 60 Hz** — `SnapshotPublisher::publish()` already skips the TMS9918 16 KB copy when unplugged but the full 64 KB RAM copy still runs every frame under `stateMutex`. Further reduction needs page-level dirty tracking in `Memory::memWrite`. Likely not worth the complexity.

### Architecture
- [ ] **Static `Screen_ImGui::displayCallback`** couples UI to emulation. → `DisplayDevice` interface injected into `Memory`; `Screen_ImGui` implements it.

### Tests
- [ ] **No unit tests** — refactors risk silently regressing 6502 emulation. → Klaus Dormann's 6502 functional tests as first smoke test; add GTest or Catch2.
- [ ] **Parsers not fuzzed** — `loadHexDump()` and `executeATCommand()` accept untested external input. → LibFuzzer targets via a CMake option (`ENABLE_FUZZING`).
