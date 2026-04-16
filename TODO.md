# TODO

Open work only. For shipped features, see `git log` or the version notes in `README.md`.

**Tag legend:** `[effort · impact]`
- effort: **S** (< 1 day) · **M** (1–5 days) · **L** (> 5 days or architectural)
- impact: **nice** (cosmetic / optional) · **solid** (clearly worthwhile) · **critical** (correctness / security / release blocker)

---

## Blocked on external

- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the ACI Extended firmware page at `$C500-$C5FF` (256 B, started with `C500R`): EOR checksum (`R`/`W`), extended format with 8-byte header at `$07F8-$07FF` (`RX`/`WX`), and autostart (`<from>` == `<to>`). **Firmware is unpublished** — Uncle Bernie stated on Applefritter it will never be released, PROMs were distributed only with his physical IC kits (sold out). **Action**: contact him via [Applefritter PM](https://www.applefritter.com/user/254186/track) (precedent: he shared docs with the HoneyCrisp emulator dev). Code-side once obtained: load at `$C500`, update `CassetteDevice` for header + checksum. Refs: [Applefritter thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).

- [ ] **P-LAB software library (Claudio Parmigiani)** `[M · solid]` — curated integration of the ZIP Claudio shared: ASOFT (Applesoft 1.3 — verify vs current `roms/applesoftlite.rom` — plus older versions), PLAB (Nino's originals), UTILS (restoration tools), FORTH (ex-CFFA1), BASIC programs + testsid files, MCODE demos (incl. Corey Cohen's 50th-anniversary Apple ASCII-art), HELP/IEC commands for the microSD shell, and SID PIANO 2019 (loads at `$C400`). Plus Angela ports (Dobble, Oregon Trail, …) from [angela](https://p-l4b.github.io/angela/). Drop into `software/{applesoft,basic,demos,sid/piano2019,forth,plab}/` and mirror into `sdcard/` with the `NAME#TTAAAA` tagged convention. Zero code change — `MainWindow_FileDialogs.cpp:36-70` auto-scans, auto-enable heuristics already cover `/sid/`, `/hgr/`, `/tms9918/`, `/net/`. Rebuild WASM to embed the new assets in `pom1_imgui.data`. **License check required**: per Claudio, Juke-Box, SDcard Storage and WiFi Modem firmware are NOT Creative Commons; confirm with him which pieces of the library are redistributable with POM1 before committing.

- [ ] **Wendell Sander's Star Trek (SPACWR)** `[M · nice]` — extended 32 K Star Trek port by the Fairchild DRAM lead, demoed to Jobs autumn 1976. Hunt the listing (Applefritter, CHM archives). If located: package as bootable `.txt` on a new "Apple-1 + Sander 32 K" preset. Sander's VMA fix (2.2 kΩ ‖ 100 pF) — simulate by enabling the upper RAM bank cleanly (no analog modelling needed).

---

## Apple-1 ecosystem — hardware

### Storage & cassette

- [ ] **Véritable lecteur de cassettes (UI + contrôle + I/O)** `[L · nice]` — ajouter un lecteur de cassettes **contrôlable comme un vrai** (load/record/stop/rew/ff/eject, état mécanique, compteur), avec une **image réaliste** de lecteur. Flux:
    1. **Asset/UI**: récupérer une image (ou set) de lecteur de cassettes (licence OK), afficher le lecteur, zones interactives (boutons + fente + cassette).
    2. **Gestion des cassettes**: sélectionner une cassette (bibliothèque locale/projet), **l'insérer** dans le lecteur, éjection; persister l'état (cassette courante, position bande).
    3. **Infos de chargement**: afficher clairement **les adresses de chargement/exécution** associées au contenu (ex: `from/to/exec`, metadata type Woz/ACI, checksum, header).
    4. **Lecture/Enregistrement "réels"**: vraie gestion lecture & enregistrement (vitesse, latence, niveaux/thresholds, erreurs optionnelles), et mapping vers l'interface cassette/ACI côté Apple-1.
    5. **CI + interchange Apple-1**: lire des fichiers audio originaux (WAV/AIFF) via la CI (décodage vers flux cassette), et en créer à partir de nouveaux programmes (encodage audio) afin de générer des "cassettes" partageables chargeables sur un Apple-1 original.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II-style emulation: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing Apple-1 DRAM-refresh jitter so RWTS cycle-counting works without a Q3 2 MHz clock. Needs GCR track/sector emulation, `.dsk`/`.woz` loading, soft-switch dispatch in `$C0Ex`, async 74LS123 drive clock. Major effort, do last — only worth it once original Apple-1 disk software surfaces.

### Print & voice

- [ ] **SWTPC PR-40 40-col printer** `[S · nice]` — parallel-port matrix printer (5×7, 40 col/line, 75 lpm). Render output to a scrolling "paper roll" window + append to `.txt`. Tiny effort, high nostalgia. Hooks into an 8-bit parallel output port (closest existing socket: split off the Terminal Card TCP stream).

- [ ] **Briel Multi I/O Board — SpeakJet** `[M · nice]` — the 6522/6551 portions duplicate microSD/MODEM. Unique value: route the UART byte stream through a TTS bridge (eSpeak or macOS `say`) to give the Apple-1 a synthetic voice. Implement as a separate optional peripheral so it can coexist with microSD.

### Sound

- [ ] **A1-AUDIO Special Edition — SID base $CC00** `[S · solid]` — Claudio Parmigiani's 10-unit Special Edition card ([A1-AUDIO](https://p-l4b.github.io/A1-AUDIO/)) moves the SID base from the prototype's `$C800` to `$CC00` — entirely collides with TMS9918 (`$CC00/$CC01`), so the two become mutually exclusive like GEN2 ↔ A1-IO RTC. Add a `baseAddress` parameter to `SID::SID()` (internal `addr & 0x1F` decode stays), register the `PeripheralBus` range dynamically in `Memory.cpp:139-146` at the chosen base, introduce a new "Apple-1 + A1-AUDIO SE" preset in `MainWindow_Presets.cpp` that auto-disables TMS9918, surface the base read-only in the SID hardware window (`MainWindow_HardwareWindows.cpp`), and update the memory map in `CLAUDE.md`. Regression: the 66 cycle-accurate SID tunes must still work at default `$C800`.

---

## Apple-1 software & loaders

- [ ] **TurboType 57 600-baud loader** `[M · solid]` (Uncle Bernie, via 8BitFlux *Keyboard Serial Terminal*) — extend `TerminalCard` with a "raw inject" mode. Flow: host sends `.TUR` hybrid → small Wozmon-speed bootstrap (`.APL` prefix) installs an in-RAM dropper that disables Wozmon echo → payload streams direct to RAM at 57 600 baud with running CRC. Loads 4 KB in < 30 s vs. the ~2 400-baud ceiling of echo-limited Wozmon. POM1-side: a single "Fast load" menu action that parses `.TUR`, switches the Terminal Card to raw mode for the burst, asserts CRC, surrenders back to Wozmon.

- [ ] **Generator script for [`software/hgr/HGR8_BBFont.inc`](software/hgr/HGR8_BBFont.inc)** `[S · nice]` — the unified table (`$20`–`$7F` from `fontbb.s`, controls + `$80`–`$FF` from `font_codepage_437_8x8.png`) now ships, but the build is still by-hand. Add a script under `tools/` that regenerates the `.inc` from both sources, so future glyph refinements (NTSC artifact-sensitive columns, stroke weight) are reproducible.

- [ ] **CodeTank daughterboard ROM** `[S · nice]` — support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.

- [ ] **More GEN2 programs** `[M · nice]` — concrete wish-list:
    - **PNG → HGR image loader**: CLI tool (`tools/png2hgr.py`) converting a 280×192 PNG into a `.bin` with the non-linear scanline layout of `GraphicsCard::scanlineAddress()`. Unlocks photo slideshows.
    - **HGR Tetris** (companion to `software/tms9918/tetris.bin` which uses TMS9918 VRAM instead).
    - **Paint app**: cursor + plot/line/rect primitives, 7-colour palette picker from the NTSC artifact set.
    - **HGR4 Mandelbrot** already has 6 preset views; add zoom animation / julia set variant.
    - **Bouncing balls / particle demo** to stress the per-line dirty diff (perf regression guard).

- [ ] **Sokoban follow-ups** `[S · nice]`
    1. Re-add Microban #43 and #44 to the text/TMS variants if we shave ~60 B (currently dropped to fit the 3 200 B stock-4K budget; both preserved in the HGR 72-level set).
    2. Swap the HGR HUD/title font for a "fat stroke" HGR font. Current `hud_font` (`software/hgr/HGR6_Sokoban.asm`) is a 5×7 thin font: horizontal bars render white, vertical 1-pixel strokes shimmer in NTSC artifact colour. [Pohoreski's apple2_hgr_font_tutorial](https://github.com/Michaelangel007/apple2_hgr_font_tutorial) documents a 7×8 fat-stroke font (1 byte/scanline, same storage budget) where every stroke is ≥ 2 pixels wide → reads as white. Pull in the digit + `M V : S O K B A N P R E L space` subset (~24 glyphs × 8 B = 192 B), verify licensing, swap as a direct replacement (no logic change).

---

## SID converter (`tools/sid2apple1.py`)

The converter is now the only bottleneck — the SID chip itself is cycle-accurate libresidfp since v1.8.

- [ ] **Arkanoid (Galway) silent** `[M · nice]` — ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split player exceeds the converter's static ISR detection.
- [ ] **IRQ-driven tunes with computed ISR addresses** `[M · nice]` — e.g. BMX Kidz (Hubbard). Players that build the ISR vector dynamically escape the LDA/LDX/LDY + STA/STX/STY pattern matcher.
- [ ] **PSID `flags` field ignored** `[S · solid]` — `pal = (speed_bit == 0)` (line ~597) confuses the PSID *speed* bit (vsync vs CIA timer) with the *video standard* (PAL vs NTSC, encoded in the `flags` word at offset `0x76`, bits 2–3). The `delay_outer` table (`0x4E` PAL / `0x41` NTSC) keys off the wrong bit, so some tunes get the wrong tempo on the Apple-1 (NTSC-clocked) target. Read `flags` directly and key the delay off bit 3.

---

## Visuals & UX

- [ ] **Native file dialog** `[M · solid]` — load/save still uses the in-app browser instead of system pickers. Library pick: `nfd` (NativeFileDialog) or `tinyfiledialogs` — both header-light, cross-platform, MIT-licensed.

- [ ] **1976 CRT fidelity** `[M · nice]` — two sub-items, opt-in under existing CRT effects (default off):
    1. **Shift-register streaming**: extend `Screen_ImGui::drawCRTOverlay` to simulate 1976 Signetics 2519 timing — characters land at ~60 char/s instead of instantly, hardware scroll visibly shifts the buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
    2. **Shift-register noise dot grid**: periodic static pattern from the 2504/2513 counter clock (Claudio photo = ground truth — **NOT random**, ~40 columns × ~3 rows per char cell, 1 sub-pixel horizontal phase drift row-to-row, last row shorter). New `drawShiftRegisterNoise(x0,y0,x1,y1)` called after the backdrop pass, deterministic nested loop (no PRNG), very low alpha (`~crtScanlineAlpha * 0.25f`) tinted with `phosphorTint`.

---

## Performance

- [ ] **M6502 : dispatch `switch` monolithique** `[M · solid]` — biggest remaining CPU lever (~15-25 % in MAX speed). Current `(this->*entry.addrMode)()` + `(this->*entry.operation)()` = two indirect member-pointer calls per instruction that LTO can't devirtualise through `const OpcodeEntry&`. Replace with a large `switch (opcode)` in `run()` with bodies inlined, freeing LTO to fuse each case with its `memRead`. Validate against `ctest` (Klaus 6502 functional test now gate this refactor — any flag/cycle regression will fail in < 2 s).

- [ ] **HGR `rasterizeLine` : LUT byte → 7 pixels** `[S · solid]` — `GraphicsCard.cpp:63-84` bit-tests 280 times per dirty scanline. A `uint32_t kHgrPixels[512][7]` indexed by `(byte | group2<<8)` returns the 7 artifact colours in one load. Boundary correction (white bleeding) stays as explicit logic at the 39 inter-byte frontiers. ~2-3× speedup on dirty lines, 14 KB table, no behavioural change.

- [ ] **PGO build preset** `[S · solid]` — `cmake --preset pgo`: compile with `-fprofile-generate`, run a bench harness (Microchess 30 s + SID tune 30 s), recompile with `-fprofile-use`. LTO + PGO gives the compiler the hot-branch stats of the M6502 dispatch + HGR raster. Typically +10-15 % on dispatch-heavy code, zero source changes. **Depends on the bench harness below.**

---

## Technical debt & code quality

### Architecture

- [ ] **Static `Screen_ImGui::displayCallback` → `DisplayDevice` interface** `[M · nice]` — the static callback currently pinned to a single `Screen_ImGui` instance prevents (a) unit-testing `Memory::memWrite` in isolation (no way to inject a fake display sink), and (b) running a second output (e.g. Terminal Card display mirror without re-opening a TCP socket). Refactor: a `DisplayDevice` interface with `void onChar(char)`, injected into `Memory` at construction; `Screen_ImGui` implements it.

### Tests

<!-- ✓ Klaus Dormann 6502 functional test + PeripheralBus dispatch smoke
     test landed via `ctest` (see `tests/`). M6502 core + bus dispatch are
     now pinned. Two real bugs surfaced and were fixed on first run:
     `PHP` wasn't setting bits 4+5 in the pushed status byte; `BRK` pushed
     PC+1 instead of PC+2 (missing the signature-byte skip). -->

- [ ] **Extend unit-test coverage beyond CPU + bus** `[M · solid]` — Klaus pins M6502 opcode/flag correctness end-to-end; the PeripheralBus smoke test pins priority ordering, enable/disable, and sniffer pass-through. The paths most likely to regress next are:
    - **SID ring buffer (SPSC)** — concurrency between emulation and audio threads; needs 2-thread test, `std::thread` + `std::atomic` validation. Bugs here manifest as intermittent audio crackles — impossible to reproduce without a dedicated test.
    - **`Memory::dirtyPages` coverage** — verify every `memWrite` path sets the correct page bit (bulk loaders, cassette sniffer fall-through, testMode fast-path).
    - **`loadHexDump` parser edge cases** — inline comments, merged `data:addr` forms, `T`/`X`/`R` prefixes.
    - **`executeATCommand` state machine** — Hayes AT sequence (`ATDT host:port`, `+++`, `ATH`) including TELNET IAC filtering.
    - **`KeyboardController` drain semantics** — `queueKey()` from UI thread + `drainTo()` under stateMutex, lock-order invariant.

    GTest or Catch2 would give the infra (parametrized cases, concurrency helpers, better failure messages), but plain `add_executable` + `assert()` works for single-threaded cases — see `tests/peripheral_bus_smoke_test.cpp` for the pattern. Add the framework only when the SID ring buffer test lands (that one genuinely needs thread orchestration).

- [ ] **Parsers not fuzzed** `[M · solid]` — `loadHexDump()` and `WiFiModem::executeATCommand()` accept untested external input. LibFuzzer targets via a CMake option (`ENABLE_FUZZING`); both are self-contained parsers, easy to wrap.

### Build & CI

- [ ] **Benchmark harness** `[S · solid]` — prerequisite for PGO and honest perf claims. `tools/bench.cpp` (or `bench` CMake target) loads a known program (Microchess, a SID tune), runs N emulated cycles, outputs cycles/s + frame time percentiles. Also guards against CPU-dispatch regressions when the 6502 switch refactor lands.

- [ ] **CI build matrix** `[S · solid]` — no `.github/workflows/` exists. A minimal matrix (Ubuntu + macOS + WASM) on push would catch platform regressions early. Include cc65 assembler step to guard `software/` builds too.

- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL has been deprecated since 10.14 (currently silenced via `GL_SILENCE_DEPRECATION`). ImGui ships `imgui_impl_metal.mm` + `imgui_impl_osx.mm` as drop-in replacements. Not urgent — OpenGL still works fine on Apple Silicon — but the long-term path. Scope includes porting the handful of raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 texture, HGR texture) to MTLTexture.
