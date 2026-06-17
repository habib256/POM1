# TODO

Open work on the **emulator** only. Shipped work → [`CHANGELOG.md`](CHANGELOG.md) / `git log` · user tour → `README.md` · 6502 software → [`dev/TODO6502.md`](dev/TODO6502.md).

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Grouped by subsystem; deferred / externally-blocked last.

---

## 🎨 Graphics

### GEN2 beam engine — Phase 4: composite OpenEmulator (rendu optionnel, non bloquant)

> Phases 0-3 + 5 livrées (soft switches `$C25x` + HST0, beam-racing TEXT/LORES/HIRES/MIXED/PAGE2, splits verticaux/horizontaux, preset 13, démo `a1_crazycycle`, Hardware Reference) → [`CHANGELOG.md`](CHANGELOG.md). Reste la voie **composite**, optionnelle : le LUT MAME actuel (`GraphicsCard`) demeure le fast-path v1. POM2 partage `forEachBeamSegment` entre RGBA et signal depuis 2026-06-09 ; ne pas recopier `signalPhaseOffset_` (constante/frame) ni le clip lo-res au block-row. Pas de DHGR ni 560-wide côté GEN2 Apple-1.

- [ ] **Signal 14,318 MHz `signalBuf`** `[M · nice]` — port `Apple2Display::fillCompositeSignal()` : 560 échantillons × 192 lignes ; `paintHgr` construit la ligne entière puis écrit `[col0·14, col1·14)` (réutiliser `buildHgrWordRow` / bitstream POM1). Même `forEachBeamSegment` que Phase 3 — zéro divergence RGBA/signal.
- [ ] **Chemin CPU `ColorCompositeOECpu`** `[M · nice]` — port `renderCompositeOeCpu()` + démodulateur FIR OpenEmulator (sans GLSL) pour WASM / fallback desktop. Menu GEN2 : « NTSC MAME (actuel) » vs « Composite OpenEmulator CPU » — calque le choix POM2 Graphics. **Non bloquant**.
- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 si le *Shared video texture layer* (→ Visuals & UX) est en place ; sinon reporter.
- [ ] **Portage pilote d'un jeu Apple II HGR** `[M · nice]` — Taipan / Breakout sur la carte release, pour exercer le moteur beam sur du vrai logiciel de jeu.

### Factorisation des libs graphiques GEN2 ↔ TMS9918

- [ ] **Intégration build de la couche `dev/lib/gfx/`** `[S · solid]` — l'axe 1 pose une couche partagée additive (géométrie ligne/cercle/rect/ellipse + conversion entier→ASCII, backend résolu au lien) à compiler puis brancher aux chemins de build cc65, dont la ligne cl65 du Bench (`src/Pom1BenchHost.cpp`). Travail 6502/cc65 + axes 2-3 (fonte partagée, façade) → [`dev/TODO6502.md`](dev/TODO6502.md) § *Factoriser les libs graphiques GEN2 HGR ↔ TMS9918*.

---

## 🛠️ Dev tooling

### POM1 Bench

> **Phases A-E livrées** (2026-06-14/16) → [`CHANGELOG.md`](CHANGELOG.md) : éditeur 6502/cc65 (ImGuiColorTextEdit), Verify / Build & Run via `ca65`/`ld65`/`cl65`, **Serial Monitor = télémétrie** (`$C440-$C443`), habillage Arduino, **cibles « machine + build »** (`kBenchTargets[]`, dialogue New 2 langages × 4 machines), bundle cc65 relocatable + sonde exe-relative (`Pom1BenchHost::probe`). **Desktop-only** pour le chemin asm/C. Reste ouvert :

- [ ] **Publier la release 1.9.2 tri-plateforme** `[M]` — la dernière release GitHub est **1.9.0** (1.9.1/1.9.2 absents) alors que les chaînes de version disent 1.9.2. Construire AppImage + .dmg + ZIP **avec cc65 embarqué**, produire + tester les bundles macOS/Windows (binaires cc65 par plateforme), attacher à un tag `v1.9.2`.
- [~] **WASM cc65 — arbitrer le port emscripten** `[L · nice]` — **Faisabilité prouvée + fondation construite (2026-06-17).** `tools/build_cc65_wasm.sh` compile la chaîne cc65 en WASM via `emcc` : `ca65` (209 ko wasm), `ld65` (142 ko), `cc65` (419 ko), `ar65` (38 ko) — modules MODULARIZE avec MEMFS + `callMain` + `FS`. `tools/cc65_wasm.js` orchestre le pipeline (instancie un outil, peuple MEMFS, `callMain`, relit le `.o`/`.bin` ; porte les `.o` d'un outil à l'autre). **Vérifié byte-identique au cc65 natif** sur le chemin **asm** (un vrai programme Apple-1 `ca65 -I … | ld65 -C apple1_4k.cfg` produit un `.bin` octet-pour-octet égal au build natif) ; `cc65` (C) tourne aussi en WASM (sortie ≠ natif seulement par la **version** 2.19 git vs 2.18 brew — renommage zp `sp`→`c_sp`, pas un défaut WASM). **Reste (câblage navigateur, documenté dans `doc/CC65_WASM.md`)** : bundler `build-wasm/cc65/*.{js,wasm}` + le sous-ensemble `dev/` cfg/includes à côté de `POM1.html` (CMake `if(EMSCRIPTEN)` + `.gitignore`) ; charger la glue dans `shell.html` ; le **seam C++** (`Pom1BenchHost.cpp` sous `#if POM1_IS_WASM` : exposer les cibles asm/C au lieu du seul Woz-hex, `compile()` appelle la glue via `EM_ASM`/`EM_JS` async → `.bin` → RAM, `headerNote()` sans le CTA) ; les **libs runtime C** version-2.19 pour le chemin C (pin `--rev`) ; et la **vérif navigateur** (rebuild emcc + ouvrir `POM1.html`) — la partie non testable headless cette session. → **le port est faisable et amorcé**, plus besoin d'« assumer le CTA » comme seule réponse.

### BASIC dans le Bench

> **Aucune compilation** : on **tokenise / injecte** un listing dans les interpréteurs déjà embarqués — **Integer BASIC** (`$E000`, cold-start `E000R`) et **Applesoft Lite** (`$6000`, preset microSD, `6000R`). Natif, **zéro dépendance externe**, et **marche aussi en WASM** (contrairement au chemin cc65). Nouvel axe langage « BASIC » dans le Bench (3ᵉ à côté d'asm/C).

- [ ] **Axe langage « BASIC » + Run par injection** `[M · solid]` — nouveau langage dans la matrice Bench (`kBenchTargets[]`, `src/Pom1BenchHost.cpp`) dont l'**Upload** ne compile pas mais **alimente l'interpréteur** : la cible choisit l'interpréteur + son preset (auto-plug), fait le cold-start (`E000R` / `6000R`), puis envoie le listing. **MVP = streaming clavier** : réutilise le pipeline paste→clavier existant (`KeyboardController`, cap 4096) pour « taper » le programme ligne à ligne au prompt, puis `RUN`. Marche en WASM.
- [ ] **Cibles BASIC (machine + interpréteur)** `[S · solid]` — au moins deux : **Integer BASIC** (ROM `$E000`, présente dans la plupart des presets) et **Applesoft Lite** (preset microSD, ROM `$6000`). Sélectionner la cible plugge la bonne machine, exactement comme les cibles asm/C actuelles.
- [ ] **Exemple BASIC intégré** `[S · nice]` — un sketch « Hello BASIC » (`10 PRINT "HELLO, APPLE-1"` / `20 GOTO 10`) dans le menu *Examples* du Bench, cible Integer BASIC par défaut. Point d'entrée immédiat.

---

## 🔌 Peripherals & loaders

- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — <https://github.com/flowenol/apple1-serial> — serial-port bootloader / terminal (complements TurboType / 8BitFlux). Pipes through Terminal Card or its own ACIA variant; likely a text-format loader on top of `Memory::loadHexDump` + paste pipeline.
- [ ] **TurboType 57 600-baud loader** `[M · solid]` — Uncle Bernie's format, shipped by 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244). Protocol: Wozmon-speed bootstrap (200 ms/newline, 20 ms/char) installs an in-RAM dropper that **skips `$D012` echoes** and streams bytes at 57.6 kbps with running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in <30 s vs ~2 400 baud Wozmon. POM1 side: parse `.TUR`/`.APL`, switch Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon.
- [ ] **ACI header + checksum on the jaquette** `[S · nice]` — `tapeinfo.txt` already drives the *"Type 0280.0FFFR"* label. Parse the raw `.aci` pulse-capture header (from / to / checksum) in `CassetteDevice::loadAciTape()` and surface for tapes without a sidecar entry.
- [ ] **Briel Multi I/O — SpeakJet** `[M · nice]` — 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as a separate optional peripheral so it coexists with microSD.
- [ ] **Terminal Card — `Ctrl-K` hand-over** `[S · nice]` — match the 8BitFlux toggle: a `Ctrl-K` byte suspends `$D010`/`$D011` injection until `Ctrl-T` re-attaches. Useful once a script bootstrapped a program and the user wants to play without dropping the session. Hook: `injectionSuspended` next to `escapePending` / `eightBitMode` in `TerminalCard.cpp`.

---

## 🖼️ Visuals & UX

> POM1 a déjà la meilleure UX du duo POM1/POM2 (126 tooltips, 15 tutoriels, boot scénographié, 0 ROM à fournir). Frictions résiduelles *(audit designer 2026-05-31)* :

- [ ] **Native file dialog** `[M · solid]` — drop in `nfd` (NativeFileDialog) or `tinyfiledialogs` — header-light, MIT, cross-platform. The in-app browser stays in the way.
- [ ] **HiDPI font scaling on Linux** `[S · nice]` — auto-detect monitor DPI on first window creation (`glfwGetMonitorContentScale`, GLFW 3.3+) and scale the default font; keep a Hardware → Display setting to override. Currently users must tweak `ImGui::GetIO().FontGlobalScale` manually.
- [ ] **1976 CRT fidelity (opt-in, default off)** `[M · nice]` — two sub-effects under the existing CRT toggle:
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing) — chars land ~60 / s, hardware scroll shifts buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock) — periodic static, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` after backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.
- [ ] **Shared video texture layer** `[M · solid]` — `Screen_ImGui.cpp` and `MainWindow_HardwareWindows.cpp` still own raw OpenGL texture lifecycles directly (glyph atlas, GEN2 HGR, TMS9918, GT-6144). Factor a backend-neutral texture helper for create/update/destroy + filter mode (`GL_NEAREST` pixel cards, `GL_LINEAR` glyph atlas) — also the prerequisite for the GEN2 GPU-shader path and the Metal backend below.
- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL deprecated since 10.14 (silenced via `GL_SILENCE_DEPRECATION`). `imgui_impl_metal.mm` + `imgui_impl_osx.mm` are drop-in; scope is porting raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 + HGR + GT-6144 textures) to MTLTexture.

---

## 🔧 Infra & technical debt

- [ ] **Snapshot residual gaps** `[M · nice]` — base format + 12-card per-card payloads + CPU section landed (May 2026). Remaining: cassette mid-stream playback position (re-load tape file by path on snapshot-load + seek to saved `playbackIndex`); WiFiModem / TerminalCard graceful "drop and reconnect" on load (currently kept disconnected); libresidfp internal filter integrators / oscillator phase (engine doesn't expose them — would need an upstream patch); SHA-256 footer (mentioned in `SnapshotIO.h` as v2 sweetener).
- [ ] **Durcissement désérialisation** `[S · nice]` *(audit 2026-05-31)* — 2 trouvailles « faibles » de `AUDIT_POM1.md` encore ouvertes : borner `fifoLevel` dans `PR40Printer::deserialize` (`PR40Printer.cpp:136`, sinon lecture OOB sur snapshot corrompu) ; tester `n > INT_MAX` avant le cast dans `parseIntPositive` (`CliDispatcher.cpp:85-94`). Robustesse sur entrées non fiables, pas de risque en usage normal.
- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) reading line-delimited commands while the emulator runs — same verbs as CLI flags, but for stateful sequences. Telnet on `:6502` carries keystrokes + display; this channel carries control without polluting the keyboard stream. Depends on CLI-verb + snapshot work above.
- [ ] **External `presets.json`** `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` to JSON under `doc/` (or next to the executable) so users add presets without recompiling. Loader in `MainWindow_Presets.cpp`, keep the C++ table as fallback.

### State rewind — raffinements (MVP livré)

> **MVP livré** → [`CHANGELOG.md`](CHANGELOG.md) : ring de snapshots delta-encodés, panneau **CPU → State Rewind…** + bande timeline inline dans la toolbar, état écran capturé dans la section `SCREEN`, **desktop-only** (`#if !POM1_IS_WASM`). Pinned by `rewind_buffer_smoke` ; voir CLAUDE.md › Emulation orchestration.

- [ ] **VRAM dirty-tracking for finer TMS9918 deltas** `[M · nice]` — the 16 KB VRAM section is chunk-diffed against the previous full blob each capture; a live VRAM dirty bitmap would cut the per-capture diff cost on graphics-heavy frames.
- [ ] **Seek cost on card-heavy presets** `[S · nice]` — `rewindSeekTo` reuses `loadSnapshotFromBuffer`, whose FLAGS dispatch re-applies card setters (may reload ROMs) every slider tick. Skip re-apply when the flag set is unchanged to keep dragging smooth.

---

## ⏸️ Deferred / conditional · 🚫 Blocked on external

> Spec connu, code tractable, mais conditionné à un déclencheur réel (logiciel exerçant la feature, demande utilisateur, hardware disponible). À promouvoir quand le déclencheur apparaît. **🚫 Blocked** = en attente d'une ressource externe hors de notre contrôle.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk` / `.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.
- [ ] **Joystick / paddle analogique (télémétrie)** `[déféré — hardware inexistant]` — **Décision 2026-06-16 : ne PAS implémenter.** Les paddles analogiques (`$C064`/`$C070` + timer 558) sont du hardware **Apple II**, pas Apple-1 — les modéliser émulerait une carte qui n'existe pas (règle « une vraie carte à la fois »). Côté télémétrie le digital est déjà couvert (FIFO `TELE_IN` + injection clavier `$D010`), et aucun logiciel Apple-1 réel n'utilise de paddle. À promouvoir seulement si une carte paddle Apple-1 réelle apparaît.
- [ ] 🚫 **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **PROM unpublished** (Bernie's physical kits, sold out; Applefritter says it will never be released). Contact [Applefritter PM](https://www.applefritter.com/user/254186/track) — he shared docs with the HoneyCrisp dev. Once obtained: load at `$C500`, extend `CassetteDevice` for header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
