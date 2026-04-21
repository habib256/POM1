# TODO

Open work only. Shipped features live in `git log` / `README.md`.

**Tag legend:** `[effort · impact]`
- effort: **S** (< 1 day) · **M** (1–5 days) · **L** (> 5 days or architectural)
- impact: **nice** (cosmetic / optional) · **solid** (clearly worthwhile) · **critical** (correctness / security / release blocker)

---

## Blocked on external

- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with an 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **PROM unpublished** (distributed only with Bernie's physical kits, sold out; Applefritter statement says it will never be released). Contact [Applefritter PM](https://www.applefritter.com/user/254186/track) — he shared docs with the HoneyCrisp emulator dev. Once obtained: load at `$C500`, extend `CassetteDevice` for the header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
- [ ] **P-LAB Juke-Box multi-page EEPROM** `[S · solid]` — `JukeBox.cpp` models the 28c256 single-page case. 29c020 / 29c040 bank switching (`P0..PF` / `S0..S1`) needs the MMIO bank-select register address, not documented publicly. Ping Claudio Parmigiani; the code change is a bank register + index into the backing store.

---

## Deferred / conditional

Not blocked externally — spec is public and the code change is tractable — but gated on a real-world trigger (software that would exercise the feature, user demand, hardware availability). Promote into another section when the trigger appears.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk`/`.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.

---

## Peripherals

- [ ] **ACI header + checksum on the jaquette** `[S · nice]` — `tapeinfo.txt` already drives the *"Type 0280.0FFFR"* label. Still missing: parse the raw `.aci` pulse-capture header (from / to / checksum) in `CassetteDevice::loadAciTape()` and surface it for tapes without a sidecar entry.
- [ ] **SWTPC PR-40 40-col printer** `[S · nice]` — parallel matrix printer (5×7, 40 col / line, 75 lpm). Scroll the output into a "paper roll" window and append to a `.txt`. Attach on an 8-bit parallel output port (cheapest socket: tee off the Terminal Card TCP stream).
- [ ] **Briel Multi I/O Board — SpeakJet** `[M · nice]` — the 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as a separate optional peripheral so it coexists with microSD.
- [ ] **P-LAB IEC Card** `[M · solid]` — Commodore IEC serial bus card for the Apple-1 by Claudio Parmigiani: lets the Apple-1 talk to C64-era peripherals (1541 floppy, printer, …). Spec: https://p-l4b.github.io/iec/ . Investigate the register window, handshake, and what firmware ships on the card; honour Parmigiani's "one board at a time" rule for mutual exclusions. Backing store likely via a host `.d64` image + a small IEC state machine (ATN/CLK/DATA). New preset + Hardware Reference entry.
- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — https://github.com/flowenol/apple1-serial — serial-port bootloader / terminal for the Apple-1 (complements TurboType / 8BitFlux Terminal). Evaluate whether it pipes through the existing Terminal Card or needs its own ACIA variant; likely a text-format loader entry on top of the current `Memory::loadHexDump` / paste pipeline.
- [ ] **A1-IO & RTC auto-plug on `software/a1io_rtc/` load** `[S · solid]` — the directory-match heuristic in `MainWindow_FileDialogs.cpp` already auto-enables the matching card when the user picks a file under `software/sid/`, `software/hgr/`, `software/tms9918/`, `software/net/` or `sdcard/`. `software/a1io_rtc/` is missing from that list — add the branch that flips `a1ioRtcEnabled = true` + queues the deferred plug (same 15-frame pattern as the other cards) so the clock demo Just Works after a File > Load Memory. **⚠ mutually-exclusive with GEN2 HGR** at `$2000-$3FFF` — if GEN2 is currently on, evict it before plugging A1-IO.

---

## Loaders & interoperability

- [ ] **TurboType 57 600-baud loader** `[M · solid]` — Uncle Bernie's format, shipped by the 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244 passthrough; predecessor: Mike Willegal 2008). Protocol: Wozmon-speed bootstrap (200 ms per newline, 20 ms per char) installs an in-RAM dropper; the dropper **skips `$D012` echoes** and streams bytes into RAM at 57.6 kbps with a running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in < 30 s vs. ~2 400 baud echo-limited Wozmon. POM1 side: parse `.TUR` / `.APL`, switch the Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon.
- [ ] **CodeTank daughterboard ROM — Juke-Box compatible** `[S · nice]` — BOM v0.4 (28c256 + 74LS156 + 3-pin jumper + 2×22 header) is electrically identical to the P-LAB Juke-Box. Runs out of the box at `roms/jukebox.rom` with preset #10. Ship a `hwKeyValue` entry in the Hardware Reference spelling out the compatibility, optionally a derived preset named *"Apple-1 with CodeTank ROM"*.

---

## Visuals & UX

- [ ] **Native file dialog** `[M · solid]` — the in-app browser stays in the way. Drop in `nfd` (NativeFileDialog) or `tinyfiledialogs` — header-light, MIT, cross-platform.
- [ ] **1976 CRT fidelity (opt-in)** `[M · nice]` — two sub-effects under the existing CRT toggle, default off. The streaming sub-effect is period-accurate; the noise sub-effect is pure cosmetic polish.
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing): characters land ~60 / s, hardware scroll shifts the buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock): periodic static pattern, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` called after the backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.
- [ ] **Hardware menu mutual-exclusion tooltips** `[S · nice]` — Parmigiani's "one board at a time" rule (see `CLAUDE.md`) means toggling some cards silently evicts others (SID ↔ A1-AUDIO SE, TMS9918 ↔ A1-AUDIO SE, GEN2 HGR ↔ A1-IO RTC, Juke-Box ↔ {CFFA1, microSD, Krusader, Wi-Fi Modem}). The UI performs the evict but doesn't forewarn the user. Add an ImGui tooltip on each checkbox in the Hardware menu listing the cards it will unplug and the bus window they overlap.
- [ ] **HiDPI font scaling on Linux** `[S · nice]` — ImGui font scaling on 4K X11/Wayland displays currently requires the user to tweak `ImGui::GetIO().FontGlobalScale` manually. Auto-detect the monitor DPI on first window creation (GLFW 3.3+ `glfwGetMonitorContentScale`) and scale the default font accordingly; keep a Hardware → Display setting to override.

---

## Packaging & distribution

- [ ] **Native OS icon bundling** `[S · nice]` — `pic/icon.png` drives every in-app surface (About 128 px, Welcome 64 px, GLFW window, WASM favicon) but the packaged OS containers still use the default icon. Three independent platforms:
  1. **macOS `.app` bundle** `[S · nice]` — `Info.plist` + `icon.icns` (`sips` + `iconutil`). Also fixes the Dock/Finder icon, since `glfwSetWindowIcon` is a no-op on macOS.
  2. **Windows `.rc` embed** `[S · nice]` — generate `packaging/windows/POM1.ico`, reference it from a `.rc` file, embed via `add_executable(${PROJECT_NAME} … POM1.rc)`.
  3. **Linux `.desktop` + hicolor** `[S · nice]` — drop a `.desktop` file + `hicolor/128x128/apps/POM1.png` in the release tarball.

---

## Technical debt & code quality

- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL is deprecated since macOS 10.14 (currently silenced via `GL_SILENCE_DEPRECATION`). `imgui_impl_metal.mm` + `imgui_impl_osx.mm` are drop-in; scope is porting the handful of raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 texture, HGR texture) to MTLTexture.
- [ ] **External `presets.json`** `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` to JSON under `doc/` (or next to the executable) so users add presets without recompiling. Loader in `MainWindow_Presets.cpp`, keep the C++ table as fallback.
- [ ] **Terminal Card — `Ctrl-K` hand-over to host keyboard** `[S · nice]` — match the 8BitFlux toggle: a `Ctrl-K` byte suspends `$D010` / `$D011` injection until `Ctrl-T` re-attaches. Useful once a script has bootstrapped a program and the user wants to play without dropping the session. Hook: add `injectionSuspended` next to `escapePending` / `eightBitMode` in `TerminalCard.cpp`, skip the keyboard queue while set.
- [ ] **Agent-facing CLI verbs** `[M · solid]` — today `main_imgui.cpp` exposes only six flags (`--preset`, `--terminal`, `--tape`, `--save-tape`, `--cpu-max`, `--list-presets`). An AI agent wanting to drive the emulator end-to-end still has to click through menus or script over telnet + keystrokes. Extend the CLI so **every runtime action** exposed in the Hardware / File / CPU menus is reachable from flags applied at boot:
  - Card toggles: `--enable sid,microsd,tms9918,...` / `--disable ...`
  - ROM / program load: `--load <addr>:<path>`, `--run <addr>`, `--paste <file>` (same path as Ctrl-V paste)
  - CPU control: `--speed <cycles-per-frame>`, `--step <N>`, `--break <addr>`, `--trace-brk`
  - Cassette deck: `--play`, `--rec`, `--rewind`, `--save-tape-format <aci|wav>`
  - microSD shell bypass: `--sd-mkdir`, `--sd-put host:guest`, `--sd-get guest:host` (avoid the Wozmon round-trip when seeding test fixtures)
  - SID: `--sid-chip 6581|8580`
  - Juke-Box: `--jukebox-jumper ram16|ram32`
  - A1-IO RTC: `--rtc-freeze YYYY-MM-DD HH:MM:SS` (deterministic test clock)

  Scope: `main_imgui.cpp` (parser) + a new `CliDispatcher` TU that maps verbs → existing `EmulationController` methods, documented grammar in `APPLE1DEV.md` / `CLAUDE.md`. Keep backwards-compatible with the six existing flags.

- [ ] **Snapshot save / load (save-state)** `[M · solid]` — `--snapshot-save <path>` / `--snapshot-load <path>` serialising `EmulationSnapshot` + every peripheral's internal state (SID register file + filter, TMS9918 VRAM + regs, microSD fs cursor, modem connection, cassette deck transport + tape offset, CFFA1 disk offset, Juke-Box bank). Enables deterministic replay, reloadable test fixtures, one-click bug reports, and is the storage format the scriptable IPC below will share. Design the on-disk format versioned so future peripheral additions don't invalidate old snapshots.

- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) that reads line-delimited commands *while the emulator is running* — same verbs as the CLI flags above, but for stateful sequences (e.g. *"load BASIC, type a listing, save to SD, reset, hard-reset, verify via telnet"*). Telnet on `:6502` already carries keystrokes + display text; this channel carries control verbs without polluting the Apple-1 keyboard stream. Depends on the CLI-verb and snapshot work above.

---

## Testing & CI

- [ ] **GitHub Actions CI (`ctest` matrix)** `[S · solid]` — `ctest` runs in ~10 s and the five native tests are stable. A nightly + on-PR workflow building Linux / macOS / Windows (build + `ctest`) plus a WASM job (`emcmake`, no tests) would catch portability breakage before merge. Zero runtime cost for the project; the main effort is pinning toolchain versions and mirroring `setup_imgui.{sh,bat}`.
- [ ] **Multiplexing Fantasy preset smoke tests** `[S · solid]` — presets #12 and #14 deliberately break Parmigiani's "one board at a time" rule by enabling overlapping cards (SID ↔ TMS9918 at `$CC00/$CC01`, GEN2 ↔ A1-IO at `$2000`, …). Add a `ctest` target that applies each preset, runs ~1 M CPU cycles, asserts no OOR strikes and every enabled peripheral's snapshot looks live. Protects the two "fantasy" presets from silent regression when bus-priority or eviction rules change.
- [ ] **Hex-dump loader fuzz harness** `[S · solid]` — `fb8c9bb` recently fixed an odd-length crash in `loadHexDump`; the parser handles comment lines, inline separators (`//` / `#` / `;`), continuation lines, `T` / `X` / `R` markers. Surface is small enough for libFuzzer: wrap the existing loader in a `LLVMFuzzerTestOneInput`, run nightly. Pre-empts the next regression without paying ongoing maintenance.
- [ ] **TSan / ASan opt-in builds** `[M · solid]` — the threading model (stateMutex + PriorityMutex + SPSC SID ring + snapshot publisher + keyboard queue + Terminal Card atomics) is classic TOCTOU territory. Add `-DPOM1_SANITIZERS=thread|address` to CMake (sanitize all non-vendored TUs; libresidfp + miniaudio stay clean). Run on a nightly CI job. Caught bugs go straight into the `ctest` suite.
- [ ] **Telnet tests — opt-in `ctest` integration** `[S · nice]` — wire `tools/test_*_telnet.py` into `ctest` behind `-DPOM1_ENABLE_TELNET_TESTS=ON` (off by default: needs a free TCP port and ~3 s boot per test). Would cover `test_sdcard_subdir_navigation_telnet.py` and `test_aci_telnet.py` at minimum.
- [ ] **`CHANGELOG.md` generator** `[S · nice]` — the TODO promises shipped features live in `git log`, but downstream users don't read the log. A tiny script (`tools/gen_changelog.sh` → `git log --merges --pretty=format:...`) that produces a rolling `CHANGELOG.md` per release tag gets the benefit without an ongoing hand-curated file.
