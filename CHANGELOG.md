# Changelog

Notable shipped work, recorded as it ships — both the **emulator** (lifted from
`TODO.md`) and the **6502 software** under `dev/` (libraries + `sketchs/` +
`dev/projects/` programs, lifted from `dev/TODO6502.md`). The authoritative commit-level history
is `git log`; the user-facing feature tour is `README.md`; open work lives in
`TODO.md` (emulator) and `dev/TODO6502.md` (6502). Format loosely follows
[Keep a Changelog](https://keepachangelog.com/). Versions track the string in
`src/main_imgui.cpp` / `README.md`.

## [Unreleased]

### Fixed — emulator (`src/`): bug-hunt sweep

Emulator-side fixes lifted from `TODO.md` — a defensive pass over snapshot/rewind
deserialisation, the CPU core, the TMS9918, the peripheral/storage stack, and a
second pass over the ImGui UI threading, card-conflict gating, and CLI parsing.
Built clean; all 32 `ctest` pass (Klaus, Harte cycle-exact, `cpu_interrupt`,
`snapshot_smoke`, `iec_snapshot_smoke`) (2026-06-21).

- **HIGH — heap overflow from a forged snapshot** (`Memory.cpp`
  `readSnapshotSections()`, MEM handler) — `ramSize` / `presetRamKB` restored
  from a `.snap`/rewind blob are now `std::clamp`'d (0..64 / 4..64). An
  unvalidated `ramSize` previously drove an out-of-bounds heap write of up to
  ~64 MB in `resetMemory()` / `clearMemory()`.
- **MEM section length validated** (`Memory.cpp`) — the declared `sectionLen`
  (`0x10000` + 12 scalar bytes) is checked before reading; a truncated/forged
  length now fails cleanly instead of loading garbage into RAM/state. Added
  `SnapshotReader::fail()` helper (`SnapshotIO.h`).
- **`loadBinary()` unseekable-file guard** (`Memory.cpp`) — checks
  `file.tellg() < 0` (procfs/FIFO/unseekable paths) before casting to `size_t`,
  mirroring `loadROM`; previously `SIZE_MAX` wrapped the size guard and threw an
  uncaught `std::length_error`.
- **CPU IRQ/NMI mutual exclusion** (`M6502.cpp` `step()`) — IRQ and NMI are now
  mutually exclusive with NMI taking priority (one interrupt per instruction
  boundary, 7 cycles, 3 bytes). A simultaneous IRQ+NMI previously ran BOTH
  handlers (14 cycles, 6 bytes pushed, wrong priority). Latent today (no
  production `setNMI()` caller); vectors unchanged (`$FFFA` NMI, `$FFFE` IRQ).
- **Undocumented multi-byte opcode lengths** (`M6502.cpp`,
  `Disassembler6502.cpp`) — 63 undocumented multi-byte opcodes that were
  dispatched to `Unoff()` (which never advanced PC, desyncing the instruction
  stream) are now mapped to `Unoff2` (2-byte) / `Unoff3` (3-byte) by their real
  NMOS addressing-mode length; the disassembler carries the matching addressing
  mode (mnemonic stays `???`). Documented opcodes untouched; Harte cycle-exact
  still passes.
- **TMS9918 5th-sprite latch independent of F** (`TMS9918.cpp`) — the per-line
  5th-sprite (5S) latch no longer gates on the F (frame) flag; 5S and F are
  independent status latches on silicon, matching the VBlank fallback path.
- **TMS9918 F (VBlank) edge across large slices** (`TMS9918.cpp`
  `advanceCycles()`) — the F flag is set by counting VBlank entries across the
  whole cycle span, so an oversized single `advanceCycles()` slice no longer
  drops the F edge.
- **microSD Timer-2 running flag persisted — SNAPSHOT FORMAT v3 → v4**
  (`MicroSD.cpp`, `SnapshotIO.h` `kSnapshotVersion`) — `t2Running` is now
  cleared in `reset()` and serialised/deserialised. The byte is appended at the
  end of the MicroSD section, gated on `r.version() >= 4`, so older v3 snapshots
  still load (`t2Running` defaults false). Independent of the POM1 application
  version string.
- **IEC card enum range-check on load** (`IECCard.cpp` `deserialize()`) —
  `role_`, `rxPhase_`, `txPhase_` restored from a snapshot are range-validated
  and fall back to `Idle` if out of range, mirroring `Drive1541`/`CFFA1`.
- **WiFiModem escape-guard '+' flush** (`WiFiModem.cpp` `advanceCycles()`) — the
  escape-guard timeout branch now flushes buffered `'+'` chars to the socket
  (1–2 trailing `'+'` followed by an idle pause were silently dropped from the
  TCP stream).
- **D64 fallback allocation spirals from the directory track** (`D64Image.cpp`
  `allocateSector()`) — fallback allocation now spirals outward from track 18,
  alternating below/above — authentic CBM DOS order — instead of packing from
  track 1; misleading comment corrected.
- **DevBench build-log header idempotency** (`Pom1BenchHost.cpp`
  `prependBuildLogHeader()`) — the guard used `compare(0, 24, …)` on a 25-char
  marker and never matched; now uses `rfind(marker, 0)`.
- **MEDIUM — screen-init data race** (`Screen_ImGui.cpp` `initializeScreen()`) —
  the power-on grid rewrite now holds `bufferMutex`. Reachable via
  `resetDisplay()` → `hardReset()` on the emulation thread (TerminalCard telnet
  hard-reset) concurrently with the UI thread's `render()` copy of `screenBuffer`;
  every sibling mutator already locked, this one didn't.
- **Toolbar honours the silicon-strict card-conflict gate** (`MainWindow_Menu.cpp`
  `renderToolbar()`) — the A1-SID / TMS9918 / GEN2 / A1-IO-RTC toolbar buttons now
  route through `gateStrictPlug()` like the Hardware menu, so a conflicting P-LAB
  pair (Parmigiani "one board at a time") can no longer be created from the toolbar
  in silicon-strict mode.
- **Cassette deserialize validates its element count** (`CassetteDevice.cpp`
  `deserialize()`) — the recorded-transition `count` is checked against the bytes
  actually present before `assign()`, mirroring `readByteVector()`; a forged
  `.snap`/rewind blob can no longer drive a multi-GB allocation attempt. Added
  `SnapshotReader::bytesAvailable()` accessor (`SnapshotIO.h`).
- **CLI address parser rejects trailing garbage** (`CliDispatcher.cpp`
  `parseAddr16()`) — both `std::stol` calls now verify the whole string was
  consumed (`idx == size()`), so `--load`/`--run`/`--break` reject malformed
  addresses like `"12G4"` instead of silently using a truncated value, matching
  `parseIntPositive()`.

### Added — 6502 software (`dev/`): shared graphics library, shared font, TMS9918 demos

6502-side work that ships under `dev/` (libraries + `sketchs/` + `dev/projects/` programs),
lifted from `dev/TODO6502.md`. The programs build to `software/<dir>/` via the
per-project Makefiles; dev loop → `sketchs/doc/APPLE1DEV.md`.

- **Shared geometry/number library `dev/lib/gfx/`** (2026-06-16/17) — additive
  layer factoring the line/circle/rect/ellipse + integer→ASCII routines that GEN2
  HGR and the TMS9918 bitmap card each duplicated. Backend resolved at link time
  (Parmigiani "one card at a time"): `gfx-gen2.lib` (280×192) and `gfx-tms.lib`
  (256×192) link-on-demand `ar65` archives. **GEN2 rewired** — `gen2_hgr_line/
  rect/circle` forward to `gfx_*`, `gen2_hgr_putx` → `gfx_hexstr`, new
  `gen2_hgr_ellipse`; **TMS9918 rewired** — `screen2_line/circle/ellipse` +
  `printlib` dec/hex route through `gfx_*`, TMS gains `screen2_rect`. Wired into
  the 5 GEN2 Makefiles, the 4 `screen2` TMS demos, and the Bench's GEN2-C cl65
  line; `make -C dev/lib/gfx check` compiles every TU against both backends.
- **Fast byte-aligned TMS rectangle fill** (2026-06-17) — `screen2_filled_rect`
  (`dev/apple1-videocard-lib/lib/screen_ext.c`) replaced its per-pixel
  `screen2_line` loop with a scanline left-partial / full-byte-run / right-partial
  fill (the TMS analogue of GEN2's `fill_pixrect`); ~10–14× fewer VRAM-port
  accesses, verified pixel-identical to a reference fill on 14 edge cases.
- **Card-neutral text façade `gfx_text` (axis 3)** (2026-06-20) — an 8×8
  cell-cursor model so a program positions text/numbers and compiles for either
  card by backend choice alone: `gfx_gotoxy` / `gfx_putc` / `gfx_text` /
  `gfx_putu` / `gfx_puti` / `gfx_putx`. Shared `gfx_text.c` owns the cursor +
  advance/wrap + formatting; per-card cell backends map a cell to the native blit
  (`gfx_text_backend_gen2.c` → `gen2_hgr_puts8`, 35×24; `gfx_text_backend_tms.c`
  → `screen2_putc`, 32×24 with `FG_BG` colour). Additive — the rich per-card text
  (GEN2 16×16 + NTSC colour, TMS sprites/true-colour) is untouched.
  `sketchs/portable/hello_gfx_text/` builds the SAME source for both cards and
  pins the façade; GEN2 render verified under POM1.
- **Shared Beautiful Boot font, multi-format emitter** (2026-06-17) —
  `tools/build_shared_font.py` emits one master (`dev/lib/hgr/bbfont_cp437.inc`)
  to both cards: HGR (`gen2_bbfont.inc`, bit 0 = left pixel) and TMS
  (`bbfont_tms.inc`, pattern table, bit 7 = left = bit-reversed HGR byte), plus
  the 37-glyph HUD subset (`font_hud8x8.inc`) now generated from the same master
  (Snake/Sokoban HUD text becomes BB). `--check` mode; `emit_bbfont.py` is now a
  compat shim.
- **TMS9918 Mode 2 (bitmap) graphics** — `init_vdp_g2`
  (`dev/lib/tms9918/tms9918m2.asm`), exercised by 7 programs (`tms9918_mandel`,
  `_asteroids`, `_maze3d`, `_light_corridor`, `_clone`, `_logo`).
- **TMS9918 Mode 1 demoscene** (2026-05-08) — `sketchs/tms9918/demo_plasma/`, a
  6502 port of Cruzer/jblang's *Plascii Petsma*: 12 effects × 16 palettes,
  auto-cycling, 1 433 B, stock 4 KB layout.
- **5th-sprite-overflow raster trap** (2026-05-08) —
  `dev/lib/tms9918/tms9918_5strigger.asm` (`arm_5s_trigger` / `wait_5s_trigger`,
  `WAIT_5S` macro): schedule a mid-frame palette/name-table swap without /INT (the
  TMS9918 has no line interrupt). Demo `sketchs/tms9918/demo_split/` (palette split
  at scanline 96).
- **Sprite-cloning (Bug N°8) visual fixture** (2026-05-08) —
  `sketchs/tms9918/demo_clone/`: SPACE toggles the illegal M1+M2 hybrid so the
  sprite-clone cascade appears/disappears for side-by-side comparison; validates
  the cloning model (`sketchs/doc/Programming_TMS9918.md` §15 Bug N°8).
- **Silicon-strict port of every TMS9918 program** (2026-04-30) —
  `tools/silicon_strict_patch.py` injected 351 `tms9918_pad12` NOPs across all
  TMS9918 projects + `lib/tms9918/*.asm`; all 3 CodeTank ROM layouts rebuild clean
  (`sketchs/doc/Programming_TMS9918.md` §25).
- **`dev/projects/*/README.md` TODO placeholders resolved** (2026-06-16).

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

- **Hardware spec resolved** — Bernie's `doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf`
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
  updated; CLAUDE.md updated; validation demo `sketchs/gen2/demo_a1_crazycycle/`
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
- **Worked example** — [`sketchs/apple1/demo_telemetry/`](sketchs/apple1/demo_telemetry/) (a "homing" game built
  on `telemetry.inc`, → `software/Telemetry/A1_TelemetryDemo.bin`) +
  `tools/test_telemetry_demo.py`: the harness reads `[player, target, won]`,
  sends a direction each frame, and the player converges on the target in 15
  deterministic frames — verified headless (no `DISPLAY`).
- `tools/test_telemetry_lockstep.py` refactored onto the library (dogfood) and
  switched to `--headless`.

### Added — POM1 Bench (in-app Arduino-style cc65 / Wozmon IDE)

An in-app sketch editor + build + upload + serial-monitor loop — the author
front-end for Bernie's "dream SDK". **Desktop-only** for the cc65 asm/C path (no
subprocess in WASM); the web build keeps the Wozmon-hex target + a "desktop
only — download the app" CTA banner (`IBenchHost::headerNote()`).

- **Serial Monitor (= telemetry)** — the *Telemetry Side Channel* panel became a
  real serial monitor: TX tap (`txMonitorRing`, hex/ASCII view, autoscroll,
  Clear), RX injection line (ASCII/Hex → `TelemetryPort::injectInbound`), a
  *Step frame* button (releases exactly one lock-step frame), and a *Log to
  file* field (same stream as `--telemetry-log`).
- **Editor + Upload** — *DevBench → POM1 Bench*: a 6502/cc65 editor (vendored
  ImGuiColorTextEdit, MIT, with a 6502 syntax definition), Open/Save, and Upload
  as Wozmon-hex or raw-bytes@$ (both stop → reset-vectors → hardReset → run).
- **Verify / Build & Run via cc65** (desktop) — toolchain probe (`whichExe`,
  exe-relative then `$PATH`), a *Board*/*Target* selector, `ca65`/`ld65`/`cl65`
  output captured into a Build-output console with clickable error lines
  (`parseBenchErrorMarkers` → `TextEditor::SetErrorMarkers`).
- **Arduino-style chrome** — teal toolbar with round Verify/Upload/New/Open/Save
  + Serial-monitor buttons, Source/Board selectors, a sketch tab, cream editor,
  dark console (orange error lines), teal status bar.
- **Targets = machine + build** — `kBenchTargets[]` bundles a POM1 preset
  (`applyMachineConfig`), a cc65 `.cfg`, a source mode, and an optional companion
  asset; selecting a target plugs the machine it runs on. Includes the
  TMS9918-C-as-CodeTank-ROM path (16K@$4000 → pad 32K → `loadCodeTankRom` →
  `4000R`). The New-sketch dialog is a 2 languages × 4 machines matrix
  (Apple-1 dual-4K/8K · P-LAB TMS9918 · Uncle Bernie GEN2 HGR · Bernie GEN2 TXT).
- **Built-in Examples** — Blink (asm/hex), Hello world (C/TMS9918),
  A-1-CrazyCycle (GEN2 HGR), Telemetry demo.
- **Bundled cc65 (packaging)** — `Pom1BenchHost::probe()` resolves
  `ca65/ld65/cl65` exe-relative first (`<exe>/cc65/bin`, macOS `Resources`,
  AppImage `share/POM1`) + a `POM1_CC65_DIR` override; `ensureCc65Home()` points
  `CC65_HOME` at the bundle's `share/cc65`. `tools/build_cc65_bundle.sh` stages a
  relocatable tree (self-tests `cl65`); the three packagers stage it
  conditionally (warn + continue if absent). Pinned by `process_util_smoke`.

### Added — CI + cycle-exact CPU test

- **GitHub Actions CI** (`.github/workflows/ci.yml`, Linux) — build → `ctest`
  (Klaus + Harte cycle-exact + smoke + graphics regression) →
  `make -C dev/projects` (build-all). Verified green on GitHub infra.
- **Per-opcode cycle-exact CPU oracle** (`cpu_harte_smoke`) — Tom Harte "65x02
  ProcessorTests", 100 cases × 151 documented opcodes, asserting final regs +
  RAM **and** cycle count (15100/15100). Fixed several `M6502.cpp` cycle counts
  (`zp,X`/`zp,Y`/`(zp,X)`/INC-DEC memory/JSR/RTS/BRK/RTI), decimal ADC/SBC
  (Bruce-Clark NMOS), and the PLP/RTI B-bits. Klaus stays green; IRQ/NMI/BRK line
  timing covered by `cpu_interrupt_smoke`.
