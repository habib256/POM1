# TODO

Open work on the **emulator** only. Shipped features → `git log` / `README.md`. 6502 software work → `dev/TODO6502.md`.

**Tags** `[effort · impact]` — effort: **S** (<1d) / **M** (1–5d) / **L** (>5d or architectural). Impact: **nice** / **solid** / **critical**.

Sections ordered by actionability: implementable first, externally-blocked last.

---

## ⏪ State rewind (microM8-style timeline)

> **MVP shipped** (`RewindBuffer` + in-memory `SnapshotWriter`/`Reader` + **CPU → State Rewind…** scrub panel). Delta-encoded ring of snapshot blobs (section + 256 B chunk deltas, keyframe-anchored segments, budget eviction). Pinned by `rewind_buffer_smoke`. See CLAUDE.md › Emulation orchestration.

- [x] **MVP: timeline scrub + delta ring** `[M · solid]` — slider preview (pause + restore), Resume here (truncate future), Back to live; ~4 captures/s; 128 MB budget.
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
> **✅ Statut 2026-06-12 : Phases 0-3 livrées** (soft switches `$C25x` read-only + HST0, journal d'événements, rendu beam-raced TEXT/LORES/HIRES/MIXED/PAGE2, splits verticaux **et** horizontaux mid-scanline). Épinglé par `gen2_floatingbus_smoke`, `gen2_softswitch_msb_smoke`, `gen2_beam_race_smoke`, `gen2_horizontal_split_smoke`. Restent : Phase 4 (composite OE, optionnelle) + Phase 5 (démo de validation pour Bernie, `--gen2-beam-log`).

### Phase 0 — Spécification hardware ✅ RÉSOLUE 2026-06-12

- [x] **Obtenir le map `$C250-$C257` release** `[S · critical]` — **RÉSOLU 2026-06-12** : Bernie a répondu point par point (PM AppleFritter #6) **et** envoyé la référence bit-exacte `doc/ColorGraphicsCard_doc_for_Arnaud.pdf` — transcription complète + statut Q1-Q10 (tous résolus) dans `doc/GEN2_RELEASE_questions.md`. Décisions clés : switches **read-only** (la lecture bascule + retourne HST0 en D7 ; l'écriture est un no-op), HST0 = 1 en H/V-blank avec un trou à 0 pendant le color burst (3 cycles, `hcnt` 13-15), HIRES page 2 `$4000-$5FFF` (`$C255`), décode `SEL = $Cxxx & !A11 & A9 & A4` (miroirs `$C2/$C3/$C6/$C7xx`), power-on indéterminé (init logicielle obligatoire, RESET Apple-1 sans effet), 65 cyc/ligne × 262 @60 Hz / 312 @50 Hz. **Tous les modes fonctionnent sur le vrai matériel — y compris le prototype de Bernie** (TEXT 40×25 minuscules, LORES 40×50/16 couleurs, HIRES 280×192, MIXED) ; le « HIRES seul » des premiers posts est obsolète — le HGR-only de POM1 est un manque émulateur, pas une parité hardware. Phase 2 débloquée — retirer les marqueurs `// SPEC PENDING BERNIE`.
- [x] **Documenter la règle portage jeux** `[S · solid]` — **`doc/GEN2_RELEASE.md`** (le « SDK Bernie », 2026-06-13) : map `$C25x` read-only, recettes HST0 (Listing 1, détection VBL, splits mid-scanline + caveat color-killer), table de portage `$C05x`→`$C25x` (SPEAKER `$C030` conservé via TAPE OUT ACI, clavier `$C000`→PIA `$D010/11`, autres `$C0xx` neutralisés), dual-monitor, RAM 48 K (Q9), boucle de dev POM1 (CLI/telnet/screenshot/`--save-tape`, caveat refresh DRAM + `INC zp`). **Lib réutilisable `dev/lib/gen2/`** : `gen2.inc` (équates + cheat-sheet HST0) + `gen2_sync.asm` (`gen2_waitvbl` grossier + `gen2_beam_lock` exact → retour à (ligne 0, hcnt 55) ±0, asserts de page ld65, shim configurable) — extraction de la synchro prouvée dans `dev/projects/a1_crazycycle/`.

### Phase 1 — Horloge vidéo + bus flottant (fondation cycle-accurate)

- [x] **Compteur de cycles vidéo global** `[M · critical]` — `Gen2VideoScanner` (`src/Gen2VideoScanner.{h,cpp}`) maintient `cycleCounter % (65×262)` avec les constantes NTSC verbatim POM2 (`kCyclesPerLine=65`, `kLinesPerFrame=262`, `kCyclesPerFrame=17030`). `peekVideoCycle()` exposé pour tests headless ; `Memory::peekGen2VideoCycle()` le relaie.
- [x] **Porter `floatingBus()`** `[M · critical]` — `Gen2VideoScanner::scannerAddress()` est le port verbatim de `POM2/src/Memory.cpp:floatingBus()` (formule MAME `scanner_address`), dépouillé des entrées IIe (80STORE, iieMode) ; le phantom row HBL texte `$1000` du II/II+ est conservé. `floatingBus(mem)` lit l'octet (HGR page 1 `$2000`, page 2 `$4000`). Page 2 **confirmée** (Q5 résolu) : HIRES page 2 = `$4000-$5FFF`, sélection `$C255` — identique Apple II.
- [x] **Brancher `advanceCycles` sur la GEN2** `[S · solid]` — `Memory::advanceCycles()` appelle `gen2Scanner.advanceCycles(cycles)` quand `hgrFramebufferAttached` (même pattern que TMS9918 / SID / cassette ; le scanner est possédé par `Memory`, pas `GraphicsCard` qui appartient à l'UI). Phase remise à zéro au branchement à froid.
- [x] **Test `gen2_floatingbus_smoke`** `[S · solid]` — oracle adresse scanner (adresses calculées à la main : HGR p1/p2 `$2068`/`$4068` lead-in, `$2000`/`$4000` premier octet, texte `$0400` + phantom `$1468`, mixed bottom-4 `$0650`) + lecture octet à sentinelles + wrap compteur. `tests/gen2_floatingbus_smoke_test.cpp`, auto-contenu (motif `peripheral_bus_smoke`).

### Phase 2 — Soft switches `$C250-$C257` + drapeau MSB Bernie ✅ LIVRÉE 2026-06-12

- [x] **`DisplayState` GEN2** `[S · critical]` — `Gen2VideoScanner::DisplayState` (`textMode`, `mixedMode`, `page2`, `hiRes` + `operator==`). Cold-state documenté **GRAPHICS+HIRES+PAGE1** (arbitraire conforme Q8 — garde les logiciels HGR pré-Phase-2 affichés sans séquence d'init) ; **jamais modifié par le RESET Apple-1** (épinglé par le test).
- [x] **MMIO via `PeripheralBus`** `[M · critical]` — handle `GEN2_softswitch` sur `$C200-$C7FF`, décode complet `SEL = $Cxxx & !A11 & A9 & A4` dans le handler (miroirs `$C2/$C3/$C6/$C7xx` A4=1 ; adresses non décodées = fall-through RAM mimé). Lecture = toggle + journal `Gen2VideoScanner::Event{emuCycle, kind, value}` (`emuCycle` = compteur scanner + `M6502::getCurrentInstructionCycles()`, nouveau getter — la position raster se re-dérive au rendu, pas de champ scanline stocké) ; écriture décodée = no-op bloquée. `PeripheralBus::EntryMask` élargi uint16→uint32 (17ᵉ entrée du bus).
- [x] **Lecture HST0 (remplace vaporlock)** `[M · critical]` — `Gen2VideoScanner::hst0State(line, hcnt)` verbatim PDF (ordre burst→VBL→live préservé — le notch burst gagne même en VBL) ; lecture `$C25x` → `(HST0<<7) | (xorshift32 & 0x7F)` (bits bas = bruit déterministe, recommandation explicite de Bernie).
- [x] **Helpers blank** `[M · solid]` — `hst0At(absCycle)` / `hst0()` sur le compteur 65×N ; jumper **50/60 Hz** exposé (`Memory::setGen2FiftyHz` + façade `EmulationController`, checkbox fenêtre HGR, persisté dans la section snapshot `GEN2VID`).
- [x] **Test `gen2_softswitch_msb_smoke`** `[S · solid]` — bornes hst0 (HBL/burst/live/VBL/50 Hz), toggle par lecture, écriture bloquée (latch + RAM), miroirs `$C350/$C675/$C77C` décodés vs `$C455`/`$C243` restés RAM, coexistence ACI (Q7), publication du journal au rollover de frame, RESET sans effet sur le latch.

### Phase 3 — Beam-racing & rendu synchronisé au faisceau ✅ LIVRÉE 2026-06-12

> **Audit POM2 (2026-06-10).** Le cœur beam-racing POM2 est **fait** (splits horizontaux mid-scanline inclus, 2026-06-09). Le back-port POM1 cible le trio **`VideoEvent.emuCycle` + `frameCycleToPos` + `forEachBeamSegment`** (`POM2/src/Apple2Display.cpp`), pas l'ancien modèle scanline-only. POM1 n'a besoin que du sous-ensemble **280-wide** (TEXT / LORES / HGR) — ignorer le path 560-wide IIe (`horizontal_split_560`, save/restore `frame80`).

- [x] **Journal `VideoEvent` par frame** `[M · critical]` — journal double dans `Memory` : `gen2RecordingEvents` (frame en cours) + `gen2PublishedEvents` (dernière frame complète) ; bascule au **rollover de frame vidéo** dans `advanceCycles()` (modèle POM2 « Memory republishes at each video-frame boundary » — l'UI peut re-rendre la même frame à 60 Hz sans consommation destructive). `emuCycle` = source de vérité (compteur scanner + cycles in-flight CPU) ; **pas de champ scanline stocké** — re-dérivé via `frameCycleToPos` au tri ET au rendu (le recorder et le replay ne peuvent pas diverger). Cap 4096 événements/frame (saturation → collapse vers l'état courant = fast-path). Publication UI : `EmulationSnapshot.gen2{Enabled, FiftyHz, DisplayState, FrameStartState, VideoEvents}` copiés par `SnapshotPublisher`.
- [x] **`frameCycleToPos` + `forEachBeamSegment`** `[M · critical]` — portés verbatim dans `GraphicsCard` (tri stable (scanline, byteCol), segments `[col0,col1)` par ligne, fusion verticale, callback `paint`, heuristique page-flip une-direction = frame-wide). `byteCol = clamp((emuCycle % 65) − 25, 0, 40)` ; paramètre `linesPerFrame` 262/312 (le jumper 50 Hz de la carte — pas du PAL couleur).
- [x] **`renderBeamRacing()` dans `GraphicsCard`** `[L · critical]` — `GraphicsCard::render(memory, endState, frameStart, events, linesPerFrame)` : journal vide + latch GRAPHICS+HIRES+PAGE1 → `rasterizeToBuffer()` d'origine (diff par ligne, régression zéro) ; sinon repaint complet via `forEachBeamSegment` → `renderInternalSegment`. **HGR** : décode toute la scanline, clippe l'écriture à `[px0,px1)` (`rasterizeHgrLine`).
- [x] **Modes d'affichage** `[M · solid]` — TEXT 40×24 **B&W** (fonte 5×7 intégrée portée de POM2 — l'EPROM 2716 de Bernie n'est pas dumpée ; attributs inverse/flash Apple II, fallback minuscules→majuscules), LORES 40×48 16 couleurs (palette MAME partagée avec la LUT HGR), MIXED = 4 dernières rangées TEXT **sur la sortie GEN2** (`$C253`), PAGE2 TEXT `$0800` / HIRES `$4000-$5FFF`. Carve-out OOR strict étendu à `$2000-$5FFF` (la carte amène sa DRAM derrière les deux pages HGR, Q9). État des switches + jumper 50 Hz affichés dans la fenêtre HGR.
- [x] **Interférence bus GEN2 + ACI** `[M · solid]` — **résolu par la spec (Q7)** : *« ACI will co-exist with the graphics card with no side effects and no clashes »* — aucun modèle d'interférence à écrire. Bits 0-6 d'une lecture `$C25x` = bruit (recommandation PDF) plutôt que le vrai octet du bus flottant ; le bus flottant bit-exact reste accessible via `Memory::gen2FloatingBus()`. Coexistence ACI épinglée dans `gen2_softswitch_msb_smoke`.
- [x] **Test `gen2_beam_race_smoke`** `[M · solid]` — rendus de mode (TEXT/LORES/HIRES/MIXED/PAGE2) + split **vertical** TEXT_ON à la scanline 96 (haut == réf HGR, bas == réf TEXT pixel-exact), heuristique page-flip une-direction, split page 1/2/1 bi-directionnel, événement VBL sans effet visible. Auto-contenu (GraphicsCard + Gen2VideoScanner).
- [x] **🎯 Split horizontal mid-scanline** `[L · solid]` — back-porté ; épinglé par `gen2_horizontal_split_smoke` : flip TEXT_ON à `byteCol 20` → gauche HGR == réf HGR, droite TEXT == réf TEXT **sur la même ligne** ; motif « color peg » répété sur une bande ; invariant contexte-artefact-NTSC au byte frontière (décode pleine ligne, write-back clippé). v1 = frontière colonne d'octet (même scope que POM2) ; cycle exact intra character-clock = v2.

### Phase 4 — Composite OpenEmulator (option rendu, pas bloquant MMIO)

> POM2 : `fillCompositeSignal` partage **`forEachBeamSegment`** avec le path RGBA depuis 2026-06-09 — splits horizontaux visibles en OE GPU/CPU + AppleWin, pas seulement LUT. Limites POM2 à ne pas recopier aveuglément : `signalPhaseOffset_` = une constante par frame ; lo-res clip au block-row (4 scanlines). GEN2 POM1 : pas de DHGR ni 560-wide → points 1 et scope 560 hors scope.

- [ ] **Signal 14,318 MHz `signalBuf`** `[M · nice]` — port `Apple2Display::fillCompositeSignal()` : 560 échantillons × 192 lignes ; `paintHgr` construit la ligne entière puis écrit `[col0·14, col1·14)` (réutiliser `buildHgrWordRow` / bitstream POM1). Même `forEachBeamSegment` que Phase 3 — zéro divergence RGBA/signal.
- [ ] **Chemin CPU `ColorCompositeOECpu`** `[M · nice]` — port `renderCompositeOeCpu()` + démodulateur FIR OpenEmulator (sans GLSL) pour WASM / fallback desktop. Menu GEN2 : « NTSC MAME (actuel) » vs « Composite OpenEmulator CPU » — calque le choix POM2 Graphics. **Non bloquant** : le LUT MAME actuel (`GraphicsCard`) reste le fast-path v1.
- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 si le shared texture layer (section Visuals) est en place ; sinon reporter.
- [ ] **Test parité MAME vs OE CPU** `[S · nice]` — même framebuffer ± quelques pixels de phase ; pas de régression sur `hgr_testcard`. Après splits horizontaux : port `horizontal_split_composite` (signal waveform gauche HGR / droite TEXT).

### Phase 5 — Intégration produit & validation Bernie

- [x] **Preset 13** `[S · solid]` — le preset GEN2 HGR plugge la carte via `setHgrFramebufferAttached(true)`, qui active désormais implicitement le handler `$C25x` + scanner + chemin beam (aucun flag séparé). Reste : option debug `--gen2-beam-log` (trace événements / cycle blank) si le besoin émerge pendant la validation Bernie.
- [x] **Hardware Reference + tooltips** `[S · solid]` — `MainWindow_Dialogs.cpp` : map `$C250-$C257` (read-only + HST0 + miroirs), notch burst, pages 1/2, power-on indéterminé, note portage `$C05x`→`$C25x`, timing 50/60 Hz ; memory map à jour (`$C250-$C257`, page 2 `$4000-$5FFF`). Tooltip latch live dans la fenêtre HGR.
- [x] **Démo de validation Bernie** `[M · solid]` — **`dev/projects/a1_crazycycle/`** (→ `software/Graphic HGR/A-1-CrazyCycle.{bin,txt}`, `E000R`) : init du latch par lectures (Q8), page TEXT « Uncle Bernie HGR COLOR CARD » répétée, mire TV couleur HGR (5 barres + barres inversées + damier), puis **fenêtre TEXT 112×64 beam-raced au milieu de la mire** — sync HST0 cycle-exacte en 3 étages (WAITVBL double-échantillonné anti-notch-burst, scan de phase ±0 cycle par glissement 66 cycles, verrouillage ligne 0) puis free-run 17030 cycles/frame, splits horizontaux `$C251`/`$C250` aux colonnes 12/28 de chaque ligne 64-127. Branches chronométrées verrouillées même-page par `.assert` ld65. Validé visuellement dans POM1 (3 phases capturées). Reste optionnel : portage pilote d'un jeu Apple II HGR (Taipan / Breakout).
- [x] **Mise à jour `CLAUDE.md`** `[S · nice]` — GEN2 documentée « beam-raced + `$C25x` HST0 » (sections GraphicsCard / Gen2VideoScanner / memory map / tests). README : la table presets n'a pas bougé ; ajouter un mot sur les soft switches au prochain passage README.

### Dépendances / hors scope immédiat

- Vaporlock Apple II pur (`$C050` + bit-fumes sans MSB Bernie) — **hors scope** ; Bernie confirme non fonctionnel avec ACI.
- DHGR, 80 colonnes, AN3, Le Chat Mauve, path 560-wide (`horizontal_split_560`, save/restore `frame80`) — Apple IIe uniquement, pas la GEN2 Apple-1.
- Timing PAL 50 Hz — fait POM2 (profils `iie-pal` / `iic-pal`) ; GEN2 Apple-1 = **NTSC couleur seul**, mais le jumper vertical 50/60 Hz de la carte (262/312 lignes, Q4) est exposé (checkbox fenêtre HGR + `setGen2FiftyHz`).
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
