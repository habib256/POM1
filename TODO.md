# TODO

Open work only. For shipped features, see `git log` or the version notes in `README.md`.

**Tag legend:** `[effort · impact]`
- effort: **S** (< 1 day) · **M** (1–5 days) · **L** (> 5 days or architectural)
- impact: **nice** (cosmetic / optional) · **solid** (clearly worthwhile) · **critical** (correctness / security / release blocker)

---

## Blocked on external

- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the ACI Extended firmware page at `$C500-$C5FF` (256 B, started with `C500R`): EOR checksum (`R`/`W`), extended format with 8-byte header at `$07F8-$07FF` (`RX`/`WX`), and autostart (`<from>` == `<to>`). **Firmware unpublished** — Uncle Bernie stated on Applefritter it will never be released, PROMs were distributed only with his physical IC kits (sold out). Contact path: [Applefritter PM](https://www.applefritter.com/user/254186/track) (precedent: he shared docs with the HoneyCrisp emulator dev). Code-side once obtained: load at `$C500`, extend `CassetteDevice` for header + checksum. Refs: [Applefritter thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
- [ ] **P-LAB Juke-Box multi-page EEPROM** `[S · solid]` — current `JukeBox.cpp` models the single-page 28c256 (32 kB). Multi-page 29c020 / 29c040 with `P0..PF` / `S0..S1` bank-select is not modelled because the MMIO bank-select register address is not documented on P-LAB's public site yet. Ping Claudio Parmigiani for the address; code change is a handful of lines (add a bank register + index into the backing store).
- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II-style emulation: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing Apple-1 DRAM-refresh jitter so RWTS cycle-counting works without a Q3 2 MHz clock. Needs GCR track/sector emulation, `.dsk`/`.woz` loading, soft-switch dispatch in `$C0Ex`, async 74LS123 drive clock. Major effort, do last — only worth it once original Apple-1 disk software surfaces.

---

## Apple-1 ecosystem — hardware

### Cassette & storage

- [ ] **Tape metadata panel** `[S · nice]` — remaining sub-item from the original "véritable lecteur de cassettes" plan. The procedural deck (`CassetteDeck_ImGui`) is shipped; the piece still missing is surfacing **load / run addresses + ACI header / checksum** alongside the currently inserted cassette. Parse the `.aci` header or a sidecar `.json` once per mount, show the result next to the cassette label.

### Print & voice

- [ ] **SWTPC PR-40 40-col printer** `[S · nice]` — parallel-port matrix printer (5×7, 40 col/line, 75 lpm). Render output to a scrolling "paper roll" window + append to `.txt`. Hooks into an 8-bit parallel output port (closest existing socket: split off the Terminal Card TCP stream).
- [ ] **Briel Multi I/O Board — SpeakJet** `[M · nice]` — the 6522/6551 portions duplicate microSD/MODEM. Unique value: route the UART byte stream through a TTS bridge (eSpeak or macOS `say`) to give the Apple-1 a synthetic voice. Implement as a separate optional peripheral so it can coexist with microSD.

---

## Apple-1 software & loaders

- [ ] **TurboType 57 600-baud loader** `[M · solid]` (Uncle Bernie, via 8BitFlux *Keyboard Serial Terminal*) — extend `TerminalCard` with a "raw inject" mode. Flow: host sends `.TUR` hybrid → small Wozmon-speed bootstrap (`.APL` prefix) installs an in-RAM dropper that disables Wozmon echo → payload streams direct to RAM at 57 600 baud with running CRC. Loads 4 KB in < 30 s vs. the ~2 400-baud ceiling of echo-limited Wozmon. POM1-side: a single "Fast load" menu action that parses `.TUR`, switches the Terminal Card to raw mode for the burst, asserts CRC, surrenders back to Wozmon.
- [ ] **CodeTank daughterboard ROM** `[S · nice]` — distinct from the P-LAB Juke-Box (already shipped). CodeTank is a smaller $4000-$7FFF ROM-only daughterboard; reuse the `PeripheralBus` pattern, no Program Manager / Save Program firmware.

---

## Visuals & UX

- [ ] **Native file dialog** `[M · solid]` — load/save still uses the in-app browser instead of system pickers. Library pick: `nfd` (NativeFileDialog) or `tinyfiledialogs` — both header-light, cross-platform, MIT-licensed.
- [ ] **1976 CRT fidelity** `[M · nice]` — two sub-items, opt-in under existing CRT effects (default off):
  1. **Shift-register streaming**: extend `Screen_ImGui::drawCRTOverlay` to simulate 1976 Signetics 2519 timing — characters land at ~60 char/s instead of instantly, hardware scroll visibly shifts the buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register noise dot grid**: periodic static pattern from the 2504/2513 counter clock (Claudio photo = ground truth — **NOT random**, ~40 columns × ~3 rows per char cell, 1 sub-pixel horizontal phase drift row-to-row, last row shorter). New `drawShiftRegisterNoise(x0,y0,x1,y1)` called after the backdrop pass, deterministic nested loop (no PRNG), very low alpha (`~crtScanlineAlpha * 0.25f`) tinted with `phosphorTint`.

---

## Technical debt & code quality

- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL has been deprecated since 10.14 (currently silenced via `GL_SILENCE_DEPRECATION`). ImGui ships `imgui_impl_metal.mm` + `imgui_impl_osx.mm` as drop-in replacements. Not urgent — OpenGL still works fine on Apple Silicon — but the long-term path. Scope includes porting the handful of raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 texture, HGR texture) to MTLTexture.
- [ ] **External `presets.json`** `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` out to a JSON file under `doc/` (or next to the executable) so a user can add a custom preset without recompiling. Loader goes in `MainWindow_Presets.cpp`; keep the current C++ table as a hard-coded fallback.
