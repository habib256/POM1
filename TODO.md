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

## 🎮 Automated game testing — telemetry side channel

> **Contexte** (Uncle Bernie, juin 2026) : le chaînon manquant de son « SDK rêvé » (cc65/ca65 → émulateur → tests de régression auto). Pour tester des **jeux d'action temps réel**, il faut un *side channel* où le jeu 6502 expose son état et où un harnais externe lit cet état, décide les entrées (clavier/joystick) et les renvoie. C'est la généralisation binaire/bidirectionnelle du pont déjà fait par la Terminal Card (`$D012 → TCP → inject $D010`).
>
> **Note de conception : [`doc/TELEMETRY_SIDE_CHANNEL.md`](doc/TELEMETRY_SIDE_CHANNEL.md).** **✅ Fonctionnellement complet → [`CHANGELOG.md`](CHANGELOG.md)** : `TelemetryPort` (`$C440-$C443`, FIFO in/out, serveur TCP, `KeyInjector`), bus priorité 30 (zone A9=0 aveugle à GEN2), `--telemetry-port N`, **lock-step déterministe** (parke le CPU via `M6502::stop()` jusqu'à l'ACK `0x06`, vérifié end-to-end + `tools/test_telemetry_lockstep.py`), `--telemetry-log PATH` (golden-trace), panneau UI (*View → Telemetry Side Channel…*), **`--headless`** (sans GLFW/display, **presets/cartes appliqués** : `--preset 13` plugge GEN2 — vérifié `tools/test_headless_preset.py`). La boucle « régression automatique » du SDK de Bernie est **complète côté émulateur** ; l'umbrella restant est **CI GitHub Actions** (section Technical debt, ci-dessous). **Kit SDK livré** : lib cc65 `dev/lib/telemetry/telemetry.inc` + lib harnais Python `tools/pom1_telemetry.py` + démo `dev/projects/a1_telemetry_demo/` & worked-example `tools/test_telemetry_demo.py` (boucle compile→load→test vérifiée headless). Restes ci-dessous (le reste du « dream SDK » = **CI GitHub Actions** + **CPU cycle-exact**, déjà suivis en Technical debt).

- [ ] **Entrée joystick / paddle analogique** `[M · nice]` — Bernie dit « keyboard *or joystick* ». La FIFO `TELE_IN` + l'injection clavier (`$D010`, via Terminal Card) couvrent le digital ; pas de paddle analogique (`$C064`-style) modélisé. Nécessaire pour les jeux à paddle.

---

## 🛠️ POM1 Bench — IDE in-app façon Arduino (front-end auteur du SDK)

> **Contexte** (juin 2026) : le « SDK rêvé » de Bernie (cc65/ca65 → émulateur → test) existe côté outils (port `$C440-$C443` + lib cc65 `dev/lib/telemetry/telemetry.inc` + harnais `tools/pom1_telemetry.py` + démo `dev/projects/a1_telemetry_demo/`), mais il vit en ligne de commande / `dev/`. Objectif : matérialiser la **boucle d'auteur** *à l'intérieur de POM1*, **dans le style de l'IDE Arduino** — fenêtre éditeur + boutons Verify/Upload + un **Serial Monitor qui EST la télémétrie**. C'est le pendant interactif du CI (section Technical debt) : le CI est l'assurance-régression batch ; ceci est l'établi. **Desktop-first** — le shell-out `ca65`/`ld65` est impossible en WASM, donc les boutons de compilation sont masqués sur le web (l'éditeur + le Serial Monitor restent dispo). `[L · solid]`.
>
> **Mapping Arduino → POM1** : sketch → source 6502/cc65 · *Board* → config `.cfg` cc65 (`apple1_4k` / `apple1_gen2` / `telemetry` …) · *Verify* (✓) → `ca65`+`ld65` → `.bin`, erreurs au panneau · *Upload* (→) → `loadHexDump`/`loadBinary` + reset/run · *Serial Monitor* → TX `$C440` (hex+ASCII) + ligne d'entrée → RX `$C442`, toggle lock-step · *console* → `stderr` toolchain, clic erreur → ligne.

### Phase A — Serial Monitor (= télémétrie) — *indépendant du toolchain, marche aussi en WASM* — ✅ LIVRÉE (2026-06-14)

> Améliore la fenêtre **Telemetry Side Channel** existante (aujourd'hui un panneau de *stats*) en vrai moniteur série. Réutilise 100 % de la plomberie `$C440-$C443`. Tient debout seule, avant tout éditeur. Build vert, lock-step + démo headless toujours verts (`test_telemetry_lockstep.py`, `test_telemetry_demo.py`).

- [x] **Tap TX + moniteur hex/ASCII** `[S · solid]` — `TelemetryPort` accumule les octets *wire* (`AA <len16> payload`) dans `txMonitorRing` (cap 8 KB) + `txMonitorTotal` ; tap dans `endFrame()` *avant* le drop backpressure (le moniteur voit toutes les frames). Transporté via `Snapshot.txTap`/`txTotal` ; l'UI append le delta `txTotal - lastSeen` (survit à l'écrasement du slot SPSC). Vue scrollable hex+ASCII, autoscroll, *Clear*, toggle Hex/Texte.
- [x] **Injection RX depuis l'UI** `[S · solid]` — ligne d'entrée → `TelemetryPort::injectInbound` (drop-oldest à `kMaxInBytes`), exposée via `EmulationController::telemetryInject` (calque de `setTelemetryEnabled`, sous `stateMutex`). Modes ASCII / Hex (`"06 41 0D"`).
- [x] **Bouton *Step frame* (lock-step)** `[S · nice]` — quand le jeu a parké la frame (`awaitingAck`), le bouton relâche exactement une image via `EmulationController::telemetryReleaseFrame` → `clearAwaitingAck`. Avance image par image sans harnais Python.
- [x] **Champ *Log to file*** `[S · nice]` — câble `setTelemetryLogFile` sur un champ chemin pour capturer la golden-trace depuis l'UI (même flux que `--telemetry-log`).

### Phase B — Éditeur + Upload (sans compilation) — ✅ LIVRÉE (2026-06-14)

> Fenêtre **POM1 Bench** (*Settings → POM1 Bench (sketch editor)…*). Le sketch « Blink » par défaut (`0300: A9 A1 20 EF FF 4C 00 03` + `0300R`, imprime `!` via WozMon ECHO) charge bien 8 o @ $0300 par le chemin `loadHexDump` (vérifié headless `--load`).

- [x] **Fenêtre éditeur** `[M · solid]` — `InputTextMultiline` sur buffer `char` 64 KB fixe (`imgui_stdlib.cpp` **pas** dans le build — choix : pas de modif CMake pour le MVP ; passer à `std::string`+resize-callback en Phase D avec ImGuiColorTextEdit). Open/Save par champ chemin (`ifstream`/`ofstream`). Entrée menu *Settings*.
- [x] **Upload (hex/brut)** `[S · solid]` — bouton *Upload* + combo de mode : **Hex (Wozmon)** → tee vers `temp/pom1_bench_sketch.txt` → `EmulationController::loadHexDump` ; **Raw bytes @ $** → `parseHexTokens` (factorisé, partagé avec le Serial Monitor) → `temp/.bin` → `loadBinary` à l'adresse du champ. Les deux loaders font déjà stop→reset-vectors→hardReset→start→run. Statut « Uploaded N B — running @ $XXXX ».

### Phase C — Verify / Upload via cc65 (desktop only) — ✅ LIVRÉE (2026-06-14, sauf clic-erreur)

> Toolbar cc65 dans **POM1 Bench** (desktop). Flux validé bout-en-bout : `ca65`/`ld65` + cfg embarqué produisent le « Blink » (`A9 A1 20 EF FF 4C 00 03`) et `loadBinary` le lance @ $0300 — identique au chemin hex. Tout sous `#if !POM1_IS_WASM` (WASM affiche « desktop-only »).

- [x] **Détection toolchain + dropdown *Board*** `[M · solid]` — `whichExe` scanne `$PATH` (+ `/usr/local/bin`, `/opt/cc65/bin`) pour `ca65`/`ld65` ; hint « install cc65 » si absent. *Board* = cfg embarqué (« Apple-1 4K @ $0300 (built-in) », marche sans `dev/`) + scan de `dev/cc65/*.cfg` ; `-I` auto pour chaque `dev/lib/*` (résout `.include`). Sondé une fois (`detectBenchToolchain`).
- [x] **Verify (✓) + panneau de sortie** `[M · solid]` — `runCapture` (popen, `2>&1`) lance `ca65 <libs> src.s -o src.o` puis `ld65 -C <board> src.o -o src.bin` en dossier temp ; sortie dans le panneau **Build output (cc65)** sous l'éditeur. **Verify** = assemble seul ; **Build & Run** = + `loadBinary` à l'adresse `start` du cfg (`parseCfgLoadAddr` sur la zone `file = %O`, fallback champ `$`). `shellQuote` POSIX/Windows.
- [x] **Clic-erreur → ligne** `[S · nice]` — ✅ (Phase D / ImGuiColorTextEdit). `parseBenchErrorMarkers` lit `src(<line>): Error: …` du `stderr` ca65 → `TextEditor::SetErrorMarkers` (marqueur + tooltip dans la gouttière) ; les lignes d'erreur de la console **Build output** sont cliquables → `SetCursorPosition` saute à la ligne dans l'éditeur.

### Phase D — Finitions

- [x] **Habillage IDE Arduino** `[M · solid]` — ✅ (2026-06-14, réf. utilisateur nitrathor.fr/fiches/ide-arduino). Barre d'outils **teal `#00979D`** avec boutons ronds **Verify (✓)** / **Upload (→)** + **New/Open/Save** + **Serial Monitor (loupe)** aligné à droite (ouvre la fenêtre télémétrie) ; sélecteur **Source** (cc65 asm / Wozmon hex / Raw) + **Board** ; **onglet sketch** ; **éditeur crème** (fond clair, texte sombre) ; **console sombre** (lignes `error`/`failed` en orange) ; **barre d'état teal** avec « <board> on POM1 » à droite. Icônes FontAwesome (atlas full-solid mergé). Le starter par défaut suit le mode (asm si cc65 présent, sinon hex).
- [x] **Coloration syntaxique 6502** `[M · nice]` — ✅ (2026-06-14). **ImGuiColorTextEdit vendoré** (BalazsJako, MIT, `src/third_party/ImGuiColorTextEdit/` + LICENSE, ajouté au CMake) ; compile contre l'ImGui 1.92.7 du repo. `build6502LangDef()` : mnémoniques NMOS en mots-clés, directives `.xxx`, nombres `$hex`/`%bin`/déc, commentaires `;`, palette claire. Remplace `InputTextMultiline` ; les 7 regex validées hors-GUI. Boutons toolbar = **icône + anneau dessiné par-dessus** (`AddCircle`), pas de disque plein.
- [x] **Sketches d'exemple intégrés** `[S · solid]` — ✅ menu **Examples** (bouton livre de la toolbar → popup) : *Blink (cc65 asm)*, *Blink (Wozmon hex)*, *Hello world (C/TMS9918)*, *A-1-CrazyCycle (GEN2 HGR demo)*, *Telemetry demo*. Les exemples fichier (CrazyCycle/Telemetry) lisent `dev/projects/…` et sélectionnent la board adaptée (CrazyCycle → `apple1_gen2.cfg`). Donne un foyer in-app au kit SDK.
- [x] **Cibles « Target = machine + build »** `[M · solid]` — ✅ (2026-06-14). Le dropdown « Board » (cfg linker seul) est remplacé par un **Target** qui *bundle* : **preset POM1** (`applyMachineConfig` → cartes + RAM + dual-bank), **cfg linker cc65**, **mode source**, et un **asset compagnon** optionnel. Sélectionner une cible **plugge la machine** sur laquelle le programme tourne. Table `kBenchTargets[]` : *Built-in 4K asm* · *Apple-1 4K (preset 1)* · **Uncle Bernie GEN2 HGR+ACI (preset 13)** · *TMS9918 C (preset 8)* · *Wozmon hex* · *Raw*. CrazyCycle → preset 13 (`$E000` RAM, `$2000/$4000` framebuffers, ACI) + `apple1_gen2.cfg` + **asset UBERNIE stagé à `$2000`** via `loadBinaryToRam` avant le run (ld65 ne produit pas l'image ; RLE abandonné — ratio 69 % + non vérifiable). Remplace l'auto-plug ad-hoc des cartes.
- [x] **Cible « TMS9918 CodeTank ROM (C) »** `[M · solid]` — ✅ (2026-06-14). Sur TMS9918, le C est **toujours une ROM CodeTank** : `cl65 -t none -Oirs -C dev/apple1-videocard-lib/cc65/codetank_c.cfg -I lib main.c apple1_asm.s tms9918.c screen1.c c64font.c` → image **16 K @ $4000**. Upload : pad 16 K → **32 K** (banque basse + $FF haut, taille exacte de `CodeTank::loadRomFile`) → `loadCodeTankRom` → jumper `Lower16` → `hardReset` → `codeTankPendingWozRunAt` (**boot 4000R**), exactement la séquence de la CodeTank Library. Preset 8 (TMS9918+CodeTank) via la cible. Détection `cl65` + lib + `codetank_c.cfg` dans `detectBenchToolchain`. Build+pad validés (16384→32768 o). *(L'ancien cfg bas-RAM $0300 est abandonné — le flux canonique TMS9918 est la cartouche CodeTank.)*
- [x] **Barre d'état non coupée** `[S]` — ✅ réserve de hauteur recalculée via métriques ImGui (`GetFrameHeightWithSpacing` pour le champ chemin + bloc console + barre d'état teal).
- [ ] **WASM cc65 (stretch)** `[L · nice]` — cc65 compilé en WASM pour offrir Verify/Upload sur le web aussi ; gros chantier, optionnel — la Phase A/B reste la valeur de base sans lui.

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

- [ ] **CI GitHub Actions** `[S · solid]` *(audit 2026-05-31)* — `.github/workflows/` n'existe pas ; les 24 tests `ctest` (Klaus Dormann inclus) ne tournent jamais automatiquement. Workflow `ctest` sur push/PR (Linux, idéalement + macOS) + build de vérification WASM (`emcmake`). **Plus fort ROI du dépôt.** **Débloqué** : `--headless` existe maintenant → le workflow peut aussi piloter `tools/test_telemetry_lockstep.py` (et futurs golden-traces) sans display.
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
