# TODO

Open work only. Shipped features live in `git log` / `README.md`.

**Tag legend:** `[effort · impact]`
- effort: **S** (< 1 day) · **M** (1–5 days) · **L** (> 5 days or architectural)
- impact: **nice** (cosmetic / optional) · **solid** (clearly worthwhile) · **critical** (correctness / security / release blocker)

Sections are ordered by actionability: implementable work first, externally-blocked items last.

---

## 🔌 Peripherals

- [ ] **P-LAB IEC Card** `[M · solid]` — Claudio Parmigiani's Commodore IEC serial bus card: lets the Apple-1 talk to C64-era peripherals (1541 floppy, printer, …). Spec: https://p-l4b.github.io/iec/ . Investigate the register window, handshake, and what firmware ships on the card; honour the "one board at a time" rule for mutual exclusions. Backing store likely via a host `.d64` image + a small IEC state machine (ATN/CLK/DATA). New preset + Hardware Reference entry.
- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — https://github.com/flowenol/apple1-serial — serial-port bootloader / terminal for the Apple-1 (complements TurboType / 8BitFlux Terminal). Evaluate whether it pipes through the existing Terminal Card or needs its own ACIA variant; likely a text-format loader entry on top of the current `Memory::loadHexDump` / paste pipeline.
- [ ] **ACI header + checksum on the jaquette** `[S · nice]` — `tapeinfo.txt` already drives the *"Type 0280.0FFFR"* label. Parse the raw `.aci` pulse-capture header (from / to / checksum) in `CassetteDevice::loadAciTape()` and surface it for tapes without a sidecar entry.
- [ ] **Briel Multi I/O Board — SpeakJet** `[M · nice]` — the 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as a separate optional peripheral so it coexists with microSD.

---

## 📥 Loaders & interoperability

- [ ] **TurboType 57 600-baud loader** `[M · solid]` — Uncle Bernie's format, shipped by the 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244 passthrough; predecessor: Mike Willegal 2008). Protocol: Wozmon-speed bootstrap (200 ms/newline, 20 ms/char) installs an in-RAM dropper that **skips `$D012` echoes** and streams bytes at 57.6 kbps with a running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in < 30 s vs. ~2 400 baud echo-limited Wozmon. POM1 side: parse `.TUR` / `.APL`, switch Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon.
- [ ] **CodeTank daughterboard ROM — Juke-Box compatible** `[S · nice]` — BOM v0.4 (28c256 + 74LS156 + 3-pin jumper + 2×22 header) is electrically identical to the P-LAB Juke-Box. Runs out of the box at `roms/jukebox.rom` with preset #11. Ship a `hwKeyValue` entry in the Hardware Reference spelling out the compatibility, optionally a derived preset named *"Apple-1 with CodeTank ROM"*.

---

## 🎨 Visuals & UX

- [ ] **Native file dialog** `[M · solid]` — the in-app browser stays in the way. Drop in `nfd` (NativeFileDialog) or `tinyfiledialogs` — header-light, MIT, cross-platform.
- [ ] **Hardware menu mutual-exclusion tooltips** `[S · nice]` — toggling cards silently evicts others per the "one board at a time" rule (SID ↔ A1-AUDIO SE, TMS9918 ↔ A1-AUDIO SE, GEN2 HGR ↔ A1-IO RTC, Juke-Box ↔ {CFFA1, microSD, Krusader, Wi-Fi Modem, A1-SID}). The UI performs the evict but doesn't forewarn — add an ImGui tooltip on each checkbox listing the cards it will unplug and the overlapping bus window.
- [ ] **HiDPI font scaling on Linux** `[S · nice]` — ImGui font scaling on 4K X11/Wayland displays currently requires the user to tweak `ImGui::GetIO().FontGlobalScale` manually. Auto-detect monitor DPI on first window creation (`glfwGetMonitorContentScale`, GLFW 3.3+) and scale the default font; keep a Hardware → Display setting to override.
- [ ] **1976 CRT fidelity (opt-in)** `[M · nice]` — two sub-effects under the existing CRT toggle, default off. The streaming sub-effect is period-accurate; the noise sub-effect is pure cosmetic polish.
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing): characters land ~60 / s, hardware scroll shifts the buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock): periodic static pattern, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` after the backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.

---

## 🔧 Technical debt & code quality

- [ ] **CLI breakpoints (`--break <addr>`)** `[S · solid]` — the CLI dispatcher landed without `--break` because `M6502` has no breakpoint infra. Add a PC-matched halt in `M6502::run()` (early-return when `programCounter == breakpoint`), `setBreakpoint(quint16)` / `clearBreakpoint()` pair, plumb through `EmulationController`, then flip the stub in `CliDispatcher.cpp` (currently errors out) into a real `CliAction::Kind::Break`. Keep the overhead off the hot path — a single compare-and-branch in the dispatch loop behind a `breakpointActive` flag.
- [ ] **Snapshot save / load (save-state)** `[M · solid]` — `--snapshot-save <path>` / `--snapshot-load <path>` serialising `EmulationSnapshot` + every peripheral's internal state (SID register file + filter, TMS9918 VRAM + regs, microSD fs cursor, modem connection, cassette transport + tape offset, CFFA1 disk offset, Juke-Box bank). Enables deterministic replay, reloadable test fixtures, one-click bug reports — and is the storage format the scriptable IPC below will share. Versioned on-disk format so future peripheral additions don't invalidate old snapshots.
- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) reading line-delimited commands *while the emulator runs* — same verbs as CLI flags, but for stateful sequences (e.g. *"load BASIC, type a listing, save to SD, reset, verify via telnet"*). Telnet on `:6502` already carries keystrokes + display text; this channel carries control verbs without polluting the keyboard stream. Depends on the CLI-verb and snapshot work above.
- [ ] **Terminal Card — `Ctrl-K` hand-over to host keyboard** `[S · nice]` — match the 8BitFlux toggle: a `Ctrl-K` byte suspends `$D010` / `$D011` injection until `Ctrl-T` re-attaches. Useful once a script has bootstrapped a program and the user wants to play without dropping the session. Hook: add `injectionSuspended` next to `escapePending` / `eightBitMode` in `TerminalCard.cpp`, skip the keyboard queue while set.
- [ ] **External `presets.json`** `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` to JSON under `doc/` (or next to the executable) so users add presets without recompiling. Loader in `MainWindow_Presets.cpp`, keep the C++ table as fallback.
- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL is deprecated since macOS 10.14 (currently silenced via `GL_SILENCE_DEPRECATION`). `imgui_impl_metal.mm` + `imgui_impl_osx.mm` are drop-in; scope is porting the handful of raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 texture, HGR texture) to MTLTexture.

---

## 🧪 Testing & CI

- [ ] **GitHub Actions CI (`ctest` matrix)** `[S · solid]` — `ctest` runs in ~5 s and all seven native tests are stable. A nightly + on-PR workflow building Linux / macOS / Windows (build + `ctest`) plus a WASM job (`emcmake`, no tests) would catch portability breakage before merge. Main effort: pinning toolchain versions and mirroring `setup_imgui.{sh,bat}`.

---

## ⏸️ Deferred / conditional

Not blocked externally — spec is public and the code change is tractable — but gated on a real-world trigger (software that would exercise the feature, user demand, hardware availability). Promote into another section when the trigger appears.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk` / `.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.

---

## 🚫 Blocked on external

- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with an 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **PROM unpublished** (distributed only with Bernie's physical kits, sold out; Applefritter statement says it will never be released). Contact [Applefritter PM](https://www.applefritter.com/user/254186/track) — he shared docs with the HoneyCrisp emulator dev. Once obtained: load at `$C500`, extend `CassetteDevice` for the header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
