# TODO

Open work on the **emulator** only. Shipped features → `git log` / `README.md`. 6502 software work → `dev/TODO6502.md`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Sections ordered by actionability: implementable first, externally-blocked last.

---

## 🖥️ Uncle Bernie GEN2 — moteur faisceau (back-port POM2 → POM1)

> **Contexte** (AppleFritter, juin 2026) : la carte GEN2 *release* déplace les soft switches graphiques en **`$C250-$C257`** (`$C0` → `$C2` dans le code Apple II porté). Le vaporlock classique (`$C050` + bit-fumes bus flottant) **ne marche pas** avec l'ACI présent ; Bernie expose à la place un **drapeau MSB** sur lecture des soft switches `$C25x` pour détecter H-blank / V-blank. POM1 v1.8.6 ne modélise que le framebuffer passif `$2000-$3FFF` + rasterisation fin-de-frame MAME (`GraphicsCard.cpp`) — pas de faisceau cycle-accurate, pas de MMIO `$C2xx`.
>
> **Source POM2** (référence à jour juin 2026) : `Memory::floatingBus()` (port MAME `apple2video.cpp:124-201`), journal `VideoEvent{emuCycle, scanline, kind, value}` (`beginVideoEventFrame` / `takeVideoEvents`), décomposition beam **`forEachBeamSegment`** + `frameCycleToPos(emuCycle) → {scanline, byteCol}` (fenêtre visible : 40 octets, HBL cycles 0–24 → `byteCol=0`), rendu RGBA via `renderBeamRacing` → `renderInternalSegment` (splits **horizontaux mid-scanline** depuis 2026-06-09), signal composite 14,318 MHz via `fillCompositeSignal` (même décomposition que RGBA) + `ColorCompositeOECpu` / shader `NtscPostProcessor`. Réf. `POM2/DEV.md` § Beam-racing, `POM2/TODO.md` § Display. Tests : `floatingbus_page2_smoke`, `beam_race_composite`, `horizontal_split`, `horizontal_split_composite`, `horizontal_split_560` (560-wide IIe — hors scope POM1).
>
> **Tag global** `[L · critical]` — prérequis pour que Bernie et les ports de jeux Apple II HGR s'appuient sur la carte release, pas sur le prototype fil à fil.

### Phase 0 — Spécification hardware (bloquant externe léger)

- [ ] **Obtenir le map `$C250-$C257` release** `[S · critical]` — demander à Uncle Bernie le tableau bit-à-bit (TEXT / GRAPHICS / MIXED / PAGE1·2 / HIRES / FULL, sémantique MSB H-blank vs V-blank). Ne pas deviner depuis l'Apple II `$C05x` : le PCB release a des contraintes A6 distinctes. Bloque la Phase 2 tant que non confirmé. **Questions envoyées 2026-06-09 → `doc/GEN2_RELEASE_questions.md`** (8 points : map, read/write decode, MSB blank, timing, page 2, décodage `$C2xx`, coexistence ACI, état power-on) ; consigner les réponses dans ce fichier.
- [ ] **Documenter la règle portage jeux** `[S · solid]` — note `doc/GEN2_RELEASE.md` : patch `$C05x`→`$C25x`, conserver `$C030` seul pour SPEAKER, retirer les autres accès `$C0xx` des ports Apple II, dual-monitor (texte `$D012` + HGR GEN2).

### Phase 1 — Horloge vidéo + bus flottant (fondation cycle-accurate)

- [x] **Compteur de cycles vidéo global** `[M · critical]` — `Gen2VideoScanner` (`src/Gen2VideoScanner.{h,cpp}`) maintient `cycleCounter % (65×262)` avec les constantes NTSC verbatim POM2 (`kCyclesPerLine=65`, `kLinesPerFrame=262`, `kCyclesPerFrame=17030`). `peekVideoCycle()` exposé pour tests headless ; `Memory::peekGen2VideoCycle()` le relaie.
- [x] **Porter `floatingBus()`** `[M · critical]` — `Gen2VideoScanner::scannerAddress()` est le port verbatim de `POM2/src/Memory.cpp:floatingBus()` (formule MAME `scanner_address`), dépouillé des entrées IIe (80STORE, iieMode) ; le phantom row HBL texte `$1000` du II/II+ est conservé. `floatingBus(mem)` lit l'octet (HGR page 1 `$2000`, page 2 `$4000`). NB mirroring page 2 réel **non confirmé** (bloqué Phase 0) — la formule reste bit-exacte.
- [x] **Brancher `advanceCycles` sur la GEN2** `[S · solid]` — `Memory::advanceCycles()` appelle `gen2Scanner.advanceCycles(cycles)` quand `hgrFramebufferAttached` (même pattern que TMS9918 / SID / cassette ; le scanner est possédé par `Memory`, pas `GraphicsCard` qui appartient à l'UI). Phase remise à zéro au branchement à froid.
- [x] **Test `gen2_floatingbus_smoke`** `[S · solid]` — oracle adresse scanner (adresses calculées à la main : HGR p1/p2 `$2068`/`$4068` lead-in, `$2000`/`$4000` premier octet, texte `$0400` + phantom `$1468`, mixed bottom-4 `$0650`) + lecture octet à sentinelles + wrap compteur. `tests/gen2_floatingbus_smoke_test.cpp`, auto-contenu (motif `peripheral_bus_smoke`).

### Phase 2 — Soft switches `$C250-$C257` + drapeau MSB Bernie

- [ ] **`DisplayState` GEN2** `[S · critical]` — struct minimale : `textMode`, `mixedMode`, `page2`, `hiRes` (+ état power-on : TEXT, PAGE1, lo-res off). Port de `POM2/src/Memory.h::DisplayState`, sans les bits IIe (80COL, DHGR, 80STORE, AN3).
- [ ] **MMIO via `PeripheralBus`** `[M · critical]` — enregistrer `GEN2_softswitch` sur `$C250-$C257` (priorité > ACI toggle si lecture ; écriture = bascule miroir Apple II). Écriture : mettre à jour `DisplayState` + journaliser un `VideoEvent{emuCycle, scanline, kind, value}` (modèle POM2 `Memory::pushVideoEventLocked` — `emuCycle` = cycle CPU + cycles instruction en cours ; `scanline` dérivé pour le tri, position horizontale recalculée via `frameCycleToPos`).
- [ ] **Lecture MSB blank (remplace vaporlock)** `[M · critical]` — sur lecture `$C25x` : `return (inHorizontalBlank() || inVerticalBlank()) ? 0x80 : 0x00` ORé avec `floatingBus() & 0x7F` selon spec Bernie (à affiner Phase 0). **Ne pas** réutiliser le chemin vaporlock `$C050` + bit-fumes seul — Bernie l'a explicitement abandonné avec ACI.
- [ ] **Helpers `inHorizontalBlank()` / `inVerticalBlank()`** `[M · solid]` — dérivés de `(h_clock, v_clock)` dans le compteur 65×262 ; calibrer sur le timing Apple II (active video h 25..64, VBL v ≥ 192 ou selon spec Bernie).
- [ ] **Test `gen2_softswitch_msb_smoke`** `[S · solid]` — poll `$C250` en boucle : MSB doit toggler à la cadence blank ; écriture `$C251` (SETGR) change le mode sans toggle TAPE OUT (ACI branchée, écriture `$C010` n'atteint plus les switches GEN2).

### Phase 3 — Beam-racing & rendu synchronisé au faisceau

> **Audit POM2 (2026-06-10).** Le cœur beam-racing POM2 est **fait** (splits horizontaux mid-scanline inclus, 2026-06-09). Le back-port POM1 cible le trio **`VideoEvent.emuCycle` + `frameCycleToPos` + `forEachBeamSegment`** (`POM2/src/Apple2Display.cpp`), pas l'ancien modèle scanline-only. POM1 n'a besoin que du sous-ensemble **280-wide** (TEXT / LORES / HGR) — ignorer le path 560-wide IIe (`horizontal_split_560`, save/restore `frame80`).

- [ ] **Journal `VideoEvent` par frame** `[M · critical]` — `beginVideoEventFrame()` au début du budget CPU émulé (miroir POM2 `EmulationController`, pas seulement au rollover VBL) ; `pushVideoEventLocked` sur chaque écriture `$C25x` avec **`emuCycle`** (cycle monotonic + cycles instruction en cours) + `scanline` dérivé pour le tri ; `takeVideoEvents()` consommé au rendu. Modèle POM2 `Memory.h:273` — **`emuCycle` est la source de vérité** ; la position horizontale se recalcule via `frameCycleToPos`, le champ `scanline` sert au tri rapide. Ne pas réinventer un journal scanline-only.
- [ ] **`frameCycleToPos` + `forEachBeamSegment`** `[M · critical]` — porter l'abstraction partagée POM2 : tri stable (scanline, `byteCol`) ; par ligne visible, segments `[col0,col1)` + `DisplayState` ; fusion verticale des lignes à segmentation identique ; callback `paint(state, y0, y1, col0, col1)`. `byteCol = clamp((emuCycle % 65) − 25, 0, 40)`. NTSC seul (pas PAL — GEN2 Apple-1). C'est le prérequis unique avant tout rendu beam-raced.
- [ ] **`renderBeamRacing()` dans `GraphicsCard`** `[L · critical]` — remplacer / compléter `rasterizeToBuffer()` : `forEachBeamSegment` → `renderInternalSegment` (port POM2). **HGR** : décode toute la scanline (fenêtre artefact NTSC 7 bits — contexte voisin), clippe l'écriture à `[col0,col1)` (identique POM2 `renderHiRes`). Fast-path log vide → `rasterizeToBuffer()` actuel (régression zéro sur `hgr_*`). v1 : HGR seul ; TEXT/LORES suivent l'item Modes ci-dessous.
- [ ] **Modes d'affichage** `[M · solid]` — TEXT / GRAPHICS / MIXED : en TEXT ou MIXED bottom-4-rows, la fenêtre GEN2 n'affiche que la zone HGR (top 160 px) ; le texte reste sur l'écran Apple-1 `$D012` (dual-monitor Bernie). MIXED = bas 32 px texte Apple II sur terminal natif, pas sur GEN2. **Prérequis split horizontal Bernie** : rendu TEXT + LORES sur la GEN2 (HGR seul aujourd'hui) — POM2 a déjà `renderText` / `renderLoRes` avec bornes `col0,col1`.
- [ ] **Interférence bus GEN2 + ACI** `[M · solid]` — modéliser que les cycles où le scanner vidéo lit `$2000-$3FFF` exposent `floatingBus()` aux lectures `$C25x` ; quand l'ACI écrit `$C0xx` pendant le refresh, le comportement MSB / données doit rester cohérent avec le setup Bernie (cassette + graphique simultanés). Test scénario : `C100R` + jeu HGR actif, pas de clear TEXT accidentel.
- [ ] **Test `gen2_beam_race_smoke`** `[M · solid]` — split **vertical** : flip mode à la scanline 96, hash pixels bande haute ≠ bande basse (port `POM2/tests/beam_race_composite_test.cpp`, LUT seul en v1).
- [ ] **🎯 Split horizontal mid-scanline** `[L · solid]` — **fait côté POM2** (2026-06-09, épinglé `horizontal_split` + `horizontal_split_composite`) ; **à back-porter**. Fonctionnalité phare Bernie : colonnes TEXT alternant avec colonnes LORES **sur la même scanline** (calage au cycle via flag HBLANK ; « color peg » Codebreaker). Port POM1 : adapter `horizontal_split_smoke` — flip `$C250/$C251` (GEN2) à `byteCol 20` chaque scanline ; gauche HGR == réf HGR, droite TEXT == réf TEXT, même ligne. Prérequis POM1 : Phase 2 (MSB blank) + rendu TEXT/LORES GEN2 + `forEachBeamSegment`. **Raffinement documenté POM2** : cycle exact au character-clock = v2 ; v1 = frontière colonne d'octet.

### Phase 4 — Composite OpenEmulator (option rendu, pas bloquant MMIO)

> POM2 : `fillCompositeSignal` partage **`forEachBeamSegment`** avec le path RGBA depuis 2026-06-09 — splits horizontaux visibles en OE GPU/CPU + AppleWin, pas seulement LUT. Limites POM2 à ne pas recopier aveuglément : `signalPhaseOffset_` = une constante par frame ; lo-res clip au block-row (4 scanlines). GEN2 POM1 : pas de DHGR ni 560-wide → points 1 et scope 560 hors scope.

- [ ] **Signal 14,318 MHz `signalBuf`** `[M · nice]` — port `Apple2Display::fillCompositeSignal()` : 560 échantillons × 192 lignes ; `paintHgr` construit la ligne entière puis écrit `[col0·14, col1·14)` (réutiliser `buildHgrWordRow` / bitstream POM1). Même `forEachBeamSegment` que Phase 3 — zéro divergence RGBA/signal.
- [ ] **Chemin CPU `ColorCompositeOECpu`** `[M · nice]` — port `renderCompositeOeCpu()` + démodulateur FIR OpenEmulator (sans GLSL) pour WASM / fallback desktop. Menu GEN2 : « NTSC MAME (actuel) » vs « Composite OpenEmulator CPU » — calque le choix POM2 Graphics. **Non bloquant** : le LUT MAME actuel (`GraphicsCard`) reste le fast-path v1.
- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 si le shared texture layer (section Visuals) est en place ; sinon reporter.
- [ ] **Test parité MAME vs OE CPU** `[S · nice]` — même framebuffer ± quelques pixels de phase ; pas de régression sur `hgr_testcard`. Après splits horizontaux : port `horizontal_split_composite` (signal waveform gauche HGR / droite TEXT).

### Phase 5 — Intégration produit & validation Bernie

- [ ] **Preset 13 + CLI** `[S · solid]` — vérifier que preset GEN2 HGR active le nouveau chemin beam (flag implicite dès que soft switches branchés). Option debug `--gen2-beam-log` (trace événements / cycle blank).
- [ ] **Hardware Reference + tooltips** `[S · solid]` — `MainWindow_Dialogs.cpp` : map `$C250-$C257`, MSB blank, conflit ACI résolu, lien `doc/GEN2_RELEASE.md`. Remplacer la description « lecture passive $2000-$3FFF ».
- [ ] **Démo de validation Bernie** `[M · solid]` — binaire test `dev/projects/hgr_vsync/` (nouveau) : poll MSB `$C250`, flip PAGE à VBL, texte score sur `$D012` + animation HGR ; jeu cible portage pilote (Taipan ou Breakout HGR).
- [ ] **Mise à jour `CLAUDE.md` + README** `[S · nice]` — GEN2 passe de « passive framebuffer » à « beam-raced + `$C25x` MSB blank ».

### Dépendances / hors scope immédiat

- Vaporlock Apple II pur (`$C050` + bit-fumes sans MSB Bernie) — **hors scope** ; Bernie confirme non fonctionnel avec ACI.
- DHGR, 80 colonnes, AN3, Le Chat Mauve, path 560-wide (`horizontal_split_560`, save/restore `frame80`) — Apple IIe uniquement, pas la GEN2 Apple-1.
- Timing PAL 50 Hz — fait POM2 (profils `iie-pal` / `iic-pal`) ; GEN2 Apple-1 = NTSC seul.
- Speaker `$C030` — déjà dans la plage ACI `$C000-$C0FF` ; documenter pour les ports, pas de nouveau hardware.

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
