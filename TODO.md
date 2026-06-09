# TODO

Open work on the **emulator** only. Shipped features → `git log` / `README.md`. 6502 software work → `dev/TODO6502.md`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Sections ordered by actionability: implementable first, externally-blocked last.

---

## 🖥️ Uncle Bernie GEN2 — moteur faisceau (back-port POM2 → POM1)

> **Contexte** (AppleFritter, juin 2026) : la carte GEN2 *release* déplace les soft switches graphiques en **`$C250-$C257`** (`$C0` → `$C2` dans le code Apple II porté). Le vaporlock classique (`$C050` + bit-fumes bus flottant) **ne marche pas** avec l'ACI présent ; Bernie expose à la place un **drapeau MSB** sur lecture des soft switches `$C25x` pour détecter H-blank / V-blank. POM1 v1.8.6 ne modélise que le framebuffer passif `$2000-$3FFF` + rasterisation fin-de-frame MAME (`GraphicsCard.cpp`) — pas de faisceau cycle-accurate, pas de MMIO `$C2xx`.
>
> **Source POM2** (déjà fonctionnel) : `Memory::floatingBus()` (port MAME `apple2video.cpp:124-201`), journal d'événements vidéo horodaté (`VideoEvent` / `beginVideoEventFrame`), rendu beam-raced par bandes de scanlines (`Apple2Display::renderInternalBand`), signal composite OpenEmulator (`fillCompositeSignal` + `ColorCompositeOECpu` / shader GPU). Réf. `POM2/DEV.md` § Beam-racing, tests `floatingbus_page2_smoke`, `beam_race_composite`.
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
- [ ] **MMIO via `PeripheralBus`** `[M · critical]` — enregistrer `GEN2_softswitch` sur `$C250-$C257` (priorité > ACI toggle si lecture ; écriture = bascule miroir Apple II). Écriture : mettre à jour `DisplayState` + journaliser un `VideoEvent{cycle, kind, value}` (port du log POM2).
- [ ] **Lecture MSB blank (remplace vaporlock)** `[M · critical]` — sur lecture `$C25x` : `return (inHorizontalBlank() || inVerticalBlank()) ? 0x80 : 0x00` ORé avec `floatingBus() & 0x7F` selon spec Bernie (à affiner Phase 0). **Ne pas** réutiliser le chemin vaporlock `$C050` + bit-fumes seul — Bernie l'a explicitement abandonné avec ACI.
- [ ] **Helpers `inHorizontalBlank()` / `inVerticalBlank()`** `[M · solid]` — dérivés de `(h_clock, v_clock)` dans le compteur 65×262 ; calibrer sur le timing Apple II (active video h 25..64, VBL v ≥ 192 ou selon spec Bernie).
- [ ] **Test `gen2_softswitch_msb_smoke`** `[S · solid]` — poll `$C250` en boucle : MSB doit toggler à la cadence blank ; écriture `$C251` (SETGR) change le mode sans toggle TAPE OUT (ACI branchée, écriture `$C010` n'atteint plus les switches GEN2).

### Phase 3 — Beam-racing & rendu synchronisé au faisceau

- [ ] **Journal `VideoEvent` par frame** `[M · critical]` — `beginVideoFrame()` au rollover `cycleCounter` (front VBL ou ligne 0) ; `logVideoEvent(cycle, kind, value)` sur chaque écriture `$C25x` ; `takeVideoEvents()` consommé au rendu. Port de `POM2/src/Memory.{h,cpp}` (version allégée). ⚠️ **Stocker le cycle complet `(h_clock, v_clock)`, PAS seulement le scanline** : POM2 a gravé sa limite dans `VideoEvent.scanline` (`cycleCounter / kCyclesPerScanline` — la position horizontale est jetée), ce qui interdit définitivement les splits horizontaux. `Gen2VideoScanner::peekVideoCycle()` expose déjà le cycle ; le journal doit le **conserver**. **Granularité par cycle = objectif** (prérequis du split horizontal Bernie, cf. item dédié plus bas).
- [ ] **`renderBeamRacing()` dans `GraphicsCard`** `[L · critical]` — remplacer / compléter `rasterizeToBuffer()` : rejouer les événements par bandes de scanlines (`renderInternalBand`, port `POM2/src/Apple2Display.cpp`). ⚠️ **POM2 est scanline-quantizé** : `renderBeamRacing` (`Apple2Display.cpp:247`) trie par `ev.scanline` puis rend des bandes de lignes **pleine largeur, un seul mode** → **splits verticaux (entre scanlines) uniquement, jamais mid-ligne**. Premier renderer POM1 = même périmètre (bascule TEXT↔HGR aux bords de scanline). Conserver le fast-path « zéro événement » = rendu actuel MAME (régression zéro sur démos existantes `hgr_*`).
- [ ] **Modes d'affichage** `[M · solid]` — TEXT / GRAPHICS / MIXED : en TEXT ou MIXED bottom-4-rows, la fenêtre GEN2 n'affiche que la zone HGR (top 160 px) ; le texte reste sur l'écran Apple-1 `$D012` (dual-monitor Bernie). MIXED = bas 32 px texte Apple II sur terminal natif, pas sur GEN2.
- [ ] **Interférence bus GEN2 + ACI** `[M · solid]` — modéliser que les cycles où le scanner vidéo lit `$2000-$3FFF` exposent `floatingBus()` aux lectures `$C25x` ; quand l'ACI écrit `$C0xx` pendant le refresh, le comportement MSB / données doit rester cohérent avec le setup Bernie (cassette + graphique simultanés). Test scénario : `C100R` + jeu HGR actif, pas de clear TEXT accidentel.
- [ ] **Test `gen2_beam_race_smoke`** `[M · solid]` — port simplifié de `POM2/tests/beam_race_composite_test.cpp` : flip mode à la scanline 96, hash pixels bande haute ≠ bande basse.
- [ ] **🎯 Split horizontal — granularité par cycle (OBJECTIF VOULU)** `[L · solid]` — la fonctionnalité phare de Bernie : colonnes TEXT alternant avec colonnes LORES **sur la même scanline** (calage au cycle via le flag HBLANK ; « color peg » Codebreaker). Exige un renderer **per-octet** changeant de mode aux frontières de colonnes *dans* la ligne — au-delà du modèle scanline-quantizé de POM2. **Voulu aussi dans POM2** (cf. `POM2/DEV.md`) ; POM2 est le banc de preuve naturel car il rend déjà TEXT/LORES/HGR et n'a **aucune dépendance Bernie** (timing Apple II documenté MAME). **Plan : POM2 d'abord → back-port POM1.** Prérequis POM1 : (1) rendu LORES + TEXT sur la GEN2 (HGR seul aujourd'hui), (2) `renderInternalBand` à granularité colonne, (3) flag HBLANK Phase 2. **Ne PAS graver la limite scanline dans `VideoEvent` (cf. item Journal) — garder `(h,v)` dès le départ.**

### Phase 4 — Composite OpenEmulator (option rendu, pas bloquant MMIO)

- [ ] **Signal 14,318 MHz `signalBuf`** `[M · nice]` — port `Apple2Display::fillCompositeSignal()` : 560 échantillons × 192 lignes, bitstream HGR avec half-dot delay (réutiliser `buildHgrWordRow` existant). Beam-race le signal sur le même journal d'événements que Phase 3.
- [ ] **Chemin CPU `ColorCompositeOECpu`** `[M · nice]` — port `renderCompositeOeCpu()` + démodulateur FIR OpenEmulator (sans GLSL) pour WASM / fallback desktop. Menu GEN2 : « NTSC MAME (actuel) » vs « Composite OpenEmulator CPU » — calque le choix POM2 Graphics.
- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 si le shared texture layer (section Visuals) est en place ; sinon reporter.
- [ ] **Test parité MAME vs OE CPU** `[S · nice]` — même framebuffer ± quelques pixels de phase ; pas de régression sur `hgr_testcard`.

### Phase 5 — Intégration produit & validation Bernie

- [ ] **Preset 13 + CLI** `[S · solid]` — vérifier que preset GEN2 HGR active le nouveau chemin beam (flag implicite dès que soft switches branchés). Option debug `--gen2-beam-log` (trace événements / cycle blank).
- [ ] **Hardware Reference + tooltips** `[S · solid]` — `MainWindow_Dialogs.cpp` : map `$C250-$C257`, MSB blank, conflit ACI résolu, lien `doc/GEN2_RELEASE.md`. Remplacer la description « lecture passive $2000-$3FFF ».
- [ ] **Démo de validation Bernie** `[M · solid]` — binaire test `dev/projects/hgr_vsync/` (nouveau) : poll MSB `$C250`, flip PAGE à VBL, texte score sur `$D012` + animation HGR ; jeu cible portage pilote (Taipan ou Breakout HGR).
- [ ] **Mise à jour `CLAUDE.md` + README** `[S · nice]` — GEN2 passe de « passive framebuffer » à « beam-raced + `$C25x` MSB blank ».

### Dépendances / hors scope immédiat

- Vaporlock Apple II pur (`$C050` + bit-fumes sans MSB Bernie) — **hors scope** ; Bernie confirme non fonctionnel avec ACI.
- DHGR, 80 colonnes, AN3, Le Chat Mauve — Apple IIe uniquement, pas la GEN2 Apple-1.
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
