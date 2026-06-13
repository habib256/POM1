# TODO

Open work on the **emulator** only. Shipped features → `git log` / `README.md`. 6502 software work → `dev/TODO6502.md`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Sections ordered by actionability: implementable first, externally-blocked last.

---

## ⏪ State rewind (microM8-style timeline)

> **MVP shipped** (`RewindBuffer` + in-memory `SnapshotWriter`/`Reader` + **CPU → State Rewind…** scrub panel). Delta-encoded ring of snapshot blobs (section + 256 B chunk deltas, keyframe-anchored segments, budget eviction). Pinned by `rewind_buffer_smoke`. See CLAUDE.md › Emulation orchestration.

- [ ] **Continuous-scrub feel + keyboard shortcut** `[S · nice]` — hold-to-rewind that loads frames smoothly and resumes on release; bind a key (none assigned yet — slider-only for the MVP).
- [ ] **On-disk persistence of the ring** `[M · nice]` — serialize the segment list so a session's history survives a save/restore; today the buffer is in-RAM only and cleared on disable.
- [ ] **VRAM dirty-tracking for finer TMS9918 deltas** `[M · nice]` — the 16 KB VRAM section is chunk-diffed against the previous full blob each capture; a live VRAM dirty bitmap would cut the per-capture diff cost on graphics-heavy frames.
- [ ] **Seek cost on card-heavy presets** `[S · nice]` — `rewindSeekTo` reuses `loadSnapshotFromBuffer`, whose FLAGS dispatch re-applies card setters (may reload ROMs) every slider tick. Skip re-apply when the flag set is unchanged to keep dragging smooth.

---

## 🖥️ Uncle Bernie GEN2 — moteur faisceau (back-port POM2 → POM1)

> **Contexte** (AppleFritter, juin 2026) : la carte GEN2 *release* déplace les soft switches graphiques en **`$C250-$C257`** (`$C0` → `$C2` dans le code Apple II porté). Le vaporlock classique (`$C050` + bit-fumes bus flottant) **ne marche pas** avec l'ACI présent ; Bernie expose à la place un **drapeau MSB** sur lecture des soft switches `$C25x` pour détecter H-blank / V-blank. POM1 v1.8.6 ne modélise que le framebuffer passif `$2000-$3FFF` + rasterisation fin-de-frame MAME (`GraphicsCard.cpp`) — pas de faisceau cycle-accurate, pas de MMIO `$C2xx`.
>
> **Source POM2** (référence à jour juin 2026) : `Memory::floatingBus()` (port MAME `apple2video.cpp:124-201`), journal `VideoEvent{emuCycle, scanline, kind, value}` (`beginVideoEventFrame` / `takeVideoEvents`), décomposition beam **`forEachBeamSegment`** + `frameCycleToPos(emuCycle) → {scanline, byteCol}` (fenêtre visible : 40 octets, HBL cycles 0–24 → `byteCol=0`), rendu RGBA via `renderBeamRacing` → `renderInternalSegment` (splits **horizontaux mid-scanline** depuis 2026-06-09), signal composite 14,318 MHz via `fillCompositeSignal` (même décomposition que RGBA) + `ColorCompositeOECpu` / shader `NtscPostProcessor`. Réf. `POM2/DEV.md` § Beam-racing, `POM2/TODO.md` § Display. Tests : `floatingbus_page2_smoke`, `beam_race_composite`, `horizontal_split`, `horizontal_split_composite`, `horizontal_split_560` (560-wide IIe — hors scope POM1).
>
> **Tag global** `[L · critical]` — prérequis pour que Bernie et les ports de jeux Apple II HGR s'appuient sur la carte release, pas sur le prototype fil à fil.
>
> **✅ Phases 0-3 + 5 livrées** — soft switches `$C25x` read-only + HST0, journal d'événements, rendu beam-raced TEXT/LORES/HIRES/MIXED/PAGE2 + splits verticaux/horizontaux mid-scanline, preset 13, démo `dev/projects/a1_crazycycle/`, Hardware Reference. **Détail → [`CHANGELOG.md`](CHANGELOG.md).** Épinglé par `gen2_floatingbus_smoke`, `gen2_softswitch_msb_smoke`, `gen2_beam_race_smoke`, `gen2_horizontal_split_smoke`. Reste : **Phase 4** (composite OpenEmulator, optionnelle, non bloquante) + finitions optionnelles ci-dessous.

### Phase 4 — Composite OpenEmulator (option rendu, pas bloquant MMIO)

> POM2 : `fillCompositeSignal` partage **`forEachBeamSegment`** avec le path RGBA depuis 2026-06-09 — splits horizontaux visibles en OE GPU/CPU + AppleWin, pas seulement LUT. Limites POM2 à ne pas recopier aveuglément : `signalPhaseOffset_` = une constante par frame ; lo-res clip au block-row (4 scanlines). GEN2 POM1 : pas de DHGR ni 560-wide → points 1 et scope 560 hors scope.

- [ ] **Signal 14,318 MHz `signalBuf`** `[M · nice]` — port `Apple2Display::fillCompositeSignal()` : 560 échantillons × 192 lignes ; `paintHgr` construit la ligne entière puis écrit `[col0·14, col1·14)` (réutiliser `buildHgrWordRow` / bitstream POM1). Même `forEachBeamSegment` que Phase 3 — zéro divergence RGBA/signal.
- [ ] **Chemin CPU `ColorCompositeOECpu`** `[M · nice]` — port `renderCompositeOeCpu()` + démodulateur FIR OpenEmulator (sans GLSL) pour WASM / fallback desktop. Menu GEN2 : « NTSC MAME (actuel) » vs « Composite OpenEmulator CPU » — calque le choix POM2 Graphics. **Non bloquant** : le LUT MAME actuel (`GraphicsCard`) reste le fast-path v1.
- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 si le shared texture layer (section Visuals) est en place ; sinon reporter.
- [ ] **Test parité MAME vs OE CPU** `[S · nice]` — même framebuffer ± quelques pixels de phase ; pas de régression sur `hgr_testcard`. Après splits horizontaux : port `horizontal_split_composite` (signal waveform gauche HGR / droite TEXT).

### Finitions optionnelles (post-Phase 5)

> Phases 0-3 + 5 livrées → **`CHANGELOG.md`** (preset 13, soft switches `$C25x` + HST0, beam-racing, Hardware Reference, démo `a1_crazycycle`, MàJ CLAUDE.md). Restes ouverts, tous optionnels :

- [ ] **Trace debug `--gen2-beam-log`** `[S · nice]` — trace événements / cycle blank, si le besoin émerge pendant la validation Bernie.
- [ ] **Portage pilote d'un jeu Apple II HGR** `[M · nice]` — Taipan / Breakout sur la carte release, pour exercer le moteur beam sur du vrai logiciel de jeu.
- [ ] **README : mot sur les soft switches `$C25x`** `[S · nice]` — la table presets est à jour ; ajouter une ligne sur les soft switches GEN2 au prochain passage README.

### Dépendances / hors scope immédiat

- Vaporlock Apple II pur (`$C050` + bit-fumes sans MSB Bernie) — **hors scope** ; Bernie confirme non fonctionnel avec ACI.
- DHGR, 80 colonnes, AN3, Le Chat Mauve, path 560-wide (`horizontal_split_560`, save/restore `frame80`) — Apple IIe uniquement, pas la GEN2 Apple-1.
- Timing PAL 50 Hz — fait POM2 (profils `iie-pal` / `iic-pal`) ; GEN2 Apple-1 = **NTSC couleur seul**, mais le jumper vertical 50/60 Hz de la carte (262/312 lignes, Q4) est exposé (checkbox fenêtre HGR + `setGen2FiftyHz`).
- Speaker `$C030` — déjà dans la plage ACI `$C000-$C0FF` ; documenter pour les ports, pas de nouveau hardware.

---

## 🔌 Peripherals

- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — https://github.com/flowenol/apple1-serial — serial-port bootloader / terminal (complements TurboType / 8BitFlux). Pipes through Terminal Card or its own ACIA variant; likely a text-format loader on top of `Memory::loadHexDump` + paste pipeline.
- [ ] **ACI header + checksum on the jaquette** `[S · nice]` — `tapeinfo.txt` already drives the *"Type 0280.0FFFR"* label. Parse the raw `.aci` pulse-capture header (from / to / checksum) in `CassetteDevice::loadAciTape()` and surface for tapes without a sidecar entry.
- [ ] **Briel Multi I/O — SpeakJet** `[M · nice]` — 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as separate optional peripheral so it coexists with microSD.

---

## 📥 Loaders

- [ ] **TurboType 57 600-baud loader** `[M · solid]` — Uncle Bernie's format, shipped by 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244). Protocol: Wozmon-speed bootstrap (200 ms/newline, 20 ms/char) installs an in-RAM dropper that **skips `$D012` echoes** and streams bytes at 57.6 kbps with running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in <30 s vs ~2 400 baud Wozmon. POM1 side: parse `.TUR`/`.APL`, switch Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon.

---

## 🎮 Automated game testing — telemetry side channel

> **Contexte** (Uncle Bernie, juin 2026) : le chaînon manquant de son « SDK rêvé » (cc65/ca65 → émulateur → tests de régression auto). Pour tester des **jeux d'action temps réel**, il faut un *side channel* où le jeu 6502 expose son état et où un harnais externe lit cet état, décide les entrées (clavier/joystick) et les renvoie. C'est la généralisation binaire/bidirectionnelle du pont déjà fait par la Terminal Card (`$D012 → TCP → inject $D010`).
>
> **Note de conception : [`doc/TELEMETRY_SIDE_CHANNEL.md`](doc/TELEMETRY_SIDE_CHANNEL.md).** **✅ Fonctionnellement complet → [`CHANGELOG.md`](CHANGELOG.md)** : `TelemetryPort` (`$C440-$C443`, FIFO in/out, serveur TCP, `KeyInjector`), bus priorité 30 (zone A9=0 aveugle à GEN2), `--telemetry-port N`, **lock-step déterministe** (parke le CPU via `M6502::stop()` jusqu'à l'ACK `0x06`, vérifié end-to-end + `tools/test_telemetry_lockstep.py`), `--telemetry-log PATH` (golden-trace), panneau UI (*View → Telemetry Side Channel…*). Reste seulement le prérequis CI ci-dessous (non spécifique télémétrie).

- [ ] **Mode headless pour CI** `[M · nice]` — *prérequis hors-périmètre, non spécifique télémétrie* : pour des tests sur chaque commit, POM1 reste une app GLFW/ImGui. Soit un drapeau offscreen réutilisant la boucle `pumpEmulationMainThread` (style WASM) sans fenêtre, soit un display virtuel. Le side channel (et son lock-step / golden-trace) marche déjà en mode fenêtré. Lié à *CI GitHub Actions* (section Technical debt).

---

## 🎨 Visuals & UX

> POM1 a déjà la meilleure UX du duo POM1/POM2 (126 tooltips, 15 tutoriels, boot scénographié, 0 ROM à fournir). Frictions résiduelles *(audit designer 2026-05-31)* :

- [ ] **Sous-menus Hardware par famille** `[S · solid]` *(audit 2026-05-31)* — le menu `Hardware` (15+ cartes) est une liste fleuve intimidante pour le novice. Regrouper par époque/usage (1976 · stockage · son · graphismes · réseau) dans `MainWindow_Menu.cpp`.
- [ ] **Overlay « premier lancement » sur le WOZ Monitor** `[S · solid]` *(audit 2026-05-31)* — l'invite `\` nue reste la porte d'entrée brute. Afficher un hint au-dessus de l'écran (« Tapez une commande, ou choisissez un preset → ») au premier boot, dismissable.
- [ ] **Galerie de presets avec vignettes** `[M · nice]` *(audit 2026-05-31)* — les 15 presets sont des libellés texte ; des miniatures faciliteraient le choix (sélecteur visuel plutôt que menu).
- [ ] **Native file dialog** `[M · solid]` — drop in `nfd` (NativeFileDialog) or `tinyfiledialogs` — header-light, MIT, cross-platform. The in-app browser stays in the way.
- [ ] **HiDPI font scaling on Linux** `[S · nice]` — auto-detect monitor DPI on first window creation (`glfwGetMonitorContentScale`, GLFW 3.3+) and scale the default font; keep a Hardware → Display setting to override. Currently users must tweak `ImGui::GetIO().FontGlobalScale` manually.
- [ ] **1976 CRT fidelity (opt-in, default off)** `[M · nice]` — two sub-effects under the existing CRT toggle:
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing) — chars land ~60 / s, hardware scroll shifts buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock) — periodic static, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` after backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.
- [ ] **Shared video texture layer** `[M · solid]` — `Screen_ImGui.cpp` and `MainWindow_HardwareWindows.cpp` still own raw OpenGL texture lifecycles directly (glyph atlas, GEN2 HGR, TMS9918, GT-6144). Factor a backend-neutral texture helper for create/update/destroy + filter mode (`GL_NEAREST` pixel cards, `GL_LINEAR` glyph atlas) so the future Metal backend ports one abstraction.

---

## 🔧 Technical debt

> Items marqués *(audit 2026-05-31)* issus de l'audit 3 angles (ingénieur · designer · commercial).

- [ ] **CI GitHub Actions** `[S · solid]` *(audit 2026-05-31)* — `.github/workflows/` n'existe pas ; les 24 tests `ctest` (Klaus Dormann inclus) ne tournent jamais automatiquement. Workflow `ctest` sur push/PR (Linux, idéalement + macOS) + build de vérification WASM (`emcmake`). **Plus fort ROI du dépôt.**
- [ ] **Test cycle-exact par instruction** `[M · solid]` *(audit 2026-05-31)* — Klaus valide le fonctionnel, pas les comptes de cycles. Les bugs JSR/RTS de `AUDIT_POM1.md` (déjà corrigés dans `M6502.cpp`) seraient passés inaperçus sans relecture manuelle. Ajouter un oracle de cycles par opcode (POM2 a `cpu_cycle_count_test`, pas POM1) + couvrir IRQ/NMI/BRK timing.
- [ ] **Durcissement désérialisation** `[S · nice]` *(audit 2026-05-31)* — 2 trouvailles « faibles » de `AUDIT_POM1.md` encore ouvertes : borner `fifoLevel` dans `PR40Printer::deserialize` (`PR40Printer.cpp:136`, sinon lecture OOB sur snapshot corrompu) ; tester `n > INT_MAX` avant le cast dans `parseIntPositive` (`CliDispatcher.cpp:85-94`). Robustesse sur entrées non fiables, pas de risque en usage normal.
- [ ] **Snapshot residual gaps** `[M · nice]` — base format + 12-card per-card payloads + CPU section landed (May 2026). Remaining work: cassette mid-stream playback position (re-load tape file by path on snapshot-load + seek to saved `playbackIndex`); WiFiModem / TerminalCard graceful "drop and reconnect" on load (currently kept disconnected); libresidfp internal filter integrators / oscillator phase (engine doesn't expose them — would need an upstream patch); SHA-256 footer (mentioned in `SnapshotIO.h` as v2 sweetener).
- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) reading line-delimited commands while the emulator runs — same verbs as CLI flags, but for stateful sequences. Telnet on `:6502` carries keystrokes + display; this channel carries control without polluting the keyboard stream. Depends on CLI-verb + snapshot work above.
- [ ] **Terminal Card — `Ctrl-K` hand-over** `[S · nice]` — match the 8BitFlux toggle: a `Ctrl-K` byte suspends `$D010`/`$D011` injection until `Ctrl-T` re-attaches. Useful once a script bootstrapped a program and the user wants to play without dropping the session. Hook: `injectionSuspended` next to `escapePending` / `eightBitMode` in `TerminalCard.cpp`.
- [ ] **External `presets.json`** `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` to JSON under `doc/` (or next to the executable) so users add presets without recompiling. Loader in `MainWindow_Presets.cpp`, keep the C++ table as fallback.
- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL deprecated since 10.14 (silenced via `GL_SILENCE_DEPRECATION`). `imgui_impl_metal.mm` + `imgui_impl_osx.mm` are drop-in; scope is porting raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 + HGR + GT-6144 textures) to MTLTexture.

---

## ⏸️ Deferred / conditional

Spec public, code change tractable, but gated on a real-world trigger (software exercising the feature, user demand, hardware availability). Promote when the trigger appears.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk` / `.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.

---

## 🚫 Blocked on external

- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **PROM unpublished** (Bernie's physical kits, sold out; Applefritter says it will never be released). Contact [Applefritter PM](https://www.applefritter.com/user/254186/track) — he shared docs with the HoneyCrisp dev. Once obtained: load at `$C500`, extend `CassetteDevice` for header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
