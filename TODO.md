# TODO

Open work on the **emulator** only. Shipped work → `[CHANGELOG.md](CHANGELOG.md)` / `git log` · user tour → `[README.md](README.md)` · 6502 software → `[dev/TODO6502.md](dev/TODO6502.md)`.

## Conventions

- **One item = one checkbox** — `- [ ] **Title** [effort · impact] — detail`.
- **Tags** `[effort · impact]` — effort: **S** (<1 d) · **M** (1–5 d) · **L** (>5 d / architectural). Impact: **nice** · **solid** · **critical**.
- `> blockquote` at the head of a (sub)section = context / what already shipped.
- Grouped by subsystem; **deferred / externally-blocked last**. Only open items live here — shipped work is lifted to `[CHANGELOG.md](CHANGELOG.md)`.
- 🚫 prefix = blocked on an external resource.



## Contents

- [🎨 Graphics](#-graphics) — GEN2 beam engine · TMS9918 beam/CPU sync
- [🛠️ Dev tooling](#-dev-tooling) — POM1 Bench · BASIC · LOGO · DevBench editor
- [🔌 Peripherals & loaders](#-peripherals--loaders) — serial loaders · optional cards
- [🖼️ Visuals & UX](#-visuals--ux) — CRT fidelity
- [🔧 Infra & technical debt](#-infra--technical-debt) — packaging / distribution / démarrage · architecture refactors · snapshot / scripting · state rewind
- [⏸️ Deferred · 🚫 Blocked](#-deferred--blocked)

---



## 🎨 Graphics



### GEN2 beam engine — Phase 4: composite OpenEmulator (rendu optionnel, non bloquant)

> Phases 0-3 + 5 + **chemin composite CPU (**`RenderMode::CompositeOECpu`**) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. **Le composite OpenEmulator est désormais le rendu par défaut de l'app** (`gen2RenderMode=1`) ; le LUT MAME reste dispo via le menu GEN2 (et reste le défaut de la `GraphicsCard` standalone, pour la golden image). Reste seulement le chemin GPU-shader, optionnel :

- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — optionnel : porter `NtscPostProcessor` POM2 (même noyaux FIR + matrice que le chemin CPU déjà livré) si le *Shared video texture layer* (livré) est exploité ; le CPU couvre déjà WASM + desktop, donc **reportable** tant qu'aucun besoin de perf n'apparaît.
  - **Conclusion : défer jusqu'à un besoin perf concret — zéro gain visuel, purement « où tournent les calculs ».** Le décodage NTSC (`GraphicsCard.cpp` : FIR 17 taps luma+chroma + démod sin/cos + matrice YUV→RGB) tourne sur CPU dans un buffer 280×192 minuscule (~3-4 M MAC/frame) → invisible dans un profil desktop. Le porter déplace **les mêmes maths** vers un fragment shader : même image byte-identique (épinglée `hgr_convert_smoke`), donc **aucune capacité visuelle ajoutée**. Ne le faire QUE si (1) on ajoute du post-traitement lourd plein écran (courbure CRT, bloom, scanlines shader, phosphore par pixel, NTSC à résolution interne >280) qui rend le CPU goulot, OU (2) un profil montre la démod comme coût réel (GPU/CPU faible, très haut refresh). Coût du porting : 2 chemins à garder byte-identiques + shader à décliner GLSL/MSL/WebGL (dont le patch sampler Metal délicat). Prérequis unique déjà livré (*shared video texture layer*), donc reportable **sans dette** — le jour venu, le port est direct.



### TMS9918 — synchro beam/CPU sub-scanline (mid-line splits + statut au tick)

> **Étapes 0-2 + socle** `BeamClock` **+ corrections silicium (fidélité init, pacing pad18, statut F+C, plancher 9c) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. L'axe entrée CPU→VRAM est tick-accurate ; l'axe sortie a été rapproché. Reste ouvert :

- [ ] **Fetch sprite SAT « une ligne en avance » sous-ligne** `[M · solid]` — line n doit afficher les sprites fetchés pendant n-1 (le latch seamless mode/blank est livré ; reste la latence de fetch SAT exacte). Effet visible seulement sur écritures SAT mid-active (rares) → à valider sur silicium (Parmigiani) avant de modéliser la latence.
- [ ] **Journal VideoEvents + renderer par rejeu, adoption GEN2** `[L · solid]` — remplacer le rattrapage *eager* (Étapes 0-2) par un journal `(cycle,kind,value)` per-cycle (h,v) composé par **rejeu** entre sync points (découplage total + rewind-friendly). GEN2 a déjà ce journal (`gen2RecordingEvents`, `Memory.cpp`). **Livré (juillet 2026) : le journal GEN2 entre désormais dans le snapshot / rewind** (section `GEN2VID` v5 sérialise le journal publié + son frame-start ; `snapshot_smoke`) → `[CHANGELOG.md](CHANGELOG.md)`. Reste ouvert : (a) **généraliser** le journal en facilité partagée (hors `Memory`) + faire adopter `BeamGeometry`/`beamPosAt` (`src/BeamClock.h`) par le rejeu GEN2 (aujourd'hui géométrie GEN2-privée dans `GraphicsCard::frameCycleToPos`) ; (b) **faire adopter le journal+rejeu par le TMS9918** (remplacer `renderBeamCatchUp`/`syncSpriteScanToBeam` eager + sérialiser son journal comme GEN2). Objectif cycle-granularity commun POM1/POM2.

---



## 🛠️ Dev tooling



### BASIC dans le Bench

> **Injection (Integer + Applesoft, 4 cibles), coloration, tokeniseurs, compilateur natif → 6502 (**`3DHat.apf`**/**`RodColor.apf` **autonomes sur GEN2 + TMS), sélecteur *Inject | Compile*, Verify-charge-prêt-à-**`LIST` **+ toggle cold/warm livrés** → `[CHANGELOG.md](CHANGELOG.md)`. Reste ouvert :

- [ ] **Variables chaîne (**`A$`**) dans le compilateur natif** `[L · nice]` — aujourd'hui le lexer rejette tout identifiant suivi de `$` (`BasicCompilerApplesoft.cpp`, « string variables need a later phase ») ; seuls les littéraux chaîne de `PRINT` existent. Chantier transverse : descripteurs (ptr+len) comme classe de variable parallèle à `V_`/`_I`, une région heap découpée dans `basicc_native.cfg`, un `basicrt_string.s` (alloc/copie/concat + `LEN`/`MID$`/`CHR$`/`STR$`), et un chemin d'expression *typé chaîne* dans le lexer/`expr` (tout est numérique aujourd'hui). Touche lexer + parser + codegen + runtime + cfg linker.
- [ ] **Tier float compact (binary16 / virgule fixe) pour coords bornées** `[L · nice]` — la largeur (2 vs 4) est abstraite par `vw()`/`W`, mais ~15 sites d'émission codent le binary32 en dur (`fpLoadConst`, `fpNeg`, `emitIfFalse`, signe FOR, tous les `jsr fp_`*). Demande : une nouvelle valeur `FpMode`/format sur `Codegen`, des helpers d'émission parallèles, un runtime `basicrt_fixed.s` (`fx_add/fx_mul/fx_div/fx_cmp/…`), un jeu de symboles + gating `-D` dédié, un dimensionnement linker, et une 3ᵉ branche dans la sélection de phase (`compile()`). Utile seulement quand la précision binary32 est superflue (jeux/anim à coords bornées).



### LOGO dans le Bench

> **Interpréteur V2.6 + injection (**`injectLogo` **/** `LogoProgramLoader`**) + 10 sketches** `sketchs/logo/` **+ REPL interactif (send / écho / historique ↑↓ / Break Ctrl-G) livrés** → `[CHANGELOG.md](CHANGELOG.md)`. LOGO est le **4ᵉ langage** du *New*, deux cibles (TMS9918 `4000R`, GEN2 HGR `6000R`), WASM-safe, pin `bench_logo_inject_smoke`. Reste ouvert (nice-to-have) :

- [ ] **Livre d'exemples LOGO dans le popup *Examples*** `[S · solid]` — les 10 `.logo` de `sketchs/logo/` existent (et sont préchargés MEMFS côté web) mais ne sont atteignables que par *File → Load* ; les câbler dans `kP1Examples[]`/`examples_` (groupe « LOGO », ouverture 1-clic) comme les exemples asm/C, pour la découvrabilité.

---



## 🔌 Peripherals & loaders

- [ ] **flowenol apple1-serial bootloader** `[S · solid]` — [https://github.com/flowenol/apple1-serial](https://github.com/flowenol/apple1-serial) — serial-port bootloader / terminal (complements TurboType / 8BitFlux). Pipes through Terminal Card or its own ACIA variant; likely a text-format loader on top of `Memory::loadHexDump` + paste pipeline.
- [ ] 🚫 **TurboType 57 600-baud loader** `[M · solid]` — **En attente de Bernie (échange courriel 2026-06-24) : spec détaillée + une ROM/binaire du dropper nécessaires avant implémentation.** Uncle Bernie's format, shipped by 8BitFlux *Keyboard Serial Terminal* (ATtiny + 11 MHz xtal + MAX232 + 74LS244). Protocol: Wozmon-speed bootstrap (200 ms/newline, 20 ms/char) installs an in-RAM dropper that **skips** `$D012` **echoes** and streams bytes at 57.6 kbps with running CRC, sentinel + CRC verify, jump to entry. Loads 4 KB in <30 s vs ~2 400 baud Wozmon. POM1 side: parse `.TUR`/`.APL`, switch Terminal Card to raw-8-bit + echo-suppressed inject (`Ctrl-T` already gives 8-bit; no-echo is new), verify CRC, surrender to Wozmon. *Note émulateur :* `loadHexDump` *gère déjà le multi-blocs + les marqueurs* `T`*/*`X`*, et charge instantanément — TurboType n'a de valeur que pour l'authenticité/démo du protocole, pas pour la vitesse de chargement.*
- [ ] **Briel Multi I/O — SpeakJet** `[M · nice]` — 6522 / 6551 blocks duplicate microSD / MODEM; the unique value is piping the UART byte stream through a TTS bridge (eSpeak, macOS `say`) to give the Apple-1 a voice. Ship as a separate optional peripheral so it coexists with microSD.

---



## 🖼️ Visuals & UX

> POM1 a déjà la meilleure UX du duo POM1/POM2 (126 tooltips, 15 tutoriels, boot scénographié, 0 ROM à fournir). **Native file dialogs, shared video texture layer, backend Metal macOS livrés** → `[CHANGELOG.md](CHANGELOG.md)`. Frictions résiduelles *(audit designer 2026-05-31)* :

- [ ] **1976 CRT fidelity (opt-in, default off)** `[M · nice]` — two sub-effects under the existing CRT toggle:
  1. **Shift-register streaming** `[S · nice]` (Signetics 2519 timing) — chars land ~60 / s, hardware scroll shifts buffer one line at a time, display freezes during CPU bursts. Pair with the bare-4K preset.
  2. **Shift-register dot noise** `[S · nice]` (2504 / 2513 clock) — periodic static, **not random** — ~40 × 3 sub-cells per char, 1-px horizontal phase drift row-to-row, last row shorter. New `drawShiftRegisterNoise()` after backdrop pass, deterministic nested loop, `alpha ≈ crtScanlineAlpha * 0.25`, tinted with `phosphorTint`.

---



## 🔧 Infra & technical debt

### Packaging, distribution & démarrage

> **Livré** → `[CHANGELOG.md](CHANGELOG.md)` : l'**exe Windows autonome** (CRT + GLFW statiques, zéro DLL, issue #34), le **pin de GLFW via `vcpkg.json`** (`builtin-baseline` vcpkg `2026.06.24` + `overrides` → glfw3 3.4#1 ; les deux sites d'appel passent en mode manifeste, sans argument de paquet), et la **borne Raspberry Pi refaite sur le modèle NeoST** (`packaging/raspberrypi/` : X nu + service systemd + `--fullscreen`, `--audio-latency`, `build_native_pi.sh --pgo`, cascade GLSL 150→140→130 + contexte 3.2→3.0), et la **bascule de GitHub Pages sur le déploiement CI** (22 août 2026 : *Settings → Pages → Source = GitHub Actions*, `pages.yml` bâtit et publie le bundle à la même URL ; `POM1.{data,wasm,js}` ne sont plus versionnés, `.git` retombé de 813 Mo à 252 Mo).

- [ ] **Chargement paresseux des assets WASM — reste `cassettes/`** `[S · nice]` — **la moitié `pic/` est livrée (24 août 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : seuls `icon.png` + le logo du magnéto restent préchargés, le reste arrive en HTTP à l'ouverture de la fenêtre (`ensurePicFetched` dans `src/MainWindow_Dialogs.cpp`, site servi par `tools/assemble_wasm_site.sh`) — le premier pixel passe de 14 à ~7 Mo. Reste `cassettes/` (2,5 Mo sur le fil, dont le `WOZ_talk.mp3` qui ne sert qu'au preset Fantasy) pour descendre vers 5 Mo — plus délicat : `CassetteDevice` charge le fichier au moment de l'application du preset, pas à l'ouverture d'une fenêtre.
  - **Ne pas faire `cfcard.po`** (correction du 22 août 2026) : fichier **creux** — 33 553 920 octets logiques, 800 Ko réels, 0,3 Mo gzip — le chantier décrit (fetch asynchrone + état « image en cours de chargement », `Memory` ouvrant l'image au démarrage) est délicat pour 2 % du gain.
- [ ] **Rejouer la borne Pi sur un Pi réel** `[S · solid]` — les scripts `packaging/raspberrypi/` sont portés de NeoST et validés en local (compilation GLES de bout en bout, cascade GLSL forcée sous llvmpipe, helpers `config.txt`/`cmdline.txt` testés en bac à sable), mais **jamais exécutés sur un Pi**. À vérifier sur place : `[CRT] GLSL …` dans `journalctl -u pom1-kiosk@pi`, absence de craquement audio à `POM1_AUDIO_LATENCY=120`, plein écran sans WM, et `--uninstall` qui rend bien `config.txt`/`cmdline.txt`.
- [ ] **CI borne Pi (artefact `cortex-a72` avec PGO)** `[M · nice]` — NeoST entraîne son PGO sur un runner ARM64 (`pi-borne.yml`) pour éviter ~1 h de compilation sur le Pi ; POM1 n'a que l'AppImage aarch64 générique du job de release.

### Solidité (audit août 2026)

> **Livré** → `[CHANGELOG.md](CHANGELOG.md)` : la bibliothèque d'objets `pom1_core` (1435 → 355 compilations), `-Wall -Wextra` + `-DPOM1_WERROR=ON` en CI, les jobs build-only macOS/Windows, le job sanitizer nocturne (`-DPOM1_SANITIZE=`, timeouts ×5), `src/LockOrder.h` + `lock_order_smoke`, `tools/check_doc_paths.py` + `doc_paths_sync`, le binaire sous test passé à `test_lib_micro.py`, et `pic/` divisé par deux. **22 août 2026** : les trois cassages que cette passe avait elle-même introduits sur les plateformes qu'aucun job par-push ne compile ou n'exécute (WASM `deltaTime`, `RC1106` sur `POM1.rc`, `C2589` MSVC dans `RewindBuffer.cpp`) — d'où les quatre premières cases ci-dessous, qui ferment le quadrant plutôt que les trois bugs.

> **La forme du trou, mesurée ce jour-là** : Linux compilait *et* testait ; macOS et Windows compilaient sans jamais tester ; le WASM n'était bâti que par `Deploy Pages`, donc **après** le merge sur `main` ; les sanitizers sont nocturnes. Les trois cassages ont atterri dans exactement ce quadrant, et il a fallu deux allers-retours de CI pour les voir, faute de pouvoir les reproduire en local.

> **Quadrant refermé (22-23 août 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : le **job `wasm`** compile le bundle à chaque push (`ci.yml`), **`ctest` tourne sur les trois bureaux** (macOS `ci.yml` + Windows `-C Release`), **`--help` existe** avec `cli_flags_sync` pour l'épingler, et les **flags de compilation sont partagés** par une cible `INTERFACE pom1_build_flags` que lient l'app, `pom1_core`, `basicc` et les 50 cibles de test — la même cible porte le `GL_SILENCE_DEPRECATION` d'Apple, qui était recopié 50 fois dans `tests/CMakeLists.txt` (2 182 → 2 046 lignes). **23 août 2026** : `crt_params_sync` (`tools/check_crt_params.py`) épingle enfin la pile CRT — 13 réglages × 3 copies, dans les deux sens, y compris « déclaré mais jamais alimenté », et vérifié par mutation. **24 août 2026** : le **smoke navigateur** ferme le trou entre « le build passe » et « le site marche » — `tools/wasm_smoke.mjs` (Playwright headless, auto-prouvé par `--self-test`) charge l'assemblage exact que `pages.yml` déploie (`tools/assemble_wasm_site.sh`, partagé), à chaque push (`ci.yml` job `wasm`) et en porte du déploiement (`pages.yml`). Restent ci-dessous les items qui n'ont pas encore de garde.

- [ ] **Fuzzer les chargeurs de fichiers** `[M · solid]` — WOZMON hex, Intel HEX, TurboType `.TUR`, l'AIFF écrit à la main, `.d64`, les images de snapshot : les seules portes d'entrée de données non maîtrisées. Chacun a ses gardes, et chaque garde a été ajoutée **après** un bug — la couverture est donc empirique, pas systématique. Le cas le plus exposé est l'AIFF (POM1 le lit lui-même, miniaudio n'ayant pas de backend : flottant 80 bits décodé à la main, quatre largeurs PCM). Une cible libFuzzer par parseur, amorcée par le corpus déjà présent dans `software/`, `cassettes/`, `sdcard/`, sous ASan. Chaque plantage trouvé devient un cas de test à coût nul.
- [ ] **Filet de crash + bundle de diagnostic** `[M · solid]` — **la moitié journal est livrée** (août 2026 : `FileLogger` + `logs/pom1.log`, troisième enfant du `TeeLogger` — voir `[CHANGELOG.md](CHANGELOG.md)`), ce qui était le vrai préalable : la case supposait un journal à empaqueter et il n'en existait aucun. Reste le filet lui-même. Seuls `SIGINT`/`SIGTERM` sont interceptés (pour vider la cassette). Un `SIGSEGV` emporte le journal, la disposition des fenêtres pas encore autosauvegardée et tout le tampon de rewind, et l'utilisateur n'a rien à envoyer. Pour un logiciel maintenu par une personne et distribué à des amateurs sur quatre plateformes, c'est le maillon manquant du support. Gestionnaire de dernier recours écrivant trace d'appels + tampon de journal dans `crash/`, plus *Aide → Signaler un problème* qui assemble journal, snapshot, `ini/` et versions dans un zip. Aucune télémétrie : c'est l'utilisateur qui décide d'envoyer.
- [ ] **`-Werror` sur Windows** `[S · nice]` — **macOS livré (23 août 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : l'arbre mesuré sous AppleClang 17 (39 avertissements, 10 sites) a été nettoyé et le job `macos` passe `-DPOM1_WERROR=ON`. Reste MSVC : le job `windows` compile en `/W4` sans `/WX`. **Relevé du 22 août 2026** : huit `C4244` (`int` → `float`) sur `src/sidtrack/SidTrackerEditor.cpp:342-343` et un `C4701` dans le `stb_vorbis.c` vendu — une passe de nettoyage précède l'activation, et le vendu devra rester hors du périmètre `/WX` comme il l'est déjà de `/W4`. À mesurer sur une machine Windows, pas à corriger à l'aveugle.
- [ ] **Étendre le rang des verrous aux mutex des cartes** `[S · nice]` — `LockOrder.h` couvre les trois verrous du cœur. `SID::chipMutex`, `TerminalCard::cardMutex` / `screenshotResultMutex` et les verrous du modem restent hors table ; leur donner un rang (sous `Snapshot`, ou dans une bande dédiée aux périphériques) étendrait la vérification au seul endroit où il reste des verrous non ordonnés. **Ne couvre pas la classe de défaut corrigée dans `CassetteDevice` en août 2026** : un rang vérifie l'ORDRE, pas la DURÉE de détention ni une allocation sur le thread temps-réel — ni `LockOrder.h` ni TSan ne voient celle-là, seule une lecture du chemin d'appel la trouve.

### Refactors architecturaux (audit juillet 2026)

> Issus d'une revue architecturale transversale. Le cœur (CPU/Memory/bus) est propre ; la dette se concentre dans le fan-out « ajouter une carte » et les god objects UI.
>
> **Livré (août 2026 — passe « god files »)** → `[CHANGELOG.md](CHANGELOG.md)` : le **découplage `CliDispatcher` → `MainWindow_ImGui`** (table de presets sortie vers `MachinePresets.{h,cpp}`, sans UI ; le 5ᵉ trou de tests de juillet 2026 est comblé — **`cli_dispatcher_smoke`**, dont l'assertion réelle est qu'il *linke*), et **cinq god files découpés en code motion pur** (jeu de méthodes / lignes de code prouvé identique à chaque fois, 90/90 tests verts) : `Pom1BenchHost.cpp` 3957 → 2229 (+`_Lang` 571, `Targets` 456, `Cc65` 789), `MainWindow_Dialogs.cpp` 3559 → 1807 (+`_Settings` 594, `_Tutorials` 1227), `MainWindow_HardwareWindows.cpp` 2530 → 1752 (+`_SiliconStrict` 818), `EmulationController.cpp` 2143 → 867 (+3 TU), `MainWindow_Presets.cpp` 2740 → 2327. Le plus gros fichier propre restant est `Memory.cpp` (2532 l., déjà allégé en juillet) : **plus rien au-dessus de 2 600 lignes, contre quatre fichiers >2 500 et deux >3 500 avant la passe.**
>
> **Livré (juillet 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : le **registry de cartes unique** (`Memory::cardSlots()` — les 4 listes hand-synced de `Memory.cpp` + la 5ᵉ que portait `snapshot_smoke` effondrées en une table ordonnée ; unicité-8-octets prouvée à la compilation ; ordre des sections épinglé) ; l'**extraction du snapshot I/O** vers `MemorySnapshot.cpp` (`Memory.cpp` : 2569 → 2157 l.) et le `RomLoadPolicy` nommé ; la **fuite d'include** `EmulationController → imgui` ; le **table-driving des fenêtres photo** ; **4 des 5 trous de tests** (Disassembler6502, A1IO_RTC, TerminalCard, WiFiModem — 74 → 78 tests).

- [ ] **Débogage au niveau source — étendre aux cibles C et au WASM** `[M · solid]` — **le MVP asm/desktop est livré (24 août 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : `ca65 -g` + `ld65 --dbgfile`, parseur pur `src/DbgFile.cpp` (`dbgfile_smoke` + `bench_cc65_smoke`, qui épingle enfin `Pom1BenchCc65.cpp`), point d'arrêt source depuis la toolbar du Bench, curseur qui suit le PC au pas-à-pas, étiquettes du programme versées d'office dans le désassembleur. Reste : **les cibles C** (cl65 accepte `-g` mais `--dbgfile` doit traverser `-Wl` dans `benchCSpecLinkCmd`/`benchCSpecCl65Cmd`, et la table de lignes C passe par les `.dbg` du code généré) ; **le WASM** (le cc65-en-navigateur ne passe ni `-g` ni `--dbgfile` — même chantier côté `cc65_bench.js`) ; et des **points d'arrêt multiples** (le MVP s'aligne sur l'unique breakpoint CPU de la machine ; en gérer N demande soit un jeu de breakpoints dans `M6502`, soit un multiplexage réarmé au vol).
> **Livré (23 août 2026 — fan-out d'en-têtes)** → `[CHANGELOG.md](CHANGELOG.md)` : `Memory.h` ne tire plus les onze en-têtes de cartes qu'il avait accumulés. Aucun n'était structurel — `std::unique_ptr<T>` n'exige `T` complet qu'au destructeur, et `~Memory()` est hors-ligne depuis longtemps ; les getters inline d'une ligne ne l'exigent pas non plus (`TMS9918` le prouvait déjà, forward-déclaré avec ses getters inline). Sept passent en déclaration anticipée ; `JukeBox.h` / `CodeTank.h` restent (types **imbriqués** dans des signatures) et `Gen2VideoScanner.h` aussi (membre par valeur). **Mesuré avant : `touch src/JukeBox.h` recompilait 105 TU, 5 min 21 s de CPU** pour un en-tête que 15 TU nomment — et `D64Image.h`, atteint via `IECCard.h` → `Drive1541.h`, entrait dans ~37 TU pour en servir 3. Rayon d'impact de la bascule : **7 TU** à qui il a fallu nommer l'en-tête qu'ils utilisaient déjà (4 dans `src/`, 3 tests).

- [ ] **Nœud de fan-out suivant : `EmulationSnapshot.h`** `[S · nice]` — la passe du 23 août 2026 a libéré `Memory.h`, mais `JukeBox.h` et `CodeTank.h` ne sont retombés que de 105 à **57 TU** (contre 105 → ~20 pour les sept autres cartes) : ils restent tirés par `EmulationSnapshot.h`, lui-même très largement inclus, qui a besoin de `JukeBox::Snapshot` / `CodeTank::Snapshot` — des structs **imbriqués** par valeur, donc le même blocage que les enums, un cran plus haut. Quatre autres en-têtes (`MachinePresets.h`, `CliDispatcher.h`, `MemoryViewer_ImGui.h`, `MainWindow_ImGui.h`) ne se servent en revanche que des trois enums et peuvent basculer sur `[src/CardTypes.h](src/CardTypes.h)` seul. Même remède qu'au tour précédent : sortir les deux `Snapshot` à portée namespace, alias membres conservés pour ne toucher aucun site d'appel.

> **Matrice headless des 13 presets livrée (23 août 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : `--exit-after-cycles N` + `tools/test_headless_presets.py` (ctest **`headless_preset_matrix`**, 3 s pour les 13). C'était le prérequis des deux cases suivantes.

- [ ] **Étendre la matrice aux combinaisons de cartes** `[S · solid]` — la matrice boote les presets tels que livrés ; les combinaisons que le mode strict *autorise* (`--enable`/`--disable` par-dessus un preset, `wouldCreateConflict` côté UI) et celles qu'il refuse ne sont pas parcourues. Même harnais, un second axe : pour chaque preset, chaque carte absente que `gateStrictPlug` accepterait, boot + Monitor. Et une assertion plus forte que « PC dans le Monitor » là où une ROM de carte a un prompt (SD CARD OS, CFFA1, Krusader) : un `--paste` + capture du flux `$D012` via `--telemetry-log`.
- [ ] **TSan ne voit jamais le thread de rendu** `[M · solid]` — corollaire du précédent, et angle mort structurel : le job sanitizer nocturne lance `ctest`, or **seuls 7 des 94 fichiers de test instancient `EmulationController`** et aucun ne fait tourner d'UI. La seule paire de threads qui existe chez un utilisateur — *thread de rendu × thread d'émulation* — n'est donc jamais instrumentée. La discipline est pourtant bonne (audit du 23 août 2026 : **0 méthode sur 207 ne touche `memory->`/`cpu->` sans `stateMutex`**), mais c'est une propriété vérifiée à la lecture, pas par une machine. Le harnais headless ci-dessus est le préalable.
- [ ] **Épingler les modules décrits comme purs** `[S · solid]` — 26 modules de `src/` n'apparaissent dans aucune cible de test, et presque tous sont de l'UI. **Recompté août 2026 : 42 des 105 `.cpp` de `src/` n'apparaissent dans aucune cible de test.** `Pom1BenchCc65.cpp` **est épinglé depuis le 24 août 2026** (`bench_cc65_smoke` — linke sans UI, part A sur les micro-parseurs purs, `Pom1BenchTargets.cpp` linké avec). Restent hors UI et testables : la table `ConflictRule` de `MainWindow_SiliconStrict.cpp`, qui est de la politique de bus pure et demande une extraction préalable. Les autres candidats plausibles ne le sont pas : `HgrImageDecode.cpp` / `TmsImageDecode.cpp` sont des TU d'implémentation stb_image sans en-tête, `bench/Markdown.cpp` porte 75 appels `ImGui::`, et `bench/BenchLang.h` inclut `TextEditor.h`. Règle à retenir de la passe précédente : **quand un module est décrit comme pur dans la doc, il devrait avoir un test qui le prouve.**
- [ ] **Décomposer les panneaux en objets** `[L · solid]` — **le registre de panneaux est livré (23 août 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : `WindowDescriptor` porte désormais `render` / `gate` / `desktopOnly` / `dock` en plus de la clé, du titre, du flag et de la catégorie, et il est la **liste unique** des 68 fenêtres — les 51 lignes `if (showX) renderX();`, les 26 lignes de `kDockLayout[]` et l'absence de menu Fenêtres ont disparu d'un coup, épinglés par **`window_registry_sync`** (premier contrôle de quelque nature que ce soit à atteindre MainWindow). L'angle `std::bitset` que cette case proposait est abandonné : il attaquait 68 octets de mémoire, pas le couplage, pour 691 sites d'accès à réécrire. **Reste ouvert, et c'est le vrai chantier** : les 17 135 lignes de la classe ne bougent pas et les 68 flags restent des membres de `MainWindow_ImGui`. La décomposition consiste à faire de chaque panneau un objet possédant son propre état (`visible`, géométrie, contexte) au lieu d'un `bool` chez MainWindow — faisable désormais **une fenêtre à la fois** derrière le registre, au lieu d'un big-bang. Les 17 lignes à `render == nullptr` (éditeurs qui auto-branchent leur carte, MemoryViewer, chooser) sont les premières candidates : leur bloc dédié EST déjà l'état qu'un objet porterait.
- [ ] **Palette de commandes et raccourcis par identifiant** `[M · nice]` — débloqué par le registre : `shortcuts[]` (`MainWindow_Keyboard.cpp`) associe encore une touche à un **pointeur de méthode**, pas à une fenêtre, donc rien ne relie un raccourci à l'entrée de menu correspondante. Avec `key` comme identifiant stable, un raccourci devient `("Bench", Ctrl+B)` et la palette (absente de POM1, présente dans POM2) se réduit à un filtre sur `windowRegistry()`. Attention à l'invariant existant : `shortcuts[]` ne doit **jamais** porter un accord CTRL+lettre, qui rendrait le code de contrôle intypable côté Apple-1.
- [ ] **Scinder la façade `EmulationController`** `[M · nice]` — la fuite d'include vers ImGui est livrée, et **l'axe taille l'est aussi au niveau des TU** (août 2026 : `EmulationController.cpp` 2143 l. → 4 TU — `.cpp` 867 (thread CPU + run/step + points d'arrêt + boucle de slice), `_State` 381, `_Machine` 332, `_Cards` 652 — code motion pur, jeu de méthodes prouvé identique) → `[CHANGELOG.md](CHANGELOG.md)`. **Reste ouvert** : l'axe *type*, pas fichier — extraire de vraies classes `CpuRunner` (run/step/slice) et `StateManager` (snapshot/rewind) au lieu de 207 méthodes sur une seule façade. Coûteux : ~110 des 207 sont des passthroughs d'une ligne vers `memory->setXxxEnabled()` appelés depuis tout le MainWindow, donc renommer déplace des centaines de sites — le vrai remède est de les data-driver contre `Memory::cardSlots()`, même chantier que l'item fenêtres hardware ci-dessus.





### Snapshot, scripting & presets

> **Durcissement désérialisation (audit 2026-05-31) livré** → `[CHANGELOG.md](CHANGELOG.md)`.

- [ ] **Snapshot residual gaps** `[M · nice]` — base format + 12-card per-card payloads + CPU section landed (May 2026). Remaining: cassette mid-stream playback position (re-load tape file by path on snapshot-load + seek to saved `playbackIndex`); WiFiModem / TerminalCard graceful "drop and reconnect" on load (currently kept disconnected); libresidfp internal filter integrators / oscillator phase (engine doesn't expose them — would need an upstream patch); SHA-256 footer (mentioned in `SnapshotIO.h` as v2 sweetener).
- [ ] **Scriptable runtime IPC** `[M · nice]` — `--cmd-fd <N>` (or Unix socket) reading line-delimited commands while the emulator runs — same verbs as CLI flags, but for stateful sequences. Telnet on `:6502` carries keystrokes + display; this channel carries control without polluting the keyboard stream. Depends on CLI-verb + snapshot work above.
- [ ] **External** `presets.json` `[S · nice]` — **le prérequis est livré** : `kMachinePresets[]` vit désormais dans `MachinePresets.{h,cpp}`, un TU sans UI (août 2026). Reste à charger la table depuis un JSON sous `doc/` (ou à côté de l'exécutable) pour que l'utilisateur ajoute des presets sans recompiler. Loader dans `MachinePresets.cpp`, table C++ conservée en repli. Attention : `preset_ram_profiles_smoke` **parse le fichier source en texte** — un chargeur JSON devra lui donner une autre prise.



### State rewind — raffinements (MVP livré)

> **MVP livré** → `[CHANGELOG.md](CHANGELOG.md)` : ring de snapshots delta-encodés, panneau **CPU → State Rewind…** + bande timeline inline, état écran capturé, **desktop-only**. Pinned by `rewind_buffer_smoke`.

- [ ] **VRAM dirty-tracking for finer TMS9918 deltas** `[M · nice]` — the 16 KB VRAM section is chunk-diffed against the previous full blob each capture; a live VRAM dirty bitmap would cut the per-capture diff cost on graphics-heavy frames.
- [ ] **Seek cost on card-heavy presets** `[S · nice]` — `rewindSeekTo` reuses `loadSnapshotFromBuffer`, whose FLAGS dispatch re-applies card setters (may reload ROMs) every slider tick. Skip re-apply when the flag set is unchanged to keep dragging smooth.

---



## ⏸️ Deferred · 🚫 Blocked

> Spec connu, code tractable, mais conditionné à un déclencheur réel (logiciel exerçant la feature, demande utilisateur, hardware disponible). À promouvoir quand le déclencheur apparaît. **🚫 Blocked** = en attente d'une ressource externe hors de notre contrôle.

- [ ] **Uncle Bernie's Woz Machine floppy** `[L · nice]` — 5.25" Disk II: Woz state machine (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbing DRAM-refresh jitter, GCR track/sector emulation, `.dsk` / `.woz` loader, `$C0Ex` soft switches, 74LS123 async drive clock. Worth it only when original Apple-1 disk software surfaces.
- [ ] **Joystick / paddle analogique (télémétrie)** `[déféré — hardware inexistant]` — **Décision 2026-06-16 : ne PAS implémenter.** Les paddles analogiques (`$C064`/`$C070` + timer 558) sont du hardware **Apple II**, pas Apple-1 — les modéliser émulerait une carte qui n'existe pas (règle « une vraie carte à la fois »). Côté télémétrie le digital est déjà couvert (FIFO `TELE_IN` + injection clavier `$D010`), et aucun logiciel Apple-1 réel n'utilise de paddle. À promouvoir seulement si une carte paddle Apple-1 réelle apparaît.
