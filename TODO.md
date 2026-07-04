# TODO

Open work on the **emulator** only. Shipped work → `[CHANGELOG.md](CHANGELOG.md)` / `git log` · user tour → `README.md` · 6502 software → `[dev/TODO6502.md](dev/TODO6502.md)`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Grouped by subsystem; deferred / externally-blocked last. Only open items live here — completed work is lifted to `[CHANGELOG.md](CHANGELOG.md)` as it ships.

---

## 🎨 Graphics

### GEN2 beam engine — Phase 4: composite OpenEmulator (rendu optionnel, non bloquant)

> Phases 0-3 + 5 + **chemin composite CPU (`RenderMode::CompositeOECpu`) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. **Le composite OpenEmulator est désormais le rendu par défaut de l'app** (`gen2RenderMode=1`) ; le LUT MAME reste dispo via le menu GEN2 (et reste le défaut de la `GraphicsCard` standalone, pour la golden image). Reste seulement le chemin GPU-shader, optionnel :

- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 (même noyaux FIR + matrice que le chemin CPU déjà livré) si le *Shared video texture layer* (livré) est exploité ; le CPU couvre déjà WASM + desktop, donc **reportable** tant qu'aucun besoin de perf n'apparaît.
  - **Conclusion : défer jusqu'à un besoin perf concret — zéro gain visuel, purement « où tournent les calculs ».** Le décodage NTSC (`GraphicsCard.cpp` : FIR 17 taps luma+chroma + démod sin/cos + matrice YUV→RGB) tourne sur CPU dans un buffer 280×192 minuscule (~3-4 M MAC/frame) → invisible dans un profil desktop. Le porter déplace **les mêmes maths** vers un fragment shader : même image byte-identique (épinglée `hgr_convert_smoke`), donc **aucune capacité visuelle ajoutée**. Ne le faire QUE si (1) on ajoute du post-traitement lourd plein écran (courbure CRT, bloom, scanlines shader, phosphore par pixel, NTSC à résolution interne >280) qui rend le CPU goulot, OU (2) un profil montre la démod comme coût réel (GPU/CPU faible, très haut refresh). Coût du porting : 2 chemins à garder byte-identiques + shader à décliner GLSL/MSL/WebGL (dont le patch sampler Metal délicat). Prérequis unique déjà livré (*shared video texture layer*), donc reportable **sans dette** — le jour venu, le port est direct.

### TMS9918 — synchro beam/CPU sub-scanline (mid-line splits + statut au tick)

> **Étapes 0-2 + socle** `BeamClock` **+ corrections silicium (fidélité init, pacing pad18, statut F+C, plancher 9c) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. L'axe entrée CPU→VRAM est tick-accurate ; l'axe sortie a été rapproché. Reste ouvert :

- [ ] **Fetch sprite SAT « une ligne en avance » sous-ligne** `[M · solid]` — line n doit afficher les sprites fetchés pendant n-1 (le latch seamless mode/blank est livré ; reste la latence de fetch SAT exacte). Effet visible seulement sur écritures SAT mid-active (rares) → à valider sur silicium (Parmigiani) avant de modéliser la latence.
- [ ] **Journal VideoEvents + renderer par rejeu, adoption GEN2** `[L · solid]` — remplacer le rattrapage *eager* (Étapes 0-2) par un journal `(cycle,kind,value)` per-cycle (h,v) composé par **rejeu** entre sync points (découplage total + rewind-friendly). GEN2 a déjà ce journal (`gen2RecordingEvents`, `Memory.cpp`). **Livré (juillet 2026) : le journal GEN2 entre désormais dans le snapshot / rewind** (section `GEN2VID` v5 sérialise le journal publié + son frame-start ; `snapshot_smoke`) → `[CHANGELOG.md](CHANGELOG.md)`. Reste ouvert : (a) **généraliser** le journal en facilité partagée (hors `Memory`) + faire adopter `BeamGeometry`/`beamPosAt` (`src/BeamClock.h`) par le rejeu GEN2 (aujourd'hui géométrie GEN2-privée dans `GraphicsCard::frameCycleToPos`) ; (b) **faire adopter le journal+rejeu par le TMS9918** (remplacer `renderBeamCatchUp`/`syncSpriteScanToBeam` eager + sérialiser son journal comme GEN2). Objectif cycle-granularity commun POM1/POM2.

---

## 🛠️ Dev tooling

### POM1 Bench

> **Phases A-E + bundle cc65 (asm/C) + BASIC + compilateur natif livrés** → `[CHANGELOG.md](CHANGELOG.md)`. Reste ouvert :

- [ ] **WASM cc65 — vérif navigateur** `[S · nice]` — la chaîne cc65-en-WASM est bundlée + câblée au bouton du Bench (asm + C + TMS + GEN2, glue Node-vérifiée byte-identique au natif des deux côtés) → `[CHANGELOG.md](CHANGELOG.md)`. Reste à **ouvrir** `POM1.html` **et tester à la main** New + un sketch asm + un sketch C (non testable headless) ; le chemin TMS-C n'a pas été vérifié individuellement en Node (même mécanisme `buildC` que GEN2-C).

### BASIC dans le Bench

> **Injection (Integer + Applesoft, 4 cibles), coloration, tokeniseurs, compilateur natif → 6502 (**`3DHat.apf`**/**`RodColor.apf` **autonomes sur GEN2 + TMS) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. Reste ouvert :

- [ ] **Mode UI explicite « Inject » vs « Compile »** `[M · solid]` — le tokeniseur (`BasicTokeniserApplesoft`) est câblé aux 4 cibles Applesoft (route par extension) mais aucun sélecteur ne le distingue de l'injection dans le dialogue *New* ; exposer le choix, et offrir le **compilateur natif** (`BasicCompilerApplesoft`) comme 2ᵉ mode compilé.
- [ ] **Extensions optionnelles du compilateur natif** `[M · nice]` — niveaux float plus petits (binary16 / virgule fixe) pour coords bornées ; `ATN`, `RND` ; variables chaîne + `POKE` pour les listings restants. Investiguer aussi le **bug** `IF (X AND 7)=0` **qui fige l'interpréteur** via le tokeniseur (le compilateur natif est correct).
- [ ] **LIST / RUN explicites + warm-start** `[S · nice]` — Verify entre le programme sans `RUN` (prêt à `LIST`) ; un toggle « cold/warm » (`E000R`/`6000R` vs `E2B3R`/`6003R`) préserverait un programme déjà tapé.
- [ ] **Applesoft Apple II + HGR sur la carte GEN2 de Bernie** `[L · solid]` — faire tourner du BASIC graphique Apple II (`HGR`/`HGR2`/`HCOLOR`/`HPLOT`/`DRAW`) sur la carte GEN2. **Le framebuffer est déjà bon** : GEN2 est en `$2000/$4000` au layout HGR Apple II, donc le calcul d'adresse de `HPLOT` marche tel quel. **Le seul correctif bus = aliaser les soft-switches Apple II** `$C050-$C057` (TXTCLR..HIRES) vers la logique GEN2 : aujourd'hui GEN2 ne décode que `$C250-$C257` (`(a&0x0200)&&(a&0x0010)`, donc A9=1) et `$C05x` a A9=0 → invisible. **Phase 1** `[S]` : étendre le handler bus GEN2 (`Memory.cpp` `gen2SoftSwitchBusHandle`) pour décoder aussi `$C050-$C057` (mapper les 3 bits bas, surveiller le conflit avec l'ACI `$C0xx` / la règle « une carte à la fois »). **Phase 2** `[M-L]` : un interpréteur Applesoft *conscient de GEN2*. Deux voies — (a) **étendre l'Applesoft Lite Apple-1** avec les mots-clés HGR pointant sur les soft-switches GEN2 + `$2000` (natif Apple-1, garde l'I/O `$D012`/`$D010`, pas de couche Apple II) ; (b) **vrai ROM Applesoft Apple II** + veneer I/O (COUT→`$D012`, clavier `$C000/$C010`→`$D010/$D011`, écran texte `$0400-$07FF`) — plus lourd. Recommandation : **Phase 1 d'abord** (utile aussi à l'asm/C, débloque tout chemin HGR Apple II), puis voie (a) pour un Applesoft graphique Apple-1-natif. Démo cible : ROSACES (`HPLOT TO`).

### LOGO dans le Bench

> **Interpréteur V2.6 + injection (`injectLogo` / `LogoProgramLoader`) + 10 sketches `sketchs/logo/` + REPL interactif (send / écho / historique ↑↓ / Break Ctrl-G) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. LOGO est le **4ᵉ langage** du *New*, deux cibles (TMS9918 `4000R`, GEN2 HGR `6000R`), WASM-safe, pin `bench_logo_inject_smoke`. Reste ouvert (nice-to-have) :
> - [ ] **Livre d'exemples LOGO dans le popup *Examples*** `[S · solid]` — les 10 `.logo` de `sketchs/logo/` existent (et sont préchargés MEMFS côté web) mais ne sont atteignables que par *File → Load* ; les câbler dans `kP1Examples[]`/`examples_` (groupe « LOGO », ouverture 1-clic) comme les exemples asm/C, pour la découvrabilité.
> - [ ] **Read-back / LIST round-trip** `[M · nice]` — l'inverse de `LogoProgramLoader` : reparser le `proc_table` résident → source, pour « récupérer depuis la machine » ce qui a été défini au REPL et pour un Verify reflétant l'état réel. Prérequis d'un **inject à chaud** (append de procs + bump `n_procs` sans cold-reset, flux incrémental proche du vrai REPL).

### Éditeur DevBench — onglets multi-fichiers + Markdown

> **Éditeur multi-documents, rendu Markdown (Preview/Edit), coloration Markdown en mode Edit, et garde de fermeture (popup Discard/Cancel sur onglet non sauvegardé) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. Reste ouvert (nice-to-have) :

- [ ] **Vue Markdown split (édition + aperçu live)** `[M · nice]` — au lieu de la bascule Preview/Edit, une vue côte-à-côte qui rafraîchit l'aperçu à la frappe.

---

## 🔌 Peripherals & loaders

- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — [https://github.com/flowenol/apple1-serial](https://github.com/flowenol/apple1-serial) — serial-port bootloader / terminal (complements TurboType / 8BitFlux). Pipes through Terminal Card or its own ACIA variant; likely a text-format loader on top of `Memory::loadHexDump` + paste pipeline.
- [ ] 🚫 **TurboType 57 600-baud loader** `[M · solid]` — **En attente de Bernie (échange courriel 2026-06-24) : spec détaillée + une ROM/binaire du dropper nécessaires avant implémentation.** Uncle Bernie's format, shipped by 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244). Protocol: Wozmon-speed bootstrap (200 ms/newline, 20 ms/char) installs an in-RAM dropper that **skips** `$D012` **echoes** and streams bytes at 57.6 kbps with running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in <30 s vs ~2 400 baud Wozmon. POM1 side: parse `.TUR`/`.APL`, switch Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon. *Note émulateur :* `loadHexDump` *gère déjà le multi-blocs + les marqueurs* `T`*/*`X`*, et charge instantanément — TurboType n'a de valeur que pour l'authenticité/démo du protocole, pas pour la vitesse de chargement.*
- [ ] **ACI header + checksum on the jaquette** `[S · nice]` — `tapeinfo.txt` already drives the *"Type 0280.0FFFR"* label. Parse the raw `.aci` pulse-capture header (from / to / checksum) in `CassetteDevice::loadAciTape()` and surface for tapes without a sidecar entry.
- [ ] **Briel Multi I/O — SpeakJet** `[M · nice]` — 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as a separate optional peripheral so it coexists with microSD.
- [ ] **Terminal Card —** `Ctrl-K` **hand-over** `[S · nice]` — match the 8BitFlux toggle: a `Ctrl-K` byte suspends `$D010`/`$D011` injection until `Ctrl-T` re-attaches. Useful once a script bootstrapped a program and the user wants to play without dropping the session. Hook: `injectionSuspended` next to `escapePending` / `eightBitMode` in `TerminalCard.cpp`.

---

## 🖼️ Visuals & UX

> POM1 a déjà la meilleure UX du duo POM1/POM2 (126 tooltips, 15 tutoriels, boot scénographié, 0 ROM à fournir). **Native file dialogs, shared video texture layer, backend Metal macOS livrés** → `[CHANGELOG.md](CHANGELOG.md)`. Frictions résiduelles *(audit designer 2026-05-31)* :

- [ ] **HiDPI font scaling on Linux** `[S · nice]` — auto-detect monitor DPI on first window creation (`glfwGetMonitorContentScale`, GLFW 3.3+) and scale the default font; keep a Hardware → Display setting to override. Currently users must tweak `ImGui::GetIO().FontGlobalScale` manually.
- [ ] **1976 CRT fidelity (opt-in, default off)** `[M · nice]` — two sub-effects under the existing CRT toggle:
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing) — chars land ~60 / s, hardware scroll shifts buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock) — periodic static, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` after backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.

---

## 🔧 Infra & technical debt

> **Durcissement désérialisation (audit 2026-05-31) livré** → `[CHANGELOG.md](CHANGELOG.md)`.

- [ ] **Snapshot residual gaps** `[M · nice]` — base format + 12-card per-card payloads + CPU section landed (May 2026). Remaining: cassette mid-stream playback position (re-load tape file by path on snapshot-load + seek to saved `playbackIndex`); WiFiModem / TerminalCard graceful "drop and reconnect" on load (currently kept disconnected); libresidfp internal filter integrators / oscillator phase (engine doesn't expose them — would need an upstream patch); SHA-256 footer (mentioned in `SnapshotIO.h` as v2 sweetener).
- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) reading line-delimited commands while the emulator runs — same verbs as CLI flags, but for stateful sequences. Telnet on `:6502` carries keystrokes + display; this channel carries control without polluting the keyboard stream. Depends on CLI-verb + snapshot work above.
- [ ] **External** `presets.json` `[S · nice]` — `MainWindow_Presets.cpp` already flags itself as the migration target. Move `kMachinePresets[]` to JSON under `doc/` (or next to the executable) so users add presets without recompiling. Loader in `MainWindow_Presets.cpp`, keep the C++ table as fallback.

### State rewind — raffinements (MVP livré)

> **MVP livré** → `[CHANGELOG.md](CHANGELOG.md)` : ring de snapshots delta-encodés, panneau **CPU → State Rewind…** + bande timeline inline, état écran capturé, **desktop-only**. Pinned by `rewind_buffer_smoke`.

- [ ] **VRAM dirty-tracking for finer TMS9918 deltas** `[M · nice]` — the 16 KB VRAM section is chunk-diffed against the previous full blob each capture; a live VRAM dirty bitmap would cut the per-capture diff cost on graphics-heavy frames.
- [ ] **Seek cost on card-heavy presets** `[S · nice]` — `rewindSeekTo` reuses `loadSnapshotFromBuffer`, whose FLAGS dispatch re-applies card setters (may reload ROMs) every slider tick. Skip re-apply when the flag set is unchanged to keep dragging smooth.

---

## ⏸️ Deferred / conditional · 🚫 Blocked on external

> Spec connu, code tractable, mais conditionné à un déclencheur réel (logiciel exerçant la feature, demande utilisateur, hardware disponible). À promouvoir quand le déclencheur apparaît. **🚫 Blocked** = en attente d'une ressource externe hors de notre contrôle.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk` / `.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.
- [ ] **Joystick / paddle analogique (télémétrie)** `[déféré — hardware inexistant]` — **Décision 2026-06-16 : ne PAS implémenter.** Les paddles analogiques (`$C064`/`$C070` + timer 558) sont du hardware **Apple II**, pas Apple-1 — les modéliser émulerait une carte qui n'existe pas (règle « une vraie carte à la fois »). Côté télémétrie le digital est déjà couvert (FIFO `TELE_IN` + injection clavier `$D010`), et aucun logiciel Apple-1 réel n'utilise de paddle. À promouvoir seulement si une carte paddle Apple-1 réelle apparaît.
- [ ] 🚫 **Uncle Bernie's Improved ACI** `[M · solid]` — emulate the Extended firmware page at `$C500-$C5FF`: `R`/`W` with EOR checksum, `RX`/`WX` with 8-byte header at `$07F8-$07FF`, autostart when `<from>` == `<to>`. **En attente de Bernie (échange courriel 2026-06-24) : il a proposé d'envoyer le binaire de la page PROM** `$C5xx` **; manquent le binaire + la spec des points d'entrée.** (Statut antérieur « PROM jamais publiée » levé par ce contact direct.) Côté code, c'est à faible risque : `$C5xx` est libre (vérifié — pas de conflit GEN2, A9=0), suivre le patron `loadROM("ACI_ext.rom", 0xC500, 0x100, …)` + write-protect `$C500-$C5FF`, **en option/variante** (toggle « ACI stock Woz / Uncle Bernie GEN2 », comme le sélecteur A1-SID) pour préserver le test `aci_tape_loading_test`. Once obtained: load at `$C500`, extend `CassetteDevice` for header + checksum. Refs: [thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).