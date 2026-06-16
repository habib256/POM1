# TODO

Open work on the **emulator** only. Shipped features → `git log` / `README.md`. 6502 software work → `dev/TODO6502.md`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Sections ordered by actionability: implementable first, externally-blocked last.

---

## ⏪ State rewind (microM8-style timeline)

> **MVP livré** → `[CHANGELOG.md](CHANGELOG.md)` : ring de snapshots delta-encodés, panneau **CPU → State Rewind…** + bande timeline inline dans la toolbar, état écran capturé dans la section `SCREEN`, **desktop-only** (`#if !POM1_IS_WASM`). Pinned by `rewind_buffer_smoke` ; voir CLAUDE.md › Emulation orchestration. Raffinements ouverts :

- [ ] **VRAM dirty-tracking for finer TMS9918 deltas** `[M · nice]` — the 16 KB VRAM section is chunk-diffed against the previous full blob each capture; a live VRAM dirty bitmap would cut the per-capture diff cost on graphics-heavy frames.
- [ ] **Seek cost on card-heavy presets** `[S · nice]` — `rewindSeekTo` reuses `loadSnapshotFromBuffer`, whose FLAGS dispatch re-applies card setters (may reload ROMs) every slider tick. Skip re-apply when the flag set is unchanged to keep dragging smooth.

---

## 🖥️ Uncle Bernie GEN2 — moteur faisceau (back-port POM2 → POM1)

> **Contexte** (AppleFritter, juin 2026) : la carte GEN2 *release* déplace les soft switches graphiques en `**$C250-$C257`** (`$C0` → `$C2` dans le code Apple II porté). Le vaporlock classique (`$C050` + bit-fumes bus flottant) **ne marche pas** avec l'ACI présent ; Bernie expose à la place un **drapeau MSB** sur lecture des soft switches `$C25x` pour détecter H-blank / V-blank. POM1 v1.8.6 ne modélise que le framebuffer passif `$2000-$3FFF` + rasterisation fin-de-frame MAME (`GraphicsCard.cpp`) — pas de faisceau cycle-accurate, pas de MMIO `$C2xx`.
>
> POM2 : `fillCompositeSignal` partage `**forEachBeamSegment`** avec le path RGBA depuis 2026-06-09 — splits horizontaux visibles en OE GPU/CPU + AppleWin, pas seulement LUT. Limites POM2 à ne pas recopier aveuglément : `signalPhaseOffset_` = une constante par frame ; lo-res clip au block-row (4 scanlines). GEN2 POM1 : pas de DHGR ni 560-wide → points 1 et scope 560 hors scope.

- [ ] **Signal 14,318 MHz `signalBuf`** `[M · nice]` — port `Apple2Display::fillCompositeSignal()` : 560 échantillons × 192 lignes ; `paintHgr` construit la ligne entière puis écrit `[col0·14, col1·14)` (réutiliser `buildHgrWordRow` / bitstream POM1). Même `forEachBeamSegment` que Phase 3 — zéro divergence RGBA/signal.
- [ ] **Chemin CPU `ColorCompositeOECpu`** `[M · nice]` — port `renderCompositeOeCpu()` + démodulateur FIR OpenEmulator (sans GLSL) pour WASM / fallback desktop. Menu GEN2 : « NTSC MAME (actuel) » vs « Composite OpenEmulator CPU » — calque le choix POM2 Graphics. **Non bloquant** : le LUT MAME actuel (`GraphicsCard`) reste le fast-path v1.
- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 si le shared texture layer (section Visuals) est en place ; sinon reporter.

- [ ] **Portage pilote d'un jeu Apple II HGR** `[M · nice]` — Taipan / Breakout sur la carte release, pour exercer le moteur beam sur du vrai logiciel de jeu.

## 🔌 Peripherals

- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — [https://github.com/flowenol/apple1-serial](https://github.com/flowenol/apple1-serial) — serial-port bootloader / terminal (complements TurboType / 8BitFlux). Pipes through Terminal Card or its own ACIA variant; likely a text-format loader on top of `Memory::loadHexDump` + paste pipeline.
- [ ] **ACI header + checksum on the jaquette** `[S · nice]` — `tapeinfo.txt` already drives the *"Type 0280.0FFFR"* label. Parse the raw `.aci` pulse-capture header (from / to / checksum) in `CassetteDevice::loadAciTape()` and surface for tapes without a sidecar entry.
- [ ] **Briel Multi I/O — SpeakJet** `[M · nice]` — 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as separate optional peripheral so it coexists with microSD.

---

## 📥 Loaders

- [ ] **TurboType 57 600-baud loader** `[M · solid]` — Uncle Bernie's format, shipped by 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244). Protocol: Wozmon-speed bootstrap (200 ms/newline, 20 ms/char) installs an in-RAM dropper that **skips `$D012` echoes** and streams bytes at 57.6 kbps with running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in <30 s vs ~2 400 baud Wozmon. POM1 side: parse `.TUR`/`.APL`, switch Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon.

---

## 🛠️ POM1 Bench — IDE in-app façon Arduino (front-end auteur du SDK)

> **Phases A-E livrées** (2026-06-14/16) → `[CHANGELOG.md](CHANGELOG.md)` : éditeur 6502/cc65 (ImGuiColorTextEdit), Verify / Build & Run via `ca65`/`ld65`/`cl65`, **Serial Monitor = télémétrie** (`$C440-$C443`), habillage Arduino, **cibles « machine + build »** (`kBenchTargets[]`, dialogue New 2 langages × 4 machines), bundle cc65 relocatable + sonde exe-relative (`Pom1BenchHost::probe`). **Desktop-only** pour le chemin asm/C (pas de subprocess en WASM). Reste ouvert :

- [ ] **Publier la release 1.9.2 tri-plateforme** `[M]` — la dernière release GitHub est **1.9.0** (1.9.1/1.9.2 absents) alors que les chaînes de version disent 1.9.2. Construire AppImage (vitrine) + .dmg + ZIP **avec cc65 embarqué**, produire + tester les bundles macOS/Windows (binaires cc65 par plateforme), attacher à un tag `v1.9.2`.
- [ ] **WASM cc65 — arbitrer le port emscripten** `[L · nice]` — compiler cc65 en WASM reste un chantier disproportionné ; le build web garde le **Bench Woz-hex** + un CTA « desktop only — download the app » (`IBenchHost::headerNote()`). Soit un port emscripten de cc65 (gros), soit on assume le CTA comme réponse définitive.

---

## 🔤 BASIC dans le Bench — lancer du BASIC via les interpréteurs (Applesoft Lite / Integer)

> **Contexte** (juin 2026) : **aucune compilation** ici — on **tokenise / injecte** un listing BASIC dans les interpréteurs que POM1 embarque **déjà** : **Integer BASIC** (`$E000`, cold-start `E000R`) et **Applesoft Lite** (`$6000`, preset microSD, `6000R`). Avantages : natif, **zéro dépendance externe**, fidélité Applesoft maximale, et **ça marche aussi en WASM** (contrairement au reste du Bench qui shell-out cc65). Nouvel axe langage « BASIC » dans le Bench, 3ᵉ langage à côté de asm/C.

- [ ] **Axe langage « BASIC » + Run par injection** `[M · solid]` — nouveau langage dans la matrice Bench (`kBenchTargets[]`, `src/Pom1BenchHost.cpp`) dont l'**Upload** ne compile pas mais **alimente l'interpréteur** : la cible choisit l'interpréteur + son preset (auto-plug), fait le cold-start (`E000R` / `6000R`), puis envoie le listing. **MVP = streaming clavier** : réutilise le pipeline paste→clavier existant (`KeyboardController`, cap 4096) pour « taper » le programme ligne à ligne au prompt, puis `RUN`. Marche en WASM.
- [ ] **Cibles BASIC (machine + interpréteur)** `[S · solid]` — au moins deux : **Integer BASIC** (ROM `$E000`, présente dans la plupart des presets) et **Applesoft Lite** (preset microSD, ROM `$6000`). Sélectionner la cible plugge la bonne machine, 
- [ ] **Exemple BASIC intégré** `[S · nice]` — un sketch « Hello BASIC » (`10 PRINT "HELLO, APPLE-1"` / `20 GOTO 10`) dans le menu *Examples* du Bench, cible Integer BASIC par défaut. Point d'entrée immédiat.

---

## 🔬 Debug & profiling — inspiré de 8bitworkshop (sonde CRT, profiler, debug source)

> **Contexte** (juin 2026) : analyse comparative de l'IDE [8bitworkshop](https://8bitworkshop.com/docs/docs/ide.html) (Steven Hugg). Son point fort = le **débogage *visuel*** (CRT Probe, Memory Probe, Symbol Profiler, Probe Log) + le **tout-navigateur** (toolchains compilées en WASM via Emscripten). POM1 gagne déjà sur le **test automatisé** (telemetry + lock-step + `--headless`) et la **fidélité matérielle** (GEN2 beam-raced, HST0). Le delta exploitable = lui emprunter ses outils de debug visuel/profiling et les **marier au moteur beam + telemetry de POM1** — ce que 8bitworkshop ne peut pas égaler sur l'Apple-1. Tout est **desktop-only** (extension du Bench), sauf l'embed / playable-link (WASM). **Pistes, pas un engagement** — à promouvoir si la communauté dev Apple-1 décolle.

- [ ] **DevBench in-browser (cc65 → WASM)** `[L · nice]` — **rouvre la décision « WASM cc65 abandonné » (2026-06-14)** à la lumière de 8bitworkshop, qui *prouve* la faisabilité (cc65/ca65/ld65 compilés en WASM via Emscripten, tournant dans l'onglet). Gros chantier, mais c'est le levier qui ferait tourner le Bench dans le **canal web zéro-friction** (cf. analyse distribution : ~80 % Windows + WASM = les canaux où le toolchain natif passe le plus mal). À reconsidérer, pas à lancer tête baissée.

---

## 🎨 Visuals & UX

> POM1 a déjà la meilleure UX du duo POM1/POM2 (126 tooltips, 15 tutoriels, boot scénographié, 0 ROM à fournir). Frictions résiduelles *(audit designer 2026-05-31)* :

- [ ] **Native file dialog** `[M · solid]` — drop in `nfd` (NativeFileDialog) or `tinyfiledialogs` — header-light, MIT, cross-platform. The in-app browser stays in the way.
- [ ] **HiDPI font scaling on Linux** `[S · nice]` — auto-detect monitor DPI on first window creation (`glfwGetMonitorContentScale`, GLFW 3.3+) and scale the default font; keep a Hardware → Display setting to override. Currently users must tweak `ImGui::GetIO().FontGlobalScale` manually.
- [ ] **1976 CRT fidelity (opt-in, default off)** `[M · nice]` — two sub-effects under the existing CRT toggle:
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing) — chars land ~60 / s, hardware scroll shifts buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock) — periodic static, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` after backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.
- [ ] **Shared video texture layer** `[M · solid]` — `Screen_ImGui.cpp` and `MainWindow_HardwareWindows.cpp` still own raw OpenGL texture lifecycles directly (glyph atlas, GEN2 HGR, TMS9918, GT-6144). Factor a backend-neutral texture helper for create/update/destroy + filter mode (`GL_NEAREST` pixel cards, `GL_LINEAR` glyph atlas) so the future Metal backend ports one abstraction.
- [ ] **ImGui Metal backend on macOS** `[L · nice]` — OpenGL deprecated since 10.14 (silenced via `GL_SILENCE_DEPRECATION`). `imgui_impl_metal.mm` + `imgui_impl_osx.mm` are drop-in; scope is porting raw GL calls in `Screen_ImGui.cpp` / `MainWindow_HardwareWindows.cpp` (glyph atlas, TMS9918 + HGR + GT-6144 textures) to MTLTexture.

---

## 🔧 Technical debt

> Items marqués *(audit 2026-05-31)* issus de l'audit 3 angles (ingénieur · designer · commercial).

- [ ] **Factoriser les libs graphiques GEN2↔TMS9918 — intégration build** `[S · solid]` — l'axe 1 pose une couche partagée additive `dev/lib/gfx/` (géométrie ligne/cercle/rect/ellipse + conversion entier→ASCII, backend résolu au lien) à compiler puis brancher aux chemins de build cc65, dont la ligne cl65 du Bench (`src/Pom1BenchHost.cpp`). Travail 6502/cc65 + axes 2-3 (fonte partagée, façade) → `[dev/TODO6502.md](dev/TODO6502.md)` § *Factoriser les libs graphiques GEN2 HGR ↔ TMS9918*.
- [ ] **Durcissement désérialisation** `[S · nice]` *(audit 2026-05-31)* — 2 trouvailles « faibles » de `AUDIT_POM1.md` encore ouvertes : borner `fifoLevel` dans `PR40Printer::deserialize` (`PR40Printer.cpp:136`, sinon lecture OOB sur snapshot corrompu) ; tester `n > INT_MAX` avant le cast dans `parseIntPositive` (`CliDispatcher.cpp:85-94`). Robustesse sur entrées non fiables, pas de risque en usage normal.
- [ ] **Snapshot residual gaps** `[M · nice]` — base format + 12-card per-card payloads + CPU section landed (May 2026). Remaining work: cassette mid-stream playback position (re-load tape file by path on snapshot-load + seek to saved `playbackIndex`); WiFiModem / TerminalCard graceful "drop and reconnect" on load (currently kept disconnected); libresidfp internal filter integrators / oscillator phase (engine doesn't expose them — would need an upstream patch); SHA-256 footer (mentioned in `SnapshotIO.h` as v2 sweetener).
- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) reading line-delimited commands while the emulator runs — same verbs as CLI flags, but for stateful sequences. Telnet on `:6502` carries keystrokes + display; this channel carries control without polluting the keyboard stream. Depends on CLI-verb + snapshot work above.
- [ ] **Terminal Card — `Ctrl-K` hand-over** `[S · nice]` — match the 8BitFlux toggle: a `Ctrl-K` byte suspends `$D010`/`$D011` injection until `Ctrl-T` re-attaches. Useful once a script bootstrapped a program and the user wants to play without dropping the session. Hook: `injectionSuspended` next to `escapePending` / `eightBitMode` in `TerminalCard.cpp`.
- [ ] **External `presets.json`** `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` to JSON under `doc/` (or next to the executable) so users add presets without recompiling. Loader in `MainWindow_Presets.cpp`, keep the C++ table as fallback.

---

## ⏸️ Deferred / conditional

Spec public, code change tractable, but gated on a real-world trigger (software exercising the feature, user demand, hardware availability). Promote when the trigger appears.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk` / `.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.
- [ ] **Uncle Bernie's joystick / paddle analogique (télémétrie)** `[déféré — hardware inexistant]` — **Décision 2026-06-16 : ne PAS implémenter.** Les paddles analogiques (`$C064`/`$C070` + timer 558) sont du hardware **Apple II**, pas Apple-1 — les modéliser émulerait une carte qui n'existe pas (règle « une vraie carte à la fois »). Côté télémétrie le digital est déjà couvert (FIFO `TELE_IN` + injection clavier `$D010`), et aucun logiciel Apple-1 réel n'utilise de paddle. À promouvoir seulement si une carte paddle Apple-1 réelle apparaît.
- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **PROM unpublished** (Bernie's physical kits, sold out; Applefritter says it will never be released). Contact [Applefritter PM](https://www.applefritter.com/user/254186/track) — he shared docs with the HoneyCrisp dev. Once obtained: load at `$C500`, extend `CassetteDevice` for header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).