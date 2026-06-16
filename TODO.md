# TODO

Open work on the **emulator** only. Shipped features → `git log` / `README.md`. 6502 software work → `dev/TODO6502.md`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Sections ordered by actionability: implementable first, externally-blocked last.

---

## ⏪ State rewind (microM8-style timeline)

> **MVP shipped** (`RewindBuffer` + in-memory `SnapshotWriter`/`Reader` + **CPU → State Rewind…** scrub panel + **inline timeline band in the toolbar** entre le badge silicon/fantasy et About — glissé = preview sur les écrans branchés, lâcher = relance à cet instant ; l'enregistrement s'auto-active). **L'état écran texte est capturé dans la snapshot** (section `SCREEN` via `DisplayDevice::serialize` — la grille Apple-1 n'est pas en RAM) → le scrub affiche bien les anciennes images (bénéficie aussi aux `.snap`). Delta-encoded ring of snapshot blobs (section + 256 B chunk deltas, keyframe-anchored segments, budget eviction). **Desktop only** — désactivé en WASM (`#if !POM1_IS_WASM` : capture + bande + panneau), car la capture périodique d'état complet sur l'unique thread main-loop saccade le build web. Pinned by `rewind_buffer_smoke`. See CLAUDE.md › Emulation orchestration.

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

## 🛠️ POM1 Bench — IDE in-app façon Arduino (front-end auteur du SDK)

> **Phases A-E livrées** (2026-06-14/16) → [`CHANGELOG.md`](CHANGELOG.md) : éditeur 6502/cc65 (ImGuiColorTextEdit), Verify / Build & Run via `ca65`/`ld65`/`cl65`, **Serial Monitor = télémétrie** (`$C440-$C443`), habillage Arduino, **cibles « machine + build »** (`kBenchTargets[]`, dialogue New 2 langages × 4 machines), bundle cc65 relocatable + sonde exe-relative (`Pom1BenchHost::probe`). **Desktop-only** pour le chemin asm/C (pas de subprocess en WASM). Reste ouvert :

- [ ] **Publier la release 1.9.2 tri-plateforme** `[M]` — la dernière release GitHub est **1.9.0** (1.9.1/1.9.2 absents) alors que les chaînes de version disent 1.9.2. Construire AppImage (vitrine) + .dmg + ZIP **avec cc65 embarqué**, produire + tester les bundles macOS/Windows (binaires cc65 par plateforme), attacher à un tag `v1.9.2`.
- [ ] **WASM cc65 — arbitrer le port emscripten** `[L · nice]` — compiler cc65 en WASM reste un chantier disproportionné ; le build web garde le **Bench Woz-hex** + un CTA « desktop only — download the app » (`IBenchHost::headerNote()`). Soit un port emscripten de cc65 (gros), soit on assume le CTA comme réponse définitive.

---

## 🔤 BASIC dans le Bench — lancer du BASIC via les interpréteurs (Applesoft Lite / Integer)

> **Contexte** (juin 2026) : **aucune compilation** ici — on **tokenise / injecte** un listing BASIC dans les interpréteurs que POM1 embarque **déjà** : **Integer BASIC** (`$E000`, cold-start `E000R`) et **Applesoft Lite** (`$6000`, preset microSD, `6000R`). Avantages : natif, **zéro dépendance externe**, fidélité Applesoft maximale, et **ça marche aussi en WASM** (contrairement au reste du Bench qui shell-out cc65). Nouvel axe langage « BASIC » dans le Bench, 3ᵉ langage à côté de asm/C.

- [ ] **Axe langage « BASIC » + Run par injection** `[M · solid]` — nouveau langage dans la matrice Bench (`kBenchTargets[]`, `src/Pom1BenchHost.cpp`) dont l'**Upload** ne compile pas mais **alimente l'interpréteur** : la cible choisit l'interpréteur + son preset (auto-plug), fait le cold-start (`E000R` / `6000R`), puis envoie le listing. **MVP = streaming clavier** : réutilise le pipeline paste→clavier existant (`KeyboardController`, cap 4096) pour « taper » le programme ligne à ligne au prompt, puis `RUN`. Marche en WASM.
- [ ] **Cibles BASIC (machine + interpréteur)** `[S · solid]` — au moins deux : **Integer BASIC** (ROM `$E000`, présente dans la plupart des presets) et **Applesoft Lite** (preset microSD, ROM `$6000`). Sélectionner la cible plugge la bonne machine, exactement comme les cibles asm/C actuelles.
- [ ] **Tokenizer natif (v2, optionnel)** `[M · nice]` — au lieu du streaming clavier (lent + plafond 4096), **tokeniser** le source en format interne et l'écrire directement en mémoire + fixer les pointeurs programme (début/fin programme, `LOMEM`/`HIMEM`). Bien documenté pour Applesoft ; Integer BASIC plus retors. Supprime la limite de taille et la lenteur de saisie ; garder le streaming en fallback.
- [ ] **Exemple BASIC intégré** `[S · nice]` — un sketch « Hello BASIC » (`10 PRINT "HELLO, APPLE-1"` / `20 GOTO 10`) dans le menu *Examples* du Bench, cible Integer BASIC par défaut. Point d'entrée immédiat.

---

## 🔬 Debug & profiling — inspiré de 8bitworkshop (sonde CRT, profiler, debug source)

> **Contexte** (juin 2026) : analyse comparative de l'IDE [8bitworkshop](https://8bitworkshop.com/docs/docs/ide.html) (Steven Hugg). Son point fort = le **débogage *visuel*** (CRT Probe, Memory Probe, Symbol Profiler, Probe Log) + le **tout-navigateur** (toolchains compilées en WASM via Emscripten). POM1 gagne déjà sur le **test automatisé** (telemetry + lock-step + `--headless`) et la **fidélité matérielle** (GEN2 beam-raced, HST0). Le delta exploitable = lui emprunter ses outils de debug visuel/profiling et les **marier au moteur beam + telemetry de POM1** — ce que 8bitworkshop ne peut pas égaler sur l'Apple-1. Tout est **desktop-only** (extension du Bench), sauf l'embed / playable-link (WASM). **Pistes, pas un engagement** — à promouvoir si la communauté dev Apple-1 décolle.

- [ ] **CRT Probe — sonde faisceau (la feature signature)** `[M · solid]` — overlay sur la fenêtre GEN2 qui colore, **à chaque position du faisceau**, ce que fait le CPU (IRQ violet · DMA noir · CPU gris · teintes read/write/stack — modèle 8bitworkshop) ; clic → breakpoint à ce `emuCycle`/scanline. **La géométrie beam est déjà là** (`Gen2VideoScanner` : compteur de cycles, `scanner_address`, journal `VideoEvent` ; `frameCycleToPos` ; `forEachBeamSegment`) — reste à **capter un flux d'activité CPU par cycle** (PC / addr R-W / état IRQ) via des taps `M6502` + `PeripheralBus`/`memRead`/`memWrite`, et à le mapper sur la position du faisceau. Parfaitement on-brand (`a1_crazycycle` = démo beam-racée). **C'est ce qui rendrait POM1 meilleur que 8bitworkshop sur l'Apple-1.**
- [ ] **Profiler scanline + Symbol Profiler + Analyze Timing** `[M · solid]` — vue **budget cycles par scanline** (où part le temps d'une frame), **compteurs d'appels** (JSR : ligne raster début/fin + nombre d'appels) et **compteurs read/write par symbole** (≈ *Symbol Profiler*) + analyse statique de timing par instruction (≈ *Analyze Timing*). Réutilise le CPU cycle-exact (`M6502::getCurrentInstructionCycles`) + les taps d'accès du CRT Probe. Le *Symbol Profiler* dépend des symboles cc65 (item ci-dessous). Indispensable pour les devs démo / jeu cycle-exacts visés.
- [ ] **Debug au niveau source (symboles cc65)** `[L · solid]` — le plus gros gain *ergonomique quotidien*. Le Bench pilote déjà cc65 → émettre `ld65 --dbgfile` + `-Ln labels.lbl`, les parser, puis : **labels dans le disasm + le memory viewer**, **watch de variables par nom**, **Run To Line** (breakpoint sur ligne source / clic gouttière), **surlignage de la ligne courante au step**, **Step Over / Step Out / Step Backwards** (granularité instruction). Step-back peut s'appuyer sur le **State Rewind** existant ; over/out sur la profondeur de pile JSR/RTS dans `M6502`.
- [ ] **Memory Probe + Probe Log + breakpoints conditionnels** `[M · solid]` — wins complémentaires moins chers : **Memory Probe** = bitmap heatmap des accès R/W (réutilise le dirty-page tracking de `SnapshotPublisher` + un compteur R/W par adresse via les taps `PeripheralBus`) ; **Probe Log** = journal scrollable cycle/instruction (extension du PC-ring 12 slots `dumpPcTrace`) ; **breakpoints conditionnels / data** (étendre `M6502::setBreakpoint`, aujourd'hui un seul PC inconditionnel : break sur write de `$XX`, ou quand `A==N`).
- [ ] **DevBench in-browser (cc65 → WASM)** `[L · nice]` — **rouvre la décision « WASM cc65 abandonné » (2026-06-14)** à la lumière de 8bitworkshop, qui *prouve* la faisabilité (cc65/ca65/ld65 compilés en WASM via Emscripten, tournant dans l'onglet). Gros chantier, mais c'est le levier qui ferait tourner le Bench dans le **canal web zéro-friction** (cf. analyse distribution : ~80 % Windows + WASM = les canaux où le toolchain natif passe le plus mal). À reconsidérer, pas à lancer tête baissée.
- [ ] **Bonus pas chers (intégration web + ergonomie cc65)** `[S · nice]` — (a) **embed IFRAME** : exposer le build WASM avec des paramètres type `?embed=1&platform=…&file0_data=…` pour mettre un **Apple-1 jouable directement dans un post Applefritter / une page web** ; (b) **playable link** (code / état encodés dans l'URL) ; (c) ergonomies cc65 façon 8bitworkshop dans le Bench : `__MAIN__` (tests par module), `#embed` (binaire dans un `.c`), `CFGFILE` via `#define`.

---

## 🎨 Visuals & UX

> POM1 a déjà la meilleure UX du duo POM1/POM2 (126 tooltips, 15 tutoriels, boot scénographié, 0 ROM à fournir). Frictions résiduelles *(audit designer 2026-05-31)* :

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

- [ ] **Factoriser les libs graphiques GEN2↔TMS9918 — intégration build** `[S · solid]` — l'axe 1 pose une couche partagée additive `dev/lib/gfx/` (géométrie ligne/cercle/rect/ellipse + conversion entier→ASCII, backend résolu au lien) à compiler puis brancher aux chemins de build cc65, dont la ligne cl65 du Bench (`src/Pom1BenchHost.cpp`). Travail 6502/cc65 + axes 2-3 (fonte partagée, façade) → [`dev/TODO6502.md`](dev/TODO6502.md) § *Factoriser les libs graphiques GEN2 HGR ↔ TMS9918*.
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
- [ ] **Entrée joystick / paddle analogique (télémétrie)** `[déféré — hardware inexistant]` — **Décision 2026-06-16 : ne PAS implémenter.** Les paddles analogiques (`$C064`/`$C070` + timer 558) sont du hardware **Apple II**, pas Apple-1 — les modéliser émulerait une carte qui n'existe pas (règle « une vraie carte à la fois »). Côté télémétrie le digital est déjà couvert (FIFO `TELE_IN` + injection clavier `$D010`), et aucun logiciel Apple-1 réel n'utilise de paddle. À promouvoir seulement si une carte paddle Apple-1 réelle apparaît.

---

## 🚫 Blocked on external

- [ ] **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **PROM unpublished** (Bernie's physical kits, sold out; Applefritter says it will never be released). Contact [Applefritter PM](https://www.applefritter.com/user/254186/track) — he shared docs with the HoneyCrisp dev. Once obtained: load at `$C500`, extend `CassetteDevice` for header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
