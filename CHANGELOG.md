# Changelog

Notable **emulator** changes, lifted from `TODO.md` as work ships. The
authoritative commit-level history is `git log`; the user-facing feature tour is
`README.md`; open work lives in `TODO.md`. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/). Versions track the string in
`src/main_imgui.cpp` / `README.md`.

## [Unreleased]

### Added — DevBench menu + Bench GEN2 text target

- **DevBench top-level menu** (`MainWindow_Menu.cpp`) — groups the dev tooling:
  *POM1 Bench (sketch editor)*, *Telemetry Side Channel*, *TMS9918 VDP
  Inspector*, *Silicon Strict Inspector*. Telemetry and Silicon Strict moved
  here out of Settings.
- **TMS9918 VDP Inspector always available** — opening it from DevBench
  auto-plugs the TMS9918 (and evicts A1-AUDIO SE) so there is a live VDP to
  inspect; `renderTMS9918InspectorWindow()` is no longer gated on
  `tms9918Enabled` (`MainWindow_ImGui.cpp`, `if (showTMS9918Inspector)`).
- **Bench "Bernie GEN2 TXT" target** + 4th machine axis in the New-sketch
  dialog (`Pom1BenchHost.cpp`) — native GEN2 TEXT mode (40×24, page `$0400`,
  built-in font, `$C251`), asm + C starters (`apple1_gen2.cfg` / `C-gen2`).
  The New-sketch matrix is now 2 languages × 4 machines (`targetFor` =
  `language*4 + machine`).
- **Bench New-dialog hints** — optional `IBenchHost::languageHints()` /
  `machineHints()` (`bench/IBenchHost.h`, `Pom1BenchHost.cpp`), shown inline in
  the dialog; P-LAB cited for the TMS9918 entries.
- **`gen2_hgr_puts()` / `gen2_hgr_row()`** in `dev/lib/gen2c`
  (`gen2.c`/`gen2.h`) — draw an ASCII string in GEN2 HIRES using an embedded
  Beautiful Boot font, pixel-doubled (H+V) to solid white with no NTSC
  artifacts (16×16 cells, 18px pitch); `gen2_hgr_row` resolves a scanline base.
- **Settings → A1-SID version & addresses** submenu (`MainWindow_Menu.cpp`) —
  pick A1-SID (`$C800-$CFFF`) vs A1-AUDIO SE (`$CC00-$CC1F`) and list all 29 SID
  register addresses for the active variant.

### Changed — Preset table merge + GEN2/TMS starters

- **Preset table simplified** (`MainWindow_Presets.cpp`) — the A1-SID and
  A1-AUDIO Special Edition presets merged into ONE preset #6 (I/O window
  selectable in *Settings → A1-SID version & addresses*). All later presets
  renumbered (old 8→7 … 14→13); final range 0–13. Invariant kept: no TMS9918
  preset without CodeTank — P-LAB Multiplexing Fantasy (#11) now plugs CodeTank
  GAME1 (`roms/codetank/Codetank_GAME1.rom`). POM1 Multiplexing Fantasy (#13)
  now plugs ACI by default. Preset numbers updated repo-wide in docs/tools.
- **GEN2 hello-world starters** — GEN2 HGR asm + C now render BBFont
  "HELLO WORLD" (pixel-doubled white text) instead of a fill/X;
  `gen2_hgr_clear`/`gen2_hgr_puts` optimised (~20× faster — page/Y-indexed
  clear, per-row base resolution). TMS9918 asm starter does a proper VDP init
  (blank during setup → clear VRAM → park sprites → screen on last).
- **Bench window** (`bench/CodeBench.cpp`) — taller default (660×720) + min
  size (520×480, `SetNextWindowSizeConstraints`); bottom status bar pinned
  flush to the window bottom (was leaving a ~30px gap).

### Added — State rewind (microM8-style timeline)

- **Timeline scrub + delta ring (MVP)** — `RewindBuffer`: delta-encoded ring of
  in-memory snapshot blobs (section + 256 B chunk deltas, keyframe-anchored
  segments, 128 MB budget eviction), captured from the emulation slice
  (~4 captures/s). UI: **CPU → State Rewind…** scrub panel — slider preview
  (pause + restore), *Resume here* (truncate the rewound-past future), *Back to
  live*. Pinned by `rewind_buffer_smoke`.
- **Inline timeline band in the toolbar** — a live scrubber between the
  silicon/fantasy badge and the About button (`renderToolbar`). Recording
  auto-starts once so the band is live out of the box; dragging it back
  previews that instant on every plugged display (`rewindSeekTo` restores +
  republishes the state), and releasing resumes the machine there
  (`rewindResumeHere`). Shows `LIVE` / `REWIND -N.Ns`, auto-sizes to the gap and
  widens with the window. Shares the proven seek/resume API with the panel.
- **Snapshots now capture the visible screen** (new `SCREEN` section via a
  `DisplayDevice::serialize` hook + `Screen_ImGui` override). The Apple-1 text
  grid lives in the display device, not in RAM, so before this a rewind/restore
  moved CPU+RAM back but the on-screen text stayed at the live frame — scrubbing
  appeared to do nothing on a text preset. Now the grid (content + scroll +
  cursor) rides along, so dragging the timeline shows the older screen images.
  Benefits `.snap` save/load too; backward-compatible (old `.snap` files simply
  lack the section). Memory-only fixtures (no display) skip it.
- **Desktop only** — rewind capture runs on the dedicated emulation thread; the
  single-threaded, memory-bounded WASM build can't afford the periodic
  full-state capture on its one main-loop thread, so rewind (capture + the
  toolbar band + the *State Rewind* panel) is compiled out (`#if !POM1_IS_WASM`)
  in the web build.

### Added — Uncle Bernie GEN2 colour graphics: cycle-accurate beam-racing engine (POM2 back-port)

GEN2 moves from a passive `$2000-$3FFF` framebuffer + end-of-frame MAME
rasteriser to a cycle-accurate, beam-raced video subsystem driven by the
release card's `$C250-$C257` soft switches.

- **Hardware spec resolved** — Bernie's `doc/ColorGraphicsCard_doc_for_Arnaud.pdf`
  transcribed; Q1–Q10 closed in `doc/GEN2_RELEASE_questions.md`. Read-only
  switches (a read toggles + returns HST0 in D7; writes are no-ops), HST0 high in
  H/V-blank with a notch during the 3-cycle colour burst, HIRES page 2
  `$4000-$5FFF`, decode `SEL = $Cxxx & !A11 & A9 & A4`, indeterminate power-on
  latch (software must init; Apple-1 RESET leaves it alone), 65 cyc/line × 262
  @60 Hz / 312 @50 Hz.
- **Developer guide ("Bernie SDK")** — `doc/GEN2_RELEASE.md`: `$C25x` map, HST0
  recipes, `$C05x`→`$C25x` porting table, dual-monitor, 48 KB RAM, the POM1 dev
  loop. Reusable `dev/lib/gen2/` (`gen2.inc` equates + cheat-sheet,
  `gen2_sync.asm` coarse `gen2_waitvbl` + exact `gen2_beam_lock`).
- **Video clock + floating bus** — `Gen2VideoScanner` (`src/Gen2VideoScanner.{h,cpp}`):
  NTSC cycle counter (`65×262`/`65×312`), verbatim MAME `scanner_address`,
  `floatingBus(mem)` (HGR page 1 `$2000` / page 2 `$4000`); advanced from
  `Memory::advanceCycles()` when the HGR framebuffer is attached. Pinned by
  `gen2_floatingbus_smoke`.
- **Soft switches `$C250-$C257` + HST0** — registered on `PeripheralBus` over
  `$C200-$C7FF` with Bernie's mirror decode (`$C2/$C3/$C6/$C7xx`, A4=1); a read
  toggles the switch, journals a `Gen2VideoScanner::Event`, and returns
  `(HST0<<7) | xorshift-noise`; decoded writes are blocked. 50/60 Hz jumper
  exposed (`setGen2FiftyHz`) + persisted in the `GEN2VID` snapshot section.
  Pinned by `gen2_softswitch_msb_smoke`.
- **Beam-raced renderer** — per-frame `VideoEvent` journal (`emuCycle` as the
  single source of truth, republished at every video-frame rollover),
  `frameCycleToPos` + `forEachBeamSegment`, `GraphicsCard::render(memory,
  endState, frameStart, events, linesPerFrame)` with a zero-regression
  `rasterizeToBuffer()` fast path for pre-beam HGR programs. Modes: TEXT 40×24
  B&W (built-in 5×7 font), LORES 40×48/16-colour, HIRES 280×192 NTSC artifact,
  MIXED, PAGE2. **Vertical and horizontal mid-scanline splits** (whole-line
  decode, clipped write-back, NTSC neighbour context preserved at the boundary).
  Pinned by `gen2_beam_race_smoke`, `gen2_horizontal_split_smoke`. OOR-strict
  carve-out extended to `$2000-$5FFF` (card DRAM behind both HGR pages).
- **Product integration** — preset 13 plugs the engine via
  `setHgrFramebufferAttached`; Hardware Reference + tooltips + memory map
  updated; CLAUDE.md updated; validation demo `dev/projects/a1_crazycycle/`
  (→ `software/Graphic HGR/A-1-CrazyCycle.{bin,txt}`, `E000R`) — latch init by
  reads, HGR colour test card, then a beam-raced TEXT window mid-pattern with
  cycle-exact HST0 sync and per-line horizontal splits.

### Added — Telemetry side channel (dev-only test-harness port)

Binary, frame-delimited bridge for automated game testing — the generalisation
of the Terminal Card's `$D012`→TCP→`$D010` bridge. The game writes its state to a
4-byte MMIO window; an external harness reads it over TCP and drives synthetic
input. Design: `doc/TELEMETRY_SIDE_CHANNEL.md`. Not real hardware — a dev aid in
the "fantasy" category.

- **`TelemetryPort`** (`src/TelemetryPort.{h,cpp}`) — a `pom1::Peripheral`
  modelled on `TerminalCard`: MMIO window `$C440-$C443`
  (`TELE_DATA`/`TELE_CTRL`/`TELE_IN`/`TELE_INLEN`), outbound frames
  `0xAA <len16-le> <payload>` flushed from `advanceCycles`, inbound FIFO, TCP
  server on localhost (default `:6503`, reuses `SocketHandle` + the TerminalCard
  socket pattern, WASM no-op stubs), `KeyInjector` for synthetic keyboard input
  (`$D010/$D011`). State is transient — `serialize`/`deserialize` are no-ops.
- **Bus window `$C440-$C443`** — the `$C4xx` A9=0 dead zone GEN2's decoder
  (`SEL = $Cxxx & !A11 & A9 & A4`) is structurally blind to and no other card
  claims (ACI `$C0/$C1xx`, SID `$C8xx+`, Juke-Box ≤ `$BFFF`, PIA `$Dxxx`);
  registered on the `PeripheralBus` at priority 30 so it wins over GEN2's broad
  `$C200-$C7FF` pass-through. Disabled by default.
- **`--telemetry-port <N>`** — opens the channel on `127.0.0.1:N` (CliDispatcher
  → MainWindow override → `EmulationController::setTelemetryListenPort` +
  `setTelemetryEnabled`). Documented in `doc/CLI.md`.
- **Deterministic lock-step** (`kCtrlLockstepOn` / `$02`) — an end-frame write
  parks the CPU at that instruction until the harness sends `kAckByte` (`0x06`):
  `Memory` calls `M6502::stop()` for a cycle-exact halt, `EmulationController`'s
  slice loop pumps the socket via `TelemetryPort::serviceStall()` between slices
  (stateMutex released — no deadlock, UI stays live), a 5 s timeout auto-resumes a
  dead harness. Game-transparent (no game-side polling). Verified end-to-end:
  exactly one frame per ACK, CPU provably parked between frames — pinned by
  `tools/test_telemetry_lockstep.py` (assembles a 6502 emitter, drives it over
  the socket).
- **`--telemetry-log <path>`** — tees the outbound frame stream to a binary file
  (same framing as the socket) for golden-trace CI — no live harness needed,
  diff a run against an expected capture. Implies enabling the port. Captures
  every frame even under socket backpressure. Documented in `doc/CLI.md`.
- **UI status panel** — *View → Telemetry Side Channel…* shows the snapshot
  (enabled, listen port, connected harness, lock-step, frames/bytes) with an
  Enable toggle; fed via `EmulationSnapshot.telemetry` / `SnapshotPublisher`.
- **`--headless`** — run with no GLFW window / GL / ImGui (`runHeadless` in
  `main_imgui.cpp`): a separate driver builds `EmulationController` (own emulation
  thread, null screen), applies speed + telemetry + Phase-C deferred verbs, then
  idles until SIGINT/SIGTERM. Lets the telemetry regression tests run on a
  display-less CI box — verified: `tools/test_telemetry_lockstep.py` now launches
  `--headless` and passes with `DISPLAY` unset (while the GUI path correctly fails
  `glfwInit` there). **`--preset` / `--enable` / `--disable` are applied headless
  too** (`MainWindow_ImGui::applyHeadlessConfig` — RAM + cards + BASIC ROM,
  plugged immediately since there is no frame loop): `tools/test_headless_preset.py`
  proves `--preset 13` plugs Uncle Bernie's GEN2 (the soft-switch HST0 bit
  toggles) with no display, while the default machine does not.
- **`Memory` out-of-line `~Memory()`** — added so the forward-declared peripheral
  `unique_ptr`s only need their complete type at the single dtor definition point
  (was previously relying on transitive includes in every TU that destroys a
  `Memory`).
- **Not yet** (tracked in `TODO.md`): the CI **GitHub Actions** workflow itself
  (now unblocked — it can drive `--headless --preset` + the telemetry tests).

### Added — Telemetry game-testing SDK kit

Turns the raw telemetry mechanism into a usable kit — the "dream SDK" loop
(compile → load → automated test that *sees* state + *drives* input),
demonstrated end-to-end with no human and no display.

- **`dev/lib/telemetry/telemetry.inc`** — cc65 6502-side library: `$C440-$C443`
  equates + macros (`TELE_ARM`, `TELE_PUT`/`TELE_PUTA`/`TELE_PUTI`, `TELE_FRAME`)
  so a game emits its per-frame state in a couple of lines.
- **`tools/pom1_telemetry.py`** — Python harness library: `TelemetryClient`
  (connect-with-retry, `read_frame`, `send`, `ack`, `step`) + a `launch_headless`
  context manager (boots POM1 `--headless`, connects, tears down). Tests reason
  over frames + inputs instead of parsing sockets.
- **Worked example** — `dev/projects/a1_telemetry_demo/` (a "homing" game built
  on `telemetry.inc`, → `software/Telemetry/A1_TelemetryDemo.bin`) +
  `tools/test_telemetry_demo.py`: the harness reads `[player, target, won]`,
  sends a direction each frame, and the player converges on the target in 15
  deterministic frames — verified headless (no `DISPLAY`).
- `tools/test_telemetry_lockstep.py` refactored onto the library (dogfood) and
  switched to `--headless`.
