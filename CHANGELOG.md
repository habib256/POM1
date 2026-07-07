# Changelog

Notable shipped work, recorded as it ships — both the **emulator** (lifted from
`TODO.md`) and the **6502 software** under `dev/` (libraries + `sketchs/` +
`dev/projects/` programs, lifted from `dev/TODO6502.md`). The authoritative commit-level history
is `git log`; the user-facing feature tour is `README.md`; open work lives in
`TODO.md` (emulator) and `dev/TODO6502.md` (6502). Format loosely follows
[Keep a Changelog](https://keepachangelog.com/). Versions track the string in
`src/main_imgui.cpp` / `README.md`.

## [Unreleased]

### Changed — CodeTank ROM library: four release cartridges (Claudio burn plan)

- **The GAME1-7 line-up is reorganised into 4 named cartridges** so Claudio's
  four EPROM burns cover the whole library (silicon-validated titles ride the
  same chips as the remediated/new banks he must test):
  **`Codetank_CLASSICS.rom`** (Tetris ✅ | Chess 🆕),
  **`Codetank_BASIC_LOGO.rom`** (LOGO V2.6 ✅ | Applesoft TMS 🆕),
  **`Codetank_ARCADE.rom`** (menu Galaga 🔴/Sokoban ✅/Snake ✅ | Rogue 🔴),
  **`Codetank_DEMOS.rom`** (menu Life ✅/Mandel 🔴/Plasma ✅/Vague 🆕/Nyan ✅ |
  Animals 🆕). The DEMOS lower bank packs all five small demos behind a new
  5-entry menu (`dev/projects/codetank/demos_menu/`, slots $4200/$4A00/$5200/
  $5A00/$6000 pinned by new `*_demos_bank.cfg`s); Nyan is slot-linked at
  $6000, Animals keeps its full-bank C build. TMS9918_Hello and TMS_Split
  leave the cartridge line-up (DevBench sketches only); the GAME5/6 packer
  machinery left `build_codetank_rom.py`. `TMS_Mandel.asm` (remediated build)
  was restored from git history — its source had left the tree while its bank
  still shipped. ARCADE is the default probe rom (`Memory.cpp`, presets).
  `tools/verify_codetank_roms.py` scenarios rewritten for the 4 carts —
  **Chess, Applesoft, Vague-from-menu and Nyan-from-menu are now covered by
  the Claudio gate** (Chess/GAME7 previously had no scenario at all).
- **`CODETANKDEV.rom` is now a pure two-slot flash cartridge — generated,
  never committed** (untracked + .gitignore). Both 16 kB banks are blank $FF
  flash slots; `flashCodeTankDevRom` composes the file from scratch when
  absent (desktop and WASM MEMFS alike, no toolchain needed — packagers call
  `--rom dev` unconditionally now). The Applesoft TMS bank it used to carry
  ships stabilised in `Codetank_BASIC_LOGO.rom`, and the DevBench injection
  paths for **both** TMS interpreters (Applesoft upper / LOGO lower) load
  that cartridge (`Pom1BenchHost.cpp`; tests updated:
  `applesoft_tms9918_smoke`, `applesoft_gen2_smoke`, `basic_compiler_smoke`,
  `bench_logo_inject_smoke`, `codetank_smoke`).
- **DevBench flash-bank picker**: CodeTank asm/C targets get an "Upper"
  toggle in the bench toolbar (new `IBenchHost::flashBankApplies`/
  `flashUpperBank` seam, default hidden) — Run flashes the chosen 16 kB half
  of CODETANKDEV and boots the matching board jumper, preserving the other
  bank's program across flashes.

### Fixed — boot profile chooser (bug-hunt pass)

- **The chooser's LOGO buttons were dead**: they passed machine indices 9/10
  into `targetFor()` after the `kP1Machines[]` compaction moved LOGO to rows
  7/8, so both buttons resolved to target -1. Machine rows consumed by name
  are now the `kP1Machine*` constants (`Pom1BenchHost.h`) with a size
  static_assert on the array.
- **Beeper SFX from the chooser vs `--disable aci`**: the Beeper branch now
  re-asserts `aciEnabled`/`pendingAciEnable` after `applyBootConfig` (like
  the SID branch) — the persistent CLI override used to clear the pending
  plug and force the editor's same-frame emergency plug (the documented
  silent-card-on-boot condition).
- **Tools → SID Tracker left a phantom Juke-Box checkmark**: the handler now
  clears `jukeBoxEnabled` like the Hardware-menu A1-SID item —
  `Memory::setSIDEnabled` already evicted the card on the bus ($CA00 sits in
  the SID window), so the stale UI flag even produced a bogus conflict row in
  the Silicon Strict Inspector.

### Added — Terminal Card: `Ctrl-K` injection hand-over

- **`Ctrl-K` (or `ESC K`) suspends/resumes keyboard injection** on the Terminal
  Card. While suspended, incoming TCP **data** bytes are dropped so the local POM1
  keyboard drives the Apple 1 — useful once a script has bootstrapped a program and
  you want to play — **without dropping the session**. It's a symmetric toggle: a
  second `Ctrl-K` re-attaches. Control commands (incl. `Ctrl-K` itself) still bite
  while suspended, and `Ctrl-K` is an escape hatch like `Ctrl-T` (works in 8-bit raw
  mode too). New `injectionSuspended` state (next to `eightBitMode`), surfaced in the
  Terminal Card hardware window + the client welcome banner. Pinned by
  `terminal_card_injection_smoke`. *(The TODO sketched re-attach via `Ctrl-T`, but
  that byte already toggles 8-bit mode — a symmetric `Ctrl-K` avoids overloading it.)*

### Added — Bench BASIC: Verify loads (ready to LIST) + cold/warm start toggle

- **Verify now LOADS the tokenised program into the live interpreter, ready to
  `LIST`** (instead of a host-side compile-check that touched no machine state).
  `Pom1BenchHost::injectBasic(run=false)` cold-starts the interpreter, pokes the
  image + pointers, and enters at the *prompt* rather than running: Integer BASIC
  via its warm start `$E2B3`, Applesoft by rewriting the launcher's trailing
  `JMP NEWSTT` → `JMP <warm>` (`$6003`/`$9803`/`$E003`/`$4003`) after `JSR SETPTRS`
  installs the pointers. So the program is present + `LIST`able and nothing runs;
  Run (`run=true`) is unchanged (loads + launches). All five BASIC targets.
- **Cold/warm start toggle** — a "Warm" checkbox in the Bench toolbar (shown only
  for BASIC targets, default **cold**). Warm re-enters the resident interpreter
  through its warm entry (`E2B3R`/`6003R`) and skips the hard-reset + ROM reload,
  so a program already typed at the REPL survives a Verify/Run; cold (`E000R`/
  `6000R`) is the classic clean boot. Warm is honoured only when that interpreter
  is actually resident (a preset switch or any non-BASIC build clears the residency
  flag), so it never warm-enters unmapped RAM. New portable seam
  `IBenchHost::warmStartApplies/warmStart/setWarmStart`.

### Added — native BASIC compiler: `ATN` + `RND`

- **`ATN(x)` and `RND(x)` now compile natively** (`BasicCompilerApplesoft` +
  `dev/lib/basicrt/basicrt_float.s`). `fp_atn` is a two-stage range reduction
  (`atan(1/x)` reciprocal fold + a `pi/6` offset fold to `|t| ≤ tan(π/12)`) then a
  4-term odd Taylor, built entirely on the existing `fp_*` core; `fp_rand` advances
  an xorshift32 state and forms the mantissa of a `[1,2)` single, minus 1 → `[0,1)`
  (its argument is evaluated then discarded — Applesoft's `RND(0)`/`RND(<0)` are not
  modelled). Both are feature-gated (`-D FP_ATN` / `-D FP_RAND`) so a program links
  them only when used, and either one auto-selects the float phase. The shared
  `LDF`/`CPF` float-constant macros moved to file scope so a routine can use them
  without `FP_SIN`. Pinned: `basic_float_runtime` (5000-point ATN grid vs `atanf`,
  RND range + non-degeneracy), `basic_native_codegen` (emits `jsr fp_atn`/`fp_rand`),
  `basic_native_run` (end-to-end arctan-curve + RND-scatter native programs). Doc:
  [`BASIC_COMPILER.md`](../doc/BASIC_COMPILER.md).

### Fixed — documented `AND`/`OR` semantics divergence (not a tokeniser bug)

- **Investigated a reported "`IF (X AND 7)=0` freezes the interpreter" bug: it is a
  misdiagnosis.** The tokeniser emits bytes byte-identical to the interpreter ROM's
  own CRUNCH (verified end-to-end on the real ROM; new byte-exact regression in
  `basic_compiler_tokenize`). The real cause is inherent Applesoft semantics: `AND`/
  `OR` are **logical** (nonzero→1) in the interpreter but **bitwise** in the native
  compiler, so a bit-mask idiom loops forever under Inject yet works under Compile —
  a genuine per-mode divergence surfaced by the new Inject/Compile toggle. Documented
  in [`BASIC_COMPILER.md`](../doc/BASIC_COMPILER.md) (no code change — both modes are
  correct, just different).

### Added — DevBench BASIC: explicit "Inject | Compile" mode toggle

- **The *New* dialog now exposes inject-vs-native-compile as a segmented Mode
  toggle** on the Applesoft BASIC row (`bench/CodeBench.cpp` `drawNewDialog`),
  instead of two look-alike "(native compile)" pseudo-machines in the Target combo.
  **Inject (interpreter)** = the ahead-of-time tokeniser (`BasicTokeniserApplesoft`,
  runs on the resident ROM); **Compile (native)** = the standalone 6502 codegen
  (`BasicCompilerApplesoft`, no interpreter, ~20× faster, `$0300`). The toggle
  shows only for the two machines with a native compiler (Applesoft GEN2 / TMS9918)
  and is **desktop-only** — WASM (no cc65) collapses to Inject; microSD / CFFA1 /
  Integer BASIC stay inject-only. The status-bar *Mode* switcher gained a matching
  indented "Compile (native)" row so native stays reachable in place.
- **New host seam `IBenchHost::nativeSiblingOf(target)`** (default −1, overridden in
  `Pom1BenchHost`: inject 9 → native 12, inject 11 → native 13, −1 on WASM) — the
  portable `bench/` module surfaces the toggle without knowing about card-specific
  compilers. The two `kP1Machines` "native compile" rows are gone; the native
  targets (12/13) and their `mode 5` dispatch are unchanged, so all
  `basic_native_*` pins keep passing. Doc: [`DEVBENCH.md`](../doc/DEVBENCH.md).

### Added — HiDPI UI font scaling (Linux / Windows)

- **The UI font auto-scales to the monitor's DPI on startup** (`main_imgui.cpp`,
  `glfwGetWindowContentScale`, GLFW 3.3+): on Linux/X11 and Windows — where GLFW
  does not scale the framebuffer — a high-DPI monitor no longer renders the whole
  UI tiny. macOS (Retina handled by `io.DisplayFramebufferScale`) and WASM (the
  browser owns devicePixelRatio) are deliberately left untouched. **Display
  Settings** gains an *Auto (follow monitor DPI)* toggle + a manual **UI font
  scale** slider (0.75–3.0×) driving `io.FontGlobalScale`, replacing the manual
  poke it documented.

### Added — DevBench editor polish + GEN2 default = OpenEmulator composite

- **Markdown syntax highlighting in the editor's Edit mode** (`bench/BenchLang.cpp`
  `langMarkdown()`, routed via `langDef("markdown")` — was plain text): a light
  regex "pencil" for `#` headings, `**bold**`/`__bold__`, `*italic*`/`_italic_`,
  `` `code` `` and `[text](url)` links. Preview still renders the real formatting.
- **Unsaved-close guard** (`CodeBench`): closing a dirty tab (X, context Close, or
  Close others/all when any tab is dirty) now pauses on a **Discard / Cancel** modal
  (Discard in red, `Esc` = Cancel) instead of silently throwing away edits.
- **GEN2 HGR now renders with the OpenEmulator composite NTSC decode by default**
  (`MainWindow_ImGui.h` `gen2RenderMode = 1`) — the more faithful decode is what you
  see on first boot. The MAME artifact LUT stays a click away in the GEN2 menu, and
  remains the `GraphicsCard` standalone default so `gfx_regress_gen2` keeps its
  reference image.

### Fixed — stray Xlib clipboard error no longer crashes the app (Linux/X11)

- A raw Xlib protocol error (classically a clipboard `SelectionRequest` racing a
  requestor whose window has already vanished → `X_ChangeProperty` `BadWindow`)
  bypassed GLFW's error callback and hit Xlib's **default handler, which calls
  `exit()`** — taking the whole emulator down mid-session. POM1 now installs a
  non-fatal Xlib error handler at startup (`src/X11ErrorGuard.{h,cpp}`,
  `pom1InstallX11ErrorGuard()` right after `glfwInit`) that logs the decoded error
  and returns instead of exiting. All Xlib inclusion is isolated in one TU so its
  macro soup never leaks; it is a no-op on macOS/Windows/WASM and on Linux builds
  where `find_package(X11)` fails (headless/Wayland-only).

### Added — LOGO listing injection in the DevBench (4th language)

- **The Bench now writes + runs LOGO turtle programs**, not just the interpreter's
  asm build. LOGO is the 4th *New*-dialog language (after asm / C / BASIC) with two
  interpreter targets calqued on the Applesoft GEN2/TMS path: **LOGO TMS9918**
  (`Codetank_GAME3.rom` lower bank, `$4000`, cold `4000R`, 8 KB Parmigiani dual-bank)
  and **LOGO GEN2 HGR** (`roms/logo-gen2.rom`, `$6000`, cold `6000R`, preset 2). Both
  cold-start the resident **APPLE-1 LOGO V2.6** interpreter and draw the turtle live.
- **`LogoProgramLoader` (`src/LogoProgramLoader.{h,cpp}`, pure C++/WASM-safe)** parses
  a listing into the interpreter's on-chip layout and `Pom1BenchHost::injectLogo`
  (mode 6) pokes it. Unlike Applesoft there is **no tokenised image**: a LOGO
  procedure is stored as **raw ASCII source** in `proc_table` (244-byte slots — name,
  params, `body_len`, CR-separated body), so the loader emits `proc_table` writes +
  `n_procs`, cold-starts to the `?` prompt, `writeMemoryBatch`-pokes the table while
  the CPU is parked, queues **only one entry line** and resumes the REPL. Feeding a
  single line dodges the `REPEAT` break-poll type-ahead drop — procedure bodies never
  travel the keyboard. **Verify** parses host-side without disturbing the machine.
- Syntax colouring `langLogo()` (`bench/BenchLang.cpp`), `.logo` extension routing,
  starters, hints/tooltips, and `bench_logo_inject_smoke` — pokes a nested-`REPEAT`
  rosette procedure, calls it by name and asserts the turtle lights both framebuffers
  (TMS VRAM pattern table + GEN2 HGR page-1 RAM), pinning `proc_table` `$E431`/`$B431`
  + `n_procs` `$0260`/`$02E3` against an interpreter relink. Frozen `roms/logo-gen2.rom`
  (byte-identical to a fresh `CODETANK_BUILD`+`LOGO_GEN2` build of `GEN2Logo.bin`).
- **10 machine-neutral `.logo` sketches** in `sketchs/logo/` (rosette, polygons,
  stars, recursive flower/tree, random rays/meadow — canonical LOGO-manual §11
  tutorials), the counterpart of `sketchs/basic_applesoft/`; each runs unchanged on
  both the TMS9918 and GEN2 turtle and was verified drawing through the real
  interpreter. Dialect: turns are `TR`/`TL` (not `RT`/`LT`), nested `REPEAT` one deep.
- **Interactive LOGO REPL** after Run: because the interpreter stays resident at its
  prompt, the Bench now shows a one-line input below the console (new portable seam
  `IBenchHost::replActive()`/`replSend()`, gated by `Pom1BenchHost::logoReplActive_`).
  Typed lines are fed to the live REPL over the keyboard FIFO **one at a time** (so a
  paste can't trip the `REPEAT` break-poll), with Up/Down command history, and echoed
  into the console as a record of what was sent. The turtle draws on the card window;
  the interpreter's own text (prompt, `OK`, `PRINT`, errors) stays on the Apple-1
  screen window. Drive the turtle, call or (re)define procedures without a re-cold-
  start. Flag cleared whenever a non-LOGO target reprograms the machine.

### Added — GEN2 video journal survives save-state / rewind (snapshot v5)

- **The GEN2 soft-switch video journal now enters the serialized snapshot.** The
  per-frame `(cycle, kind, value)` journal that drives the beam-raced renderer
  (mid-line PAGE2/TEXT/HIRES flips — DROL-class double-buffering, horizontal
  splits) was published into the live in-memory `EmulationSnapshot` but *dropped*
  from the save-state / rewind blob: the load path called
  `resetGen2VideoEventJournal()` and rebuilt from the bare end-of-frame latch, so
  a beam-split scene restored mid-frame lost its per-line flips until the program
  toggled a switch again. The `GEN2VID` section now serializes the **published
  journal + that frame's start state**; on load they are restored (after the
  recording half is cleared and rebased to the restored latch). Event `emuCycle`s
  are absolute and the renderer maps them modulo the frame, so they stay valid
  against the restored cycle counter. This is the first slice of the TODO's
  "journal enters the snapshot" (TMS9918 adoption + shared `BeamClock` replay
  still open).
- **Snapshot format bumped v4 → v5.** The new journal fields are gated on
  `r.version() >= 5`; pre-v5 snapshots read no journal (the section's length
  prefix realigns) and fall back to the prior end-of-frame-latch behaviour, so
  old save-states still load. A forged event count (> `kGen2MaxEventsPerFrame`)
  is rejected before allocation.
- Pinned by a new round-trip case in `snapshot_smoke` (journal two soft-switch
  flips, publish across a frame boundary, save/load, assert every event + the
  frame-start state survives).

### Added — HGR Sprite editor: monochrome ×2 colour + side-by-side B&W / colour view

- **Reliable single-colour ×2 sprites** (`hgrsprite::magnifyColor2x`, pure +
  pinned in `hgr_sprite_blit_smoke`). On real HGR a ×2-magnified sprite lights two
  adjacent dots per source pixel, which NTSC reads as solid **white**. To get
  colour, each source pixel is doubled into a 2-aligned **colour clock**: only one
  dot of the pair is lit (even → Violet/Blue, odd → Green/Orange) plus the byte's
  **palette high bit** (MSB) selecting the group — so the whole ×2 sprite reads as
  one chosen artifact colour instead of white. The editor is monochrome by design:
  the shape is drawn in black & white and the sprite takes a **single** colour.
  Pinned by a new smoke case that stamps the doubled bytes and reads every hue
  back through the decoder.
- **Sprite editor rework**: the pencil/fill now author a pure **B&W shape**; the
  palette became **"Sprite colour (×2)"** — one colour for the whole sprite,
  **active only in ×2** (two ×1 dots are white, so colour needs the doubled
  clock). `buildSpriteBytes` produces the mono ×1 bytes or the single-colour
  doubled ×2 bytes, used by Stamp and the previews.
- **Dual display**: the editor now shows the **black-&-white shape canvas** and a
  read-only **NTSC colour view** of the same sprite side by side (decoded through
  the real GEN2 pipeline), so the colour clocks and hue bascules are legible while
  editing. The live-page preview also composites the sprite's real bytes and
  decodes through NTSC (was a swatch overlay).

### Added — GEN2 HGR: OpenEmulator composite NTSC decode on the CPU (Phase 4)

- **`GraphicsCard::RenderMode::CompositeOECpu`** — a second HIRES colour
  pipeline beside the default MAME artifact-colour LUT (which stays the
  fast-path v1). Instead of the 128-entry table it builds the 14.318 MHz
  composite signal from the *same* doubled-word HGR bitstream
  (`buildHgrWordRow`), then runs OpenEmulator's NTSC demodulator on the CPU:
  a symmetric **17-tap FIR** (luma ≈ 2.0 MHz, chroma ≈ 0.6 MHz), synchronous
  sin/cos demodulation against the 4-phase colour subcarrier, and the
  OpenEmulator YUV→RGB matrix. The 560 demodulated sub-pixels are pair-averaged
  down to the shared **280×192** RGBA buffer (no 560-wide framebuffer, no DHGR —
  GEN2 has neither), so the texture seam and every downstream path (monitor
  tint, phosphor persistence, beam-race splits, fast-path diff) are unchanged
  and the toggle is free. Port of POM2's `Apple2Display::renderCompositeOeCpu()`;
  pure CPU (no GLSL) so it works identically on WASM and desktop.
- **Verified against the original OpenEmulator source** (not just POM2's copy):
  the YUV→RGB coefficients (`1.139883 / -0.394642 / -0.580622 / 2.032062`) are
  verbatim from libemulation `OpenGLCanvas.cpp`, and the kernels are the exact
  result of its `chebyshevWindow(17, 50) × lanczosWindow(17, …)` recipe with
  luma normalised to sum 1 and chroma normalised **×2** (the demod gain).
- **Grafts at `rasterizeHgrLine`**: the composite branch replaces only the
  "560 sub-pixels" stage; `forEachBeamSegment` / `renderInternalSegment` /
  TEXT / LORES / the legacy fast path are reused as-is (composite affects the
  HGR artifact-colour decode only). GEN2 has no DHGR, so the per-frame
  `signalPhaseOffset` is always 0 (phase = sample index mod 4).
- **UI**: a "NTSC render" combo in the GEN2 window's right-click popup —
  "NTSC MAME (actuel)" vs "Composite OpenEmulator CPU", applied each frame via
  `setRenderMode`. Pinned by **`gen2_composite_smoke`** (achromatic-bright
  white, chromatic `$55/$2A` violet, black-on-empty, output distinct from the
  LUT, repaint on mode toggle). The golden-image `gfx_regress_gen2_testcard`
  stays byte-identical (default LUT path untouched).

## [1.9.3] — 2026-07-01

### Fixed — 6502 software: Galaga title/help SAT rebuilt during active display

- `draw_title_sprites` / `draw_help_sprites`
  (`sketchs/tms9918/game_galaga/TMS_Galaga.asm`) rebuilt the Sprite Attribute
  Table (`$1B00`, ~20 bytes) during **active display with no `WAIT_VBLANK`
  gate** — and `title_wait_key` re-invoked `draw_title_sprites` on every
  keyboard poll — so the raster scanned the SAT mid-rewrite, tearing/garbling
  the title and help aliens on real TMS9918A silicon (matching Claudio
  Parmigiani's "Galaga still broken" report on his Replica-1). The in-game
  `render_sprites` path was already correctly VBlank-gated; the pad18 timing
  fix alone did not cover this. Both routines now open with `WAIT_VBLANK` +
  the pad18 cross-boundary cushion, mirroring `render_sprites`. Costs +16 B
  and still fits the `$4100-$61FF` CodeTank bank (170 B headroom); reship by
  rebuilding `Codetank_GAME1.rom` (needs the external `tetris_codetank.bin`
  lower-bank drop-in).

### Added — Renderer abstraction, macOS Metal backend & OS-native file dialogs

- **`PomRenderer` graphics-backend seam** (`src/PomRenderer.h` + `_GL.cpp` /
  `_Metal.mm` / `_Internal.h`): a single opaque renderer interface
  (`createTexture` / `updateTexture` / `destroyTexture` / `beginFrame` / `clear`
  / `renderDrawData` / `present` / `readBackbufferRGBA` / ImGui backend init).
  Every texture site in the codebase — Screen_ImGui glyph atlas + native-res
  framebuffer, GEN2 HGR / TMS9918 / GT-6144 framebuffers, the 10 Help→Photos
  textures incl. the interactive keyboard, and the HGR/TMS Paint editor canvases
  — now routes through it; no `gl*` calls survive outside `PomRenderer_GL.cpp`.
  Selected at configure time via the `POM1_RENDERER` cache option. Delivered as
  Phase 1 (abstraction) → Phase 3 (HGR/TMS editors adopt the seam). This also
  fulfils the **Shared video texture layer** item.
- **macOS Metal backend** (`PomRenderer_Metal.mm`): `CAMetalLayer` on GLFW's
  NSWindow, BGRA8 drawables (`framebufferOnly = NO` so the screenshot blit reads
  them), `@autoreleasepool` per frame. **Default renderer on macOS-non-WASM**;
  OpenGL stays the default on Linux/Windows/WASM and remains available on macOS
  via `-DPOM1_RENDERER=opengl`. The upstream `imgui_impl_metal` inline sampler is
  patched linear→nearest at configure time so pixel-art framebuffers/glyphs/
  canvases stay crisp. Fulfils the **ImGui Metal backend on macOS** item.
- **OS-native file dialogs** (`src/NativeFileDialog.{h,cpp}` + `_Mac.mm`): Load/
  Save Memory, Tape, and Snapshot (plus the cassette deck buttons and the HGR/
  TMS Paint editors + DevBench) open the platform's localised file picker —
  Win32 `GetOpenFileNameW`/`GetSaveFileNameW`, Cocoa `NSOpen/NSSavePanel`, Linux
  probing `$PATH` for `zenity` then `kdialog`. WASM and Linux-without-either fall
  back to the existing in-app ImGui browser; the portable editor seams keep a
  `pickFilePath(...)` that defaults to `false` so the modules stay standalone.
  Fulfils the **Native file dialog** item.

### Added — TMS9918 sub-scanline beam/CPU synchronisation

- **renderUpToBeam + write catch-up**: `renderActiveLine` is split into
  `renderLineToTemp` + `commitActiveSegment`, and the live raster commits only
  the horizontal slice the beam has crossed. The `Memory` MMIO **write** hook
  calls `TMS9918::renderBeamCatchUp(inFlight)` before each register/VRAM
  mutation, with the in-flight offset = `cpu->getCurrentInstructionCycles()`
  (sub-instruction accuracy, the same idiom as the GEN2 video-event journal). A
  mid-scanline `R7`/`R5`/`R6`/`R4`/VRAM change now splits the line at the exact
  pixel (horizontal rainbow, mid-line table swap); a static frame still commits
  each line in one slice, so the golden image is byte-preserved. Pinned by
  `tms9918_per_scanline` Phase G.
- **5S/collision read-time sync**: `syncSpriteScanToBeam(inFlight)` advances the
  per-line sprite scan to the beam's scanline before a `$CC01` read returns, so
  the 5S overflow / collision / index reflect the beam at the **read cycle** —
  the 5S raster-split poll loop is now cycle-precise. Pinned by Phase H.
- **Seamless mode/blank split**: the display mode (R0 M3, R1 M1/M2) and the blank
  bit (R1.6) are latched at the **start** of each render line (`lineLatchR0/R1`)
  while table bases + R7 stay live, so a mid-line mode/blank write defers to the
  next line (Grauw/ARTRAG "seamless" splits). Pinned by Phase I.
- **Shared `BeamClock` seam** (`src/BeamClock.h`, `pom1::beamPosAt`): factors the
  cycle→(line,x) raster mapping the catch-up paths compute inline — the shared
  "BeamClock + renderUntil(beam)" foundation for the TMS9918 (now) and the GEN2
  beam engine (to adopt). Pinned by `beam_clock_smoke`. (The decoupled
  journal/replay renderer + GEN2 adoption remain open in `TODO.md`.)

### Fixed — TMS9918 status-read semantics + active-display access floor

- **Status read clears F + C only, not 5S** (`TMS9918::readControl`, `~0xE0` →
  `~0xA0`): per BiFi/Sean Young §2.2 a `$CC01` read latch-clears only the frame
  flag (bit 7) and collision (bit 5); the 5th-sprite flag (bit 6) and its SAT
  index are re-derived by the sprite scan each frame (per-frame reset at the top
  of the active scan), **not** cleared on read. The old mask wiped 5S, so a
  second in-frame status read wrongly reported "no overflow". Pinned by
  `tms9918_sprite_status` T9. Docs §13/§18/§24/§26 + `TMS9918-SPRITE_INIT` /
  `-SPRITE_BEST_PRACTICES` made consistent; §30 Bug N°4 (overscan collision)
  reclassified 🔑 OPEN (Test E, silicon-unconfirmed) and the contradictory
  inline "overscan [-32,288)" comments corrected to visible-only [0,256).
- **Active-display CPU-access floor = 9c = ⌈8 µs⌉** (`kMinActiveDrainCycles`
  16c → 9c, Gfx12-scoped): the TI datasheet's ~8 µs between data-port writes in
  Graphics I/II is `⌈8 µs × 1.022727 MHz⌉` = 9 cycles, so an 8c gap drops and 9c
  lands. The retired flat 16c floor (≈2× spec) wrongly dropped the sprites-OFF
  `TMS_Plasma` timings that run clean on Parmigiani's Replica-1.
  `tms9918_silicon_strict_runtime` re-anchored.

### Fixed — headless keystroke injection (`--paste-at-cycle`) + TMS9918 silicon A/B validation

- **`--paste-at-cycle` never reached the CPU.** `queueKeystrokes` → `queueKey`
  enqueued the key, but the headless deterministic run path (`runCyclesSync`)
  pauses the async emulation thread — the only thing that drained the keyboard
  queue into Memory (`$D010`). So cycle-scheduled keys sat in the queue forever;
  the CPU never saw them, and the earlier "noise ON vs OFF → identical hash"
  check was trivially true (both runs stalled on the title screen). Fixed with
  `EmulationController::deliverQueuedKeys()` (drains the queue into Memory under
  `stateMutex`), called after each injection in `runCyclesWithTimedPastes`. Inject
  one key per `--paste-at-cycle` for programs that read several prompts in turn
  (each read needs its own `$D010` strobe at its own cycle).
- **A/B validation of the TMS9918 silicon-fidelity fixes.** With the tool
  actually working, Snake (`--run 7600`, keys `1`+`1`), Sokoban (`--run 6200`,
  key `1`) and Galaga (`--run 4100`, keys `1`+space) now drive headless past their
  keyboard-gated title screens into the sprite-bearing playfield. On the gameplay
  frame the render is **pixel-identical with VRAM power-on noise OFF vs ON**
  (`--vram-noise`) — the defensive SAT fill neutralises the ghost sprites the raw
  DRAM noise would otherwise surface. Galaga is additionally **poison-invariant**
  across `--ram-poison 00/FF/AA`, confirming the `anim_tick` (`$3F`) uninitialised-
  RAM read is fixed. This is the empirical close-out the fidelity work was after.

### Added — 6502 software: standalone `$0280` TMS9918 programs rebuilt (pad18)

- The shipped standalone `software/Graphic TMS9918/*.txt` (Woz-hex, load/run at
  `$0280`) had gone stale + orphaned: they carried the pre-pad18 timing and their
  per-program build recipes were deleted in the 686fe03 refactor. Re-established a
  committable, reproducible driver — `sketchs/tms9918/build_standalone_txt.py`
  (goes through `dev/cc65/emit_woz.py`, auto-links `tms9918_pad.asm`) — plus the
  restored `$0280` linker cfgs next to each source. Rebuilt 8 programs against the
  current pad18 libs: Snake, Sokoban, Mandel, Plasma, Life, Vague, Split,
  Logo_16k. Snake + Sokoban verified byte-behavior-identical to their CodeTank ROM
  builds (same gameplay-frame hash); the demos render correctly. The driver is
  idempotent (re-run → no diff). Five artefacts have no in-tree source any more
  (Maze3D, OrbitalPool, SilBench, Stars, Nyan_Fantasy — sources removed in
  686fe03, never migrated to `sketchs/`) and are left untouched; Galaga stays
  CodeTank-only (`$4100`, outgrew the `$0280` window).

### Fixed — 6502 software: defensive SAT fill in Snake + Sokoban

- **`TMS_Snake.asm` / `TMS_Sokoban.asm`** wrote only `SAT[0].Y=$D0` and omitted
  the `SAT[1..127]=$D1` fill — the confirmed cause-#1 of ghost sprites on real
  TMS9918A (renders fine on POM1's bistable power-on VRAM, breaks on silicon).
  Added the inline `$D0 + 127×$D1` fill (gold standard, `TMS9918-SPRITE_INIT.md`
  §4.2), and rebuilt `roms/codetank/Codetank_GAME1.rom`: the Tetris lower bank
  (by **Nippur72 / Antonino Porcino**) is preserved byte-identical, Snake is
  2476/2560 B and Sokoban 5077/5120 B, and the title screens are byte-identical
  old/new under `--vram-noise` (the fill is defensive — neutral outside
  gameplay). **CodeTank cartridge audit**: all 9 games now carry the fill —
  Galaga/Life/Snake/Sokoban inline, Rogue/Mandel/Plasma/Logo/Nyan via the lib
  `disable_sprites` helpers (`tms9918m1.asm` Mode I @ `$1B00` / `tms9918m2.asm`
  Mode II @ `$3B00`). `SPRITE_INIT` §11 corrected: failures **diverge by
  default** (POM1 hides the SAT-init class unless VRAM noise is armed), not
  "shared silicon↔POM1".

### Fixed — 6502 software: GEN2 LOGO `BIRDFLY` sprite flicker

- **GEN2 HGR LOGO (`sketchs/tms9918/tool_logo/TMS_Logo_16k.asm`, build
  `software/Graphic HGR/GEN2Logo.txt`)**: the bird sprite in the `DEMO`
  slideshow's final `BIRDFLY` scene flickered. The cap-only turtle commands
  (`SETH` / `TR` / `TL`) ran the full XOR `erase_turtle` → `draw_turtle` cycle
  even for a `SETSHAPE` **emote**, which is non-directional — turning changes
  none of its pixels (`gen2_draw_emote` ignores the heading, and `tx/ty` are
  unchanged). On a single live HGR page the transient "erased" window between
  the erase and the redraw is what the async beam-race renderer caught as a
  blink (each `BFR`/`BFL` flap does a `TR 12` between the two `FD 3` steps).
  A new `turn_erase` / `turn_draw` seam skips the erase+redraw when
  `sprite_mode ≠ 0` (emote → visual no-op, leave it drawn) and tail-calls the
  real routines when `sprite_mode = 0` (the triangle turtle genuinely rotates),
  removing ⅓ of the per-flap flicker windows (the two pure turns) while leaving
  the real motion (`FD`, `SETSHAPE`) untouched. The "emote + invisible + turn"
  state is unreachable (no `HIDETURTLE`; every `turtle_visible = 0` is either in
  triangle mode or immediately redrawn), so the change is behaviourally
  transparent apart from the fix. The **TMS9918 / CodeTank GAME3 LOGO** build is
  **byte-for-byte unchanged**: it has no flicker to fix (HW sprites + VBlank
  sync), so `turn_erase`/`turn_draw` alias straight through to the originals and
  emit no extra code (verified by identical linked-binary SHA).

### Added — HGR Paint Editor window

- **`HGR Paint Editor`**: an Apple II hi-res paint window for the GEN2 card.
  Draws **live** into the HGR framebuffer (`$2000` page 1 / `$4000` page 2) so
  strokes appear on the GEN2 screen in real time, and renders its canvas through
  the GEN2 NTSC artifact-colour pipeline so it is pixel-identical to the
  emulator's output. Tools: pencil, eraser, line, rectangle, ellipse, flood fill,
  eyedropper, rectangular select/clipboard, palette-shift, plus page select,
  brush size, zoom, grid, seam overlay, minimap, NTSC/mono toggle, undo/redo,
  clear, and load/save of 8 KB `.HGR` images + PNG export. **Faithful HGR colour
  model**: the six artifact colours obey column parity and the per-byte shared
  high bit. Pinned by `hgr_paint_plot_smoke`. Independent reimplementation
  inspired by fadden's HGRTool (concept only, Apache-2.0).
  - **Portable module** (`hgrpaint/`, at the repo root alongside `bench/`): the
    editor + pure model now depend only on ImGui/GL and a small
    `hgrpaint::IHgrPaintHost` seam (poke / render-page / file I/O), mirroring
    `bench/IBenchHost`. POM1 supplies `src/Pom1HgrPaintHost` (GraphicsCard +
    `EmulationController` + stb_image_write); **POM2 can reuse `hgrpaint/`
    verbatim** with its own host. The pure model carries its own Apple II row
    interleave + geometry constants (cross-checked against `GraphicsCard` in the
    test) so it pulls in no emulator headers.
  - **Fill rewritten to flood by *perceived* artifact colour** (renders the page
    through the host NTSC pipeline) instead of raw pixel on/off. An HGR colour
    field is bit-dithered (solid violet is the byte pattern `$55`, odd columns
    *off*), so the old raw-bit flood leaked through every chromatic region via
    those off sub-pixels — filling the background flooded ~the whole canvas.
    Recolour now clears the region first so an old colour's bits can't OR with
    the new ones into white (`$2A | $55 = $7F`). Pinned by new
    `hgr_paint_plot_smoke` cases (no-leak + clean recolour).
  - **UI redesigned MacPaint-style**: a left vertical palette of **FontAwesome
    icon tool buttons** (`IconsFontAwesome6`, same dependency as `bench/`), the
    **colour palette along the bottom**, the **navigator thumbnail in the left
    panel** (below the edit buttons, no longer overlaying the canvas), and a slim
    top strip (page · file · help). The drawing canvas is now an `InvisibleButton`
    (not an `Image`) so **dragging on it paints instead of moving the window** —
    the window only moves from its title bar (was unusable: any stroke dragged
    the whole window).
  - **Ergonomics**: right-drag quick-erase (no tool switch), middle-drag pan,
    `Shift` constrains Line to 0/45/90° and Rect/Ellipse to a square, zoom-to-fit
    on first open, and a `(?)` controls cheat-sheet.
  - **File picker** (portable `std::filesystem`): Load / Save / Save PNG / Import
    now open a modal browser instead of needing a typed path — it lists every file
    with its byte size and highlights the relevant ones (8 KB raw HGR pages for
    Load — they have no standard extension, e.g. `sdcard/NONO/HGR/PIC#062000` —
    or images for Import).
  - **Import PNG/JPG → HGR (ii-pix-grade)**, entirely in `hgrpaint/`
    (`Cam16.{h,cpp}`, `HgrConvert.{h,cpp}`, `HgrImageDecode.cpp`): decode (stb) →
    fit/letterbox resample → **analysis-by-synthesis** dithering. For each byte it
    tries all 256 (7 pixels + palette) patterns, renders each through the module's
    own copy of the NTSC pipeline (**byte-identical to GraphicsCard**, pinned in
    the test), scores it in **CAM16-UCS** perceptual space (chroma-weighted so flat
    greys dither clean black/white instead of magenta confetti), and keeps the
    best with Floyd-Steinberg error diffusion. Beats Buckshot/bmp2dhr by dithering
    against the *true* artifact colours incl. the sliding-window coupling. ~30 ms
    per image. Pinned by `hgr_convert_smoke` (CAM16 sanity, decode == GraphicsCard,
    black→empty, in-gamut reproduction, ramp tone conservation).
  - **Interactive import preview**: picking an image opens a modal with the
    **source and the HGR result side by side**, live-reconverting as you drag
    **Colour noise** (the CAM16 chroma weight — left = clean black/white greys,
    right = vivid colour), **Brightness**, **Contrast** and **Gamma**, plus
    **Diffusion (grain)** — the Floyd-Steinberg strength (1 = full dithering,
    lower = smoother/flatter) — plus **Serpentine** dithering (alternates the FS
    scan direction per row to kill diagonal smear) / Dither / Stretch toggles and
    a Reset. Apply commits it as one undoable stroke; Cancel discards.

### Added — native compiler: `SIN`/`SQR`/`INT`, peephole optimizer, `3DHat.apf` runs native

- **Transcendentals in the float runtime** (`dev/lib/basicrt/basicrt_float.s`):
  `fp_int` (truncate toward zero), `fp_sqrt` (Newton–Raphson, 5 iterations) and
  `fp_sin` (2π range reduction → fold to [-π/2,π/2] → 4-term Taylor). Each is
  **feature-gated** (`-D FP_INT`/`FP_SQRT`/`FP_SIN`) so it links only when the
  program calls it. The compiler compiles `SIN`/`SQR`/`INT` to these, and
  auto-precision forces the float phase when `SIN`/`SQR` appear. Logical `AND`/`OR`
  on floats fixed (had fallen through to the comparison path). Pinned by
  `basic_float_runtime` (now 5736 cases vs host `sinf`/`sqrtf`).
- **`3DHat.apf` compiles and runs native on GEN2 and TMS9918** — the MTU/Micro
  May-1981 hidden-line 3-D HAT (HGR2, nested `FOR`, `IF/GOTO`, `GOSUB/RETURN`,
  `INT`/`SQR`/`SIN`, decimal literals, `HCOLOR=0` column erase) draws the sombrero
  with proper hidden-line removal, standalone, **no ROM**. This meets the project
  goal. Pinned by `basic_native_run` (native-only 3DHat case).
- **`HCOLOR=0` erase** in the GEN2 runtime: `rt_plot` is pen-aware (`AND ~mask` to
  clear vs `OR mask` to set), so the hidden-line "plot point then erase column
  below" trick works; `rt_hgr` seeds a non-zero default pen.
- **Peephole optimizer** (`Codegen::optimizePeephole`): fuses the codegen's
  "define a temp, then copy it elsewhere" chains by retargeting the store straight
  into the destination (temp vanishes; self-copies dropped). Intra-block liveness;
  runtime `jsr fp_*`/`rt_*` transparent. Trims ~640 B — enough that the full 3DHat
  fits GEN2's `$0300–$1FFF` window (`basicc_native.cfg` moves BSS to `$0200` to give
  code the whole window up to the framebuffer at `$2000`).
- **Benchmarks** (`basic_native_bench`): a size+speed-by-program-type table —
  int-arith **21×**, int-raster **14×**, lines **2.4×**, float-arith **2.0×**,
  transcend **1.4×** vs the interpreter; binary 354 B–7157 B with dead-stripping
  (4 runtime routines for integer programs, 13 for 3DHat).

### Added — native compiler: auto-precision, dead-stripping (minimal size), clear diagnostics

- **Auto precision** (`FpMode::Auto`, the `basicc --native` default): the compiler
  picks the **smallest sufficient** numeric type — 16-bit integer unless a line
  needs a fraction (a decimal literal or a `/`), then binary32. A program that
  uses no floats **never links the float runtime**. `--int`/`--float` force a tier.
- **Minimal code size (dead-stripping):** the compiler emits only the runtime
  symbols it uses, and the runtime (`basicrt_*.s`) gates each routine on a `-D
  RT_xxx` flag the build derives from those imports — unused routines and the
  560-byte hi-res pixel tables never reach the binary. Measured GEN2 sizes:
  `X=5+2:X=X+1` **1695 → 89 B**, `PRINT`+`FOR` **1746 → 265 B**, `HGR:HPLOT` 1686
  → 1165 B. Pinned by `basic_native_run` (size assertion) + `basic_native_codegen`.
- **Clear, line-precise diagnostics** for authoring new programs: every error
  names the exact Applesoft line (`line 20: FOR expects a variable`), `GOTO`/
  `GOSUB`/`THEN` targets are validated at compile time (`GOTO 99: no such line
  number`), float literals are rejected in the integer phase, and `NEXT` without
  `FOR` is caught.

### Added — native compiler Phase 2b: float codegen (compile + run a float program, no ROM)

- **The native compiler now emits floating-point code** (`basicnative::compile(…,
  floatMode=true)`, `basicc --native --float`, `basicc_native.sh --float`):
  binary32 variables/temps, `+ - * /` and comparisons via the `fp_*` runtime,
  float `FOR/NEXT`/`IF`, and `HPLOT`/`HCOLOR` coords converted with `fp_toint16`.
  A float program (parabola) compiles to a standalone binary that runs **with no
  interpreter and no ROM float**, drawing the same picture.
- **`basic_native_run`** now pins **both** phases. Measured (native vs same source
  on the interpreter, identical output): integer compute loop **~22×** (16.8M vs
  368M), integer line-draw **~4.5×**, **float parabola ~2.0×** (2.8M vs 5.6M). The
  ~2× float ceiling is honest — binary32 work isn't cheaper than the ROM's float,
  so the gain there is only from removing interpreter overhead; control/integer
  code wins an order of magnitude more. `basic_native_codegen` adds float pins.
- **Remaining for native `3DHat.apf`:** `SIN/COS/SQR/INT` on the proven `fp_*`
  core + `FOR`-index type inference. See [`doc/BASIC_COMPILER.md`](doc/BASIC_COMPILER.md).

### Added — native compiler Phase 2a: standalone binary32 software-float runtime

- **`dev/lib/basicrt/basicrt_float.s`** — the autonomous floating-point core (no
  Applesoft ROM) for the native compiler's FP phase. 4-byte IEEE-754 single
  storage; `fp_fromint16/fp_toint16/fp_add/fp_sub/fp_mul/fp_div/fp_cmp` over the
  zero-page slots `FA`/`FB`, computing on an unpacked `{sign, E, 24-bit SG}` form.
  Pinned by **`basic_float_runtime`** (cc65-gated), which assembles the runtime and
  checks every op against the host IEEE `float` over a value grid **and 4000
  randomised pairs spanning 2^±20** — all exact within float tolerance. Phase 2b
  (compiler type-system integration + `SIN/SQR` → compile `3DHat.apf` to native)
  is the documented next step. See [`doc/BASIC_COMPILER.md`](doc/BASIC_COMPILER.md).

### Added — native BASIC compiler: standalone 6502 machine code (~20× faster, no interpreter)

- **`src/BasicCompilerApplesoft.{h,cpp}` + `dev/lib/basicrt/` runtime + `basicc
  --native` + `tools/basicc_native.sh`.** A **real** native-code compiler (not a
  tokenizer): recursive-descent / precedence-climbing parser → standalone ca65
  assembly with native control flow (`GOTO`→`JMP`, `GOSUB`/`RETURN`→`JSR`/`RTS`,
  `FOR`/`NEXT`/`IF` as native branches, line numbers → labels), 16-bit variables
  at fixed addresses (no name lookup), and **constant-multiply strength reduction**
  (`X*3` → shifts+adds, not a 16-iteration multiply). The output binary runs with
  **no Applesoft interpreter** — only the graphics card — via a tiny per-card
  runtime (`rt_*`) wrapping the project's graphics asm (GEN2 `plot_pixel`/
  `clear_hgr`; TMS `plot_set`/`line_xy`) + shared 16-bit math. Loads + runs at
  `$0300`; both GEN2 and TMS9918.
- **Measured (POM1 core, identical output):** ~**4.5×** on pixel-plot-bound code,
  **~20×** on arithmetic/control-bound code (e.g. 19.2 M vs 368 M cycles). Pinned
  by `basic_native_run` (cc65-gated: builds native + interpreter, asserts same
  framebuffer **and** native faster) and `basic_native_codegen` (pure asm-text
  unit pin: strength reduction, FOR/NEXT, GOTO/GOSUB, graphics ABI).
- Integer phase (16-bit signed: `+ - * /`, comparisons, `AND/OR/NOT`, `ABS`,
  `FOR/NEXT`, `IF/THEN`, `GOTO`, `GOSUB/RETURN`, `PRINT`, `HGR/HCOLOR/HPLOT`).
  **Phase-1 polish:** variables/temporaries moved to **zero page** (~20→25× on the
  arith benchmark), **full 16-bit X** (GEN2 hi-res 0..279, verified exact), `PRINT`
  of strings + signed integers via the WOZ terminal, and a clean TMS link. A
  standalone floating-point runtime (so `3DHat.apf` compiles to native code with
  no ROM) is the documented next phase. See [`doc/BASIC_COMPILER.md`](doc/BASIC_COMPILER.md).

### Added — Applesoft "BASIC compiler": compile an `.apf` to a 6502 image (no injection)

- **`src/BasicTokeniserApplesoft.{h,cpp}` + `basicc` tool + `doc/BASIC_COMPILER.md`.**
  Compiles an Applesoft Lite listing (GEN2 or TMS9918 dialect) **ahead of time**
  into a 6502 memory image — a tokenized program at `$0801` (Applesoft's own
  on-disk layout, byte-for-byte what `PARSE` builds) plus a 14-byte launcher at
  `$0280` (`install VARTAB; JSR SETPTRS; JMP NEWSTT`). The resident interpreter
  ROM supplies every runtime (FP, `SIN/SQR/INT`, `FOR/GOSUB`, `HGR/HPLOT`), so
  the program **loads and runs directly** instead of having its listing typed in
  one keystroke at a time. Pure C++ (no GL/ImGui) → links into the bench
  (desktop + WASM), the CLI and the tests; wired into the app `SOURCES` and a
  standalone `basicc` host tool (`--target {gen2|tms}` → Wozmon-hex image).
- **Pinned by `basic_compiler_smoke`** (ctest): the floating-point
  `sketchs/basic_applesoft/3DHat.apf` 3-D hat compiles and **executes on both the
  GEN2 HGR card and the TMS9918 card**, drawing into each framebuffer — verified
  injection-free (cold-start the ROM, poke the image, jump to the launcher). The
  test re-pins the two interpreter entry points (`SETPTRS`/`NEWSTT`), so a ROM
  rebuild that shifts them fails loudly. A second pure unit test
  (`basic_compiler_tokenize`) pins the exact tokenized bytes (links, `REM`/`DATA`/
  string/`?`→`PRINT`, ascending-line sort, launcher stub) against the Applesoft
  on-disk layout, independent of any ROM.

### Added — packaging: release builds bundle the cc65 toolchain (asm + C)

- **Every release package now ships cc65 next to POM1** so the DevBench
  (`Pom1BenchHost`) compiles **both** of its native languages out of the box,
  with no system cc65 on PATH — asm (`ca65`/`ld65`) **and** C (`cl65`/`cc65`)
  plus the `share/cc65` runtime (resolved via `CC65_HOME`, see `ensureCc65Home`).
  The exe-relative probe (`<exe>/cc65/bin`, macOS `Resources/cc65/bin`, AppImage
  `share/POM1/cc65/bin`) already existed; this wires the *producers*.
- **`tools/verify_cc65_bundle.sh`** (new) — asserts a staged tree carries
  ca65+ld65+cl65+cc65 + `share/cc65/{include,lib,target}`. The three packagers
  (`packaging/linux/build_appimage.sh`, `package_macos_release.sh`,
  `package_windows_release.bat`) call it through a **`POM1_REQUIRE_CC65=1`**
  strict gate so a missing/partial bundle is a hard failure, not a silent
  Woz-hex-only package.
- **`packaging/windows/fetch_cc65.ps1`** (new) — pure-PowerShell fetch of the
  official cc65 Windows snapshot, re-homed to the relocatable `cc65/bin` +
  `cc65/share/cc65` layout (`POM1_CC65_WIN_URL` overrides the URL). The Windows
  packager auto-runs it when no bundle is staged.
- **`.github/workflows/release.yml`** (new) — one native job per OS (cc65 can't
  be cross-built): Linux AppImage, macOS `.dmg`, Windows ZIP, each built with
  cc65 bundled + verified, uploaded as an artifact and attached to the GitHub
  Release on a `v*` tag. `package_macos_release.sh` / `package_windows_release.bat`
  now honor `POM1_VERSION` (tag → artifact name) with the shipped default kept.

### Added — emulator (`src/`): BASIC language axis in the Bench

- **Bench "BASIC" language — Run by injection** (`Pom1BenchHost.cpp`,
  `Pom1BenchHost.h`) — a third Bench language beside asm and C that compiles
  nothing: it cold-starts the in-ROM interpreter through the WOZ Monitor and
  TYPES the listing at the prompt over the Apple-1 keyboard FIFO (`$D010`), then
  `RUN`. New source mode 4 + `injectBasic()`; `build()` dispatches mode 4 before
  the cc65 split, so the path is byte-identical on desktop and the web (WASM)
  build — **no compiler in the loop, so both BASICs run in-browser**. The
  keyboard FIFO self-paces on the program's reads (same pipeline as Ctrl-V
  paste, 4096-char budget).
- **Two BASIC targets (machine + interpreter)** — **Integer BASIC** (Apple-1
  CC65 DevBench machine, preset 0, ROM at `$E000`, cold start `E000R`) and
  **Applesoft Lite** (P-LAB **microSD + Applesoft Lite** machine, preset 8, ROM at
  `$6000`, cold start `6000R`). The microSD preset owns the `$6000` Applesoft ROM +
  the `$8000` SD-OS, but is 8 KB with silicon/OOR-strict armed, so `$6000-$7FFF`
  (inside the `$1000..$7FFF` out-of-range window) reads back `$FF` and a bare
  `6000R` jumped into `$FF` garbage and fell back to the WOZ Monitor (which then
  parsed each program line as a hex address). `injectBasic` therefore **relaxes
  the microSD preset to a permissive 64 KB view for the Applesoft run**
  (`presetRamKB=64` → `isOorAddress` always false → `$6000` and Applesoft's RAM
  workspace live; the microSD card stays plugged, only OOR enforcement is lifted).
  Integer BASIC `$E000` is OOR-exempt (≥ `$8000`) so it needs no relaxation and
  stays on the authentic 8 KB DevBench. `hardReset` reloads Integer automatically;
  `injectBasic` re-loads the Applesoft `$6000` ROM (zeroed by the reset) and
  surfaces a reload failure instead of
  letting the cold-start crash silently. The New-sketch dialog gains a "BASIC"
  language whose Target combo is now **per-language**: picking BASIC offers just
  the two interpreters — "Integer BASIC ($E000)" and "Applesoft Lite ($6000,
  P-LAB microSD)" — as dedicated machine entries, while asm/C still show the three
  graphics machines (`CodeBench` filters the combo by `targetFor()` and snaps the
  selection when the language changes).
- **Built-in BASIC examples + starters** — "Hello (Integer BASIC)" and
  "Hello (Applesoft Lite)" in the Bench *Examples* popup, plus the per-target
  HELLO-WORLD starters used by *New*.
- **Pinned by `bench_basic_inject_smoke`** (`tests/bench_basic_inject_smoke_test.cpp`)
  — boots WOZ headlessly, injects `E000R`/`6000R` + a listing + `RUN` for both
  interpreters and asserts the program's *computed* PRINT result reaches the
  `$D012` display (e.g. `1000+7 → 1007`, `100/8 → 12.5`), proving the
  interpreter executed the injected program rather than echoing the source. A
  third block pins the OOR root cause: `$6000` reads back `$FF` under
  `presetRamKB=8` + strict but the real ROM byte under 64 KB Fantasy — so nobody
  re-points the Applesoft target at an 8 KB / strict machine.

### Added — Bench: four Applesoft BASIC machines + BASIC editor without gutter

- **`New` → BASIC now offers four Applesoft machines** (`Pom1BenchHost.cpp`
  `kP1Machines`/`targetFor`/`injectBasic`): **Applesoft Lite (Apple-1)** =
  `roms/applesoft-lite-cffa1.rom` @ `$E000` (`E000R`, 64 KB-relaxed);
  **Applesoft Lite + microSD** = `applesoft-lite-microsd.rom` @ `$6000`;
  **Applesoft GEN2 HGR** = `sketchs/gen2/applesoft_gen2` @ `$6000` (preset 2);
  **Applesoft TMS9918** = `sketchs/tms9918/applesoft_tms9918`, flashed as a
  CodeTank ROM cartridge @ `$4000` (`4000R`, preset 1). `injectBasic` dispatches
  the ROM load per variant (reloadApplesoftLite{CFFA1,SDCard} / loadInterpreterRom
  for GEN2 / CodeTank flash for TMS9918). `.bas`/`.apf` files route to the GEN2 or
  TMS9918 variant by path. (Integer BASIC drops out of the New grid but is still
  reachable via `.ibas`.) The committed `applesoft-tms9918.bin` ships under
  `software/Apple-1_TMS_CC65/`.
- **BASIC editor hides the gutter line numbers** (`TextEditor` gains
  `SetShowLineNumbers`; CodeBench disables it for BASIC docs) — a BASIC program's
  own line numbers (10, 20, …) are what matter, so the editor gutter is just noise.
- **`applesoft_gen2_smoke` extended** to also cold-start and run the **TMS9918**
  (`4000R`) and **CFFA1** (`E000R`) interpreter cores (`APRINT`/`PRINT 1000+7` →
  `1007`), proving all four renumbered interpreters execute. 35/35 ctest pass.

### Fixed — Applesoft TMS9918: sprite garbage + clipped HPLOT

- **`HGR`/`GR` now park the sprites** (`sketchs/tms9918/applesoft_tms9918/tmsgfx.inc`)
  — the Sprite Attribute Table sat uninitialised (Graphics II SAT `$3B00`, and the
  Multicolor `mc_regs` even pointed it at `$0000` over the framebuffer), so 32
  garbage sprites floated over the bitmap. Both setups now write `$D0` to the
  SAT's first sprite Y (terminates the sprite scan → all hidden); Multicolor's SAT
  moved to `$0B00`.
- **`HPLOT` clamps x to the 256-wide screen** — x came straight from the low byte
  of the 16-bit coordinate, so a GEN2-style `HPLOT … TO 279,191` wrapped to x=23
  and drew a narrow line. `clampx` now pins x≥256 to 255, so the same listing
  draws a full-width line. Pinned by the new `tms9918-hgr` check in
  `applesoft_gen2_smoke` (asserts SAT `$3B00`==`$D0` and the line reaches the
  right-edge cells, via the real VDP).

### Added — Bench: file-type routing, tab-aware mode, markdown hyperlinks

- **The file extension drives the action, re-evaluated on every tab switch**
  (`Pom1BenchHost::targetForPath`, `bench/CodeBench.cpp`): `.s`/`.asm` → assemble,
  `.c` → compile, `.hex`/`.txt` → Woz-hex, `.bas`/`.apf` → inject Applesoft,
  `.ibas` → inject Integer, `.md` → document, **anything else → do nothing**
  (Verify/Run report "nothing to build" instead of silently building a non-source
  file as asm). Switching tabs now refreshes the host's active-source context and
  re-derives the mode from the front tab's extension, so the status bar + toolbar
  always match the tab you're looking at. `targetIndex == -1` is a first-class
  "no build target" state (markdown / unknown), guarded in the status bar.
- **`.apf`/`.bas` BASIC injection is GEN2-aware** — a BASIC file in a GEN2/HGR
  path injects into **Applesoft GEN2** (new target: the `applesoft_gen2`
  interpreter loaded at `$6000` on the GEN2 card, preset 2) so a turtle/graphics
  listing runs with the GEN2 commands; elsewhere it uses the stock microSD
  Applesoft. New `EmulationController::loadInterpreterRom` drops the sketch-built
  interpreter into RAM without resetting the running WOZ Monitor; `injectBasic`
  loads `software/Graphic HGR/applesoft-gen2.bin` for the GEN2 target.
- **Markdown links are clickable** (`bench/Markdown.cpp` — `RenderMarkdown` now
  returns the clicked URL): `[text](other.md)` resolved relative to the document
  opens the target in a new tab when it exists; `http(s)://` links copy to the
  clipboard. Covered by the expanded `applesoft_gen2_smoke` (PRINT→GEN2 /
  APRINT→Apple-1, HGR/HGR2/lo-res, HOME/HTAB/VTAB, SCRN, and end-to-end injection
  of the shipped `Tortue.apf` lo-res drawing).

### Added — 6502 software (`sketchs/`): Applesoft Lite interpreter sketch

- **`sketchs/apple1/applesoft_lite/`** — the full **Applesoft Lite** (Microsoft
  6502 BASIC, floating-point) interpreter source from `txgx42/applesoft-lite`,
  packaged as a DevBench sketch so it assembles in the Bench (Verify) like any
  other Apple-1 ASM sketch. Sources verbatim (`applesoft-lite.s`, `io.s`,
  `cffa1.s`, `wozmon.s`, `macros.s`, `zeropage.s`); the only edit is one
  `.feature force_range` line so modern ca65 (≥ 2.18) accepts the 2008 source's
  negative immediates / `<label-1` precedence. `.sketch.json` drives the build
  (`cfg` + `extraAsm`), and `applesoft_lite.cfg` links the canonical
  `$E000-$FFFF` 8 KB ROM image (BASIC `$E000-$FEFF` + Woz Monitor `$FF00`).
  **The DevBench build is byte-identical to the shipped
  `roms/applesoft-lite-cffa1.rom`** — i.e. this sketch is the editable source of
  the same interpreter that backs the Bench "Applesoft Lite" BASIC runtime
  (relocated to `$6000` as `roms/applesoft-lite-microsd.rom`). Verify/compile is
  preset-neutral; faithful run needs `$E000-$FEFF` backed (the CFFA1 flavour this
  build targets).

### Added — 6502 software (`sketchs/`): Applesoft GEN2 (the BASIC for the GEN2 card)

- **`sketchs/gen2/applesoft_gen2/`** — Applesoft Lite turned into the BASIC for
  Uncle Bernie's GEN2 colour card: CFFA1 disk I/O removed, a full Apple II-style
  graphics + console command set added, and **`PRINT` retargeted to the GEN2
  screen**. New statements: `TEXT GR GR2 HGR HGR2 MIX NOMIX SHOW VBL COLOR=
  HCOLOR= PLOT HLIN..AT VLIN..AT HPLOT..TO HOME HTAB VTAB APRINT` plus the
  `SCRN(x,y)` function. The three freed CFFA tokens become TEXT/GR/HGR and the
  rest are inserted as new statement tokens ($A2-$B1), renumbering every operator
  + function token; dispatch is robust to this (`MATHTBL` off `TOKEN_PLUS`,
  `UNFNC` off `TOKEN_SGN`, positional tokenizer). Handlers + tables in
  `gen2gfx.inc`: lo-res on the `$0400` page (Apple II interleave), hi-res on
  `$2000`/`$4000` via a ÷7 byte/bit calc + the `dev/lib/gen2` scanline tables + a
  16-bit Bresenham for `HPLOT TO`.
- **Output model — `PRINT` → GEN2, `APRINT` → Apple-1.** An Apple II-style `CSW`
  char-out vector (`io.s` `OUTDO` does `JMP (CSW)`): it defaults to the Apple-1
  WOZ ECHO `$FFEF` (so prompt/`LIST`/errors/`INPUT` echo stay on the terminal),
  the `PRINT` wrapper points it at a GEN2 text console (`GCOUT`: cursor, CR, wrap,
  scroll on the `$0400` page) for its output, and `APRINT` forces it back.
  `HOME`/`HTAB`/`VTAB` drive the GEN2 cursor.
- **Pages + sync.** `HGR2`/`GR2` draw on page 2 (`$4000`/`$0800`); `SHOW n`
  displays page n and routes drawing to the hidden page (tear-free double
  buffering); `VBL` is a coarse vertical-blank wait. COLDSTART pins HIMEM at
  `$2000` so BASIC storage (`$0800-$1FFF`) can't grow into the HGR framebuffer.
  Builds to `software/Graphic HGR/applesoft-gen2.bin` (~9.6 KB at `$6000`, run
  `6000R` on preset 2).
- **Pinned by `applesoft_gen2_smoke`** (`tests/applesoft_gen2_smoke_test.cpp`) —
  boots the ROM headlessly and asserts: the renumbered core runs and `APRINT`
  reaches `$D012` (`1000+7 → 1007`); `PRINT` writes screen codes to `$0400`;
  `HGR`/`HPLOT` fill `$2000`; `GR`/`COLOR=`/`PLOT`/`HLIN`/`VLIN` fill `$0400`;
  `HGR2` fills page 2 `$4000`; `HOME`/`VTAB`/`HTAB` place a glyph; `SCRN(5,5)`
  reads back the plotted colour.

### Added / Fixed — Bench: editor syntax highlighting

- **`langBasic()` syntax definition** (`bench/BenchLang.cpp`) for the BASIC editor
  targets — union of Integer BASIC + Applesoft keywords (statements, numeric +
  string `$` functions, word operators), `REM` line comments, Applesoft float /
  scientific numbers, and `$`/`%` variable suffixes, case-insensitive. Wired via
  `langDef("BASIC")`, so picking either BASIC target colours the listing.
- **Highlighting accuracy pass** (multi-agent audit) — `langBasic`: removed three
  non-existent keywords (`ELSE`, `MOD`, `SQRT` — Apple BASIC has no `IF/THEN/ELSE`,
  no `MOD`, and uses `SQR`), added the Applesoft slot-I/O `PR#`/`IN#` tokens, and
  swapped the `/* */` block-comment sentinels for an un-typeable `\x01` marker so a
  literal `A/*B` can't start a phantom block comment. `lang6502`: added ca65
  character literals (`'A'`) and cheap-local labels (`@name`) as proper tokens.
- **`REM` comment false-positive fixed** (`TextEditor.cpp` tokenizer) — the shared
  single-line-comment matcher is now word-aware and case-insensitive-aware: an
  alphabetic marker like `REM` only starts a comment at word boundaries (so a
  variable `REMARK`/`REMOTE` no longer blanks the rest of the line) and matches
  case-insensitively when the language is case-insensitive (lowercase `rem`). No-op
  for `;` (6502) and `//` (C), which legitimately appear mid-token.
- **C block-comment / string desync fixed** (`TextEditor.cpp` Colorize pass) — a
  `"` INSIDE a `/* */` block comment (e.g. the `the "\"` prompt in the
  `GEN2Countdown.c` header) used to open string mode; the `withinString` branch
  never scans for `*/`, so the comment "never closed" and the opened string
  swallowed every line down to the next `"` (`#include "gen2.h"`), wrecking the
  colouring of the whole file. The sole `withinString = true` site is now guarded
  with `&& !inComment`, so a quote inside a block comment stays comment text and
  `*/` end-detection still runs. No-op for normal strings/char-literals/line
  continuations and for asm/BASIC.
- **Bench-specific `langC()`** (`bench/BenchLang.cpp`) — `langDef("C")` now copies
  the upstream C definition (keeping its custom tokenizer + libc built-ins) and
  adds ~115 real POM1 cc65 library entry points (`woz_puts`/`woz_mon`,
  `gen2_hgr_*`, `apple1_getkey`/`apple1_input_line`, `tms_*`/`screen1_*`,
  `gfx_*`, `telemetry_*`) as KnownIdentifiers plus the cc65 qualifiers
  (`__fastcall__`, `__A__`/`__X__`/`__Y__`, `__asm__`, …) as keywords — names
  sourced verbatim from `dev/lib` headers — so library calls in sketches stand out.

### Added — Bench: editor right-click context menu

- **Right-click context menu in the DevBench code editor** (`bench/CodeBench.cpp`)
  — Cut / Copy / Paste / Delete / Select All / Undo / Redo, each enabled by the
  live editor state (selection, read-only, clipboard, undo/redo depth). Extension
  point for later actions (comment block, go-to-error, snippet insert). The
  editor's built-in right-click-on-selection quick-copy is now gated by a new
  `TextEditor::SetHandleRightClickCopy(bool)` (ImGuiColorTextEdit) and disabled by
  the Bench so the right button cleanly owns the menu.

### Added — Bench: multi-file tabs + Markdown preview/edit

- **Multi-document tabs** (`bench/CodeBench.{h,cpp}`) — the DevBench editor went
  from one buffer to a set of open documents rendered in a real tab bar. Each tab
  is an independent `Doc` (its own `TextEditor`, path, target, dirty flag, error
  markers, syntax). New / Open / Examples open in a tab (Open focuses the tab if
  the file is already open); a trailing **`+`** button and per-tab close box (with
  an unsaved-dot) manage the set; closing the last tab respawns a fresh sketch.
  Build / Run / status / toolchain reflect the **active** tab. Reference-stable via
  `vector<unique_ptr<Doc>>` so opening a file mid-frame can't dangle the active doc.
- **Markdown presentation + editing** (`bench/Markdown.{h,cpp}`, new `RenderMarkdown`)
  — opening a `.md`/`.markdown` file gives a **Preview / Edit** toggle: Preview is a
  lightweight rendered view (ATX headings sized via ImGui 1.92 `PushFont(NULL,size)`,
  **bold**/*italic*/`code`/[links], fenced code blocks as read-only selectable
  boxes, bullet/ordered lists, blockquotes, horizontal rules); Edit drops back to
  the text editor. Links copy their URL to the clipboard on click. Verify/Run are
  no-ops on a Markdown doc ("nothing to build"). `Markdown.cpp` added to the bench
  sources (desktop + WASM).

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

### Changed — emulator (`src/`): GEN2 silicon params follow the SILICON/FANTASY buttons

- **GEN2 HGR silicon-fidelity knobs are now armed/disarmed by the silicon
  profile toggle** (`MainWindow_Menu.cpp` toolbar ruler/horse button,
  `MainWindow_HardwareWindows.cpp` master `SILICON STRICT`/`MULTIPLEXING FANTASY`
  button, `MainWindow_Presets.cpp` preset apply). All four GEN2 power-on knobs
  (`setGen2RandomPowerOn` → random latch / floating-bus noise / vertical scanner
  phase / framebuffer DRAM noise) now flip with the master profile, and the four
  individual Silicon Strict Inspector checkboxes track it. Previously only the
  preset path touched them; the toggle buttons left GEN2 out of sync. The
  headless path stays deterministic.

### Fixed — emulator (`src/`): bug-hunt sweep (continued — passes 3-4)

Further adversarial bug-hunt passes over real-time audio, host I/O error
handling, the DRAM-refresh CLI feature, and snapshot/reset robustness of the
storage cards. Built clean; all 32 `ctest` pass (2026-06-21).

- **Tape export checks write errors** (`CassetteDevice.cpp` `saveAciTape` /
  `saveWavTape`) — both now `flush()` + verify stream state after writing, so a
  full/failing disk reports an error instead of silently leaving a truncated
  `.aci`/`.wav` reported as success.
- **`--dram-refresh` / `--no-dram-refresh` now honoured in GUI mode**
  (`main_imgui.cpp`, `MainWindow_ImGui.{h,cpp}`) — the override was applied only
  on the headless path; the GUI launch dropped it so the preset default always
  won. Added `setDramRefreshOverride()` + an apply branch mirroring
  `--silicon-strict`.
- **Cassette ramp-in counter is now atomic** (`CassetteDevice.{h,cpp}`) —
  `audioRampInSamplesRemaining` was reachable from the realtime audio-callback
  thread and main-thread resets under two different mutexes (a data race). Made
  `std::atomic<uint32_t>`; `clearLiveAudioState()` no longer needs a mode-specific
  lock (which would have dead-locked callers already holding `audioStreamMutex`).
- **microSD write-finish reports I/O errors** (`MicroSD.cpp` `cmdWriteFinish()`) —
  a failed/truncated host write now returns `I/O ERROR` to the guest instead of
  acknowledging the SAVE as OK.
- **DRAM-refresh stall counter cleared on reset** (`M6502.cpp` `hardReset()`) —
  `hardReset()` now calls `resetDramRefreshStallCount()` so the inspector's
  "stall cycles since reset" readout matches its label after a reset/preset switch.
- **microSD MCU FSM phases validated on snapshot load** (`MicroSD.cpp`
  `deserialize()`) — `mcuPhase`/`nextPhaseAfterResponse` are clamped to `IDLE`
  if out of range; a forged snapshot could otherwise wedge the MCU (the
  command-byte switch has no `default`). Mirrors the IEC/Drive1541/CFFA1 clamps.
- **IEC daughterboard FSM reset with the microSD VIA** (`Memory.cpp`
  `resetMemory`/`initMemory`) — `iecCard->busReset()` now runs alongside
  `microSD->reset()`, so an F5 hard reset mid-transfer no longer leaves the IEC
  serial-bus FSM desynced from the freshly-cleared VIA it rides on.
- **1541 error-channel read state reset on snapshot load** (`Drive1541.cpp`
  `deserialize()`) — the in-flight channel-15 `errBuffer_`/`errCursor_`/`errBuilt_`
  (not serialized) are now reset on load so the next read re-derives a clean
  stream from the restored `errCode_` instead of duplicating/restarting bytes.
- **`--dram-refresh` documented** (`doc/CLI.md`) — added the flag row next to
  `--silicon-strict` (it was missing from the canonical CLI reference).

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
