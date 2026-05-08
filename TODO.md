# TODO

Open work on the **emulator** only. Shipped features → `git log` / `README.md`. 6502 software work → `dev/TODO6502.md`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Sections ordered by actionability: implementable first, externally-blocked last.

---

## 🔌 Peripherals

- [ ] **P-LAB IEC Card** `[M · solid]` — Parmigiani's Commodore IEC serial bus card; lets the Apple-1 talk to 1541 floppy / printer / etc. Spec: https://p-l4b.github.io/iec/. Investigate register window + ATN/CLK/DATA handshake; honour mutex rules. Backing store probably a host `.d64` + small IEC state machine. New preset + Hardware Reference entry.
- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — https://github.com/flowenol/apple1-serial — serial-port bootloader / terminal (complements TurboType / 8BitFlux). Pipes through Terminal Card or its own ACIA variant; likely a text-format loader on top of `Memory::loadHexDump` + paste pipeline.
- [ ] **ACI header + checksum on the jaquette** `[S · nice]` — `tapeinfo.txt` already drives the *"Type 0280.0FFFR"* label. Parse the raw `.aci` pulse-capture header (from / to / checksum) in `CassetteDevice::loadAciTape()` and surface for tapes without a sidecar entry.
- [ ] **Briel Multi I/O — SpeakJet** `[M · nice]` — 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as separate optional peripheral so it coexists with microSD.

---

## 📥 Loaders

- [ ] **TurboType 57 600-baud loader** `[M · solid]` — Uncle Bernie's format, shipped by 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244). Protocol: Wozmon-speed bootstrap (200 ms/newline, 20 ms/char) installs an in-RAM dropper that **skips `$D012` echoes** and streams bytes at 57.6 kbps with running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in <30 s vs ~2 400 baud Wozmon. POM1 side: parse `.TUR`/`.APL`, switch Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon.

---

## 🎨 Visuals & UX

- [ ] **Native file dialog** `[M · solid]` — drop in `nfd` (NativeFileDialog) or `tinyfiledialogs` — header-light, MIT, cross-platform. The in-app browser stays in the way.
- [ ] **HiDPI font scaling on Linux** `[S · nice]` — auto-detect monitor DPI on first window creation (`glfwGetMonitorContentScale`, GLFW 3.3+) and scale the default font; keep a Hardware → Display setting to override. Currently users must tweak `ImGui::GetIO().FontGlobalScale` manually.
- [ ] **1976 CRT fidelity (opt-in, default off)** `[M · nice]` — two sub-effects under the existing CRT toggle:
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing) — chars land ~60 / s, hardware scroll shifts buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock) — periodic static, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` after backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.
- [ ] **Shared video texture layer** `[M · solid]` — `Screen_ImGui.cpp` and `MainWindow_HardwareWindows.cpp` still own raw OpenGL texture lifecycles directly (glyph atlas, GEN2 HGR, TMS9918, GT-6144). Factor a backend-neutral texture helper for create/update/destroy + filter mode (`GL_NEAREST` pixel cards, `GL_LINEAR` glyph atlas) so the future Metal backend ports one abstraction.

---

## 🔧 Technical debt

- [ ] **Snapshot save / load** `[M · solid]` — `--snapshot-save <path>` / `--snapshot-load <path>` serialising `EmulationSnapshot` + every peripheral's internal state (SID register file + filter, TMS9918 VRAM + regs, microSD fs cursor, modem connection, cassette transport + tape offset, CFFA1 disk offset, Juke-Box bank). Enables deterministic replay, reloadable test fixtures, one-click bug reports. Versioned on-disk format.
- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) reading line-delimited commands while the emulator runs — same verbs as CLI flags, but for stateful sequences. Telnet on `:6502` carries keystrokes + display; this channel carries control without polluting the keyboard stream. Depends on CLI-verb + snapshot work above.
- [ ] **Terminal Card — `Ctrl-K` hand-over** `[S · nice]` — match the 8BitFlux toggle: a `Ctrl-K` byte suspends `$D010`/`$D011` injection until `Ctrl-T` re-attaches. Useful once a script bootstrapped a program and the user wants to play without dropping the session. Hook: `injectionSuspended` next to `escapePending` / `eightBitMode` in `TerminalCard.cpp`.
- [ ] **External `presets.json`** `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` to JSON under `doc/` (or next to the executable) so users add presets without recompiling. Loader in `MainWindow_Presets.cpp`, keep the C++ table as fallback.
- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL deprecated since 10.14 (silenced via `GL_SILENCE_DEPRECATION`). `imgui_impl_metal.mm` + `imgui_impl_osx.mm` are drop-in; scope is porting raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 + HGR + GT-6144 textures) to MTLTexture.

---

## 🧪 Testing & CI

- [ ] **GitHub Actions CI (ctest matrix)** `[S · solid]` — `ctest` runs in ~5 s, all native tests stable. Nightly + on-PR matrix building Linux / macOS / Windows (build + `ctest`) plus a WASM job (`emcmake`, no tests). Main effort: pinning toolchain versions and mirroring `setup_imgui.{sh,bat}`.

---

## ⏸️ Deferred / conditional

Spec public, code change tractable, but gated on a real-world trigger (software exercising the feature, user demand, hardware availability). Promote when the trigger appears.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk` / `.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.

---

## 🚫 Blocked on external

- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **PROM unpublished** (Bernie's physical kits, sold out; Applefritter says it will never be released). Contact [Applefritter PM](https://www.applefritter.com/user/254186/track) — he shared docs with the HoneyCrisp dev. Once obtained: load at `$C500`, extend `CassetteDevice` for header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
