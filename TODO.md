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

- [ ] **Chargement paresseux des assets WASM** `[S–M · solid]` — **mesuré sur le site en ligne** (en-têtes HTTP, 22 août 2026) : Pages sert en **gzip**, donc le visiteur télécharge **14,0 Mo** de `POM1.data` et **1,2 Mo** de `POM1.wasm` — pas les 62 Mo bruts que cette case annonçait. Décomposition **sur le fil** (`tar | gzip -6`, ce qu'un serveur envoie) : **`pic/` 6,9 Mo (50 %)**, `cassettes/` 2,5, `sketchs/` 2,4, `dev/` 0,6, `sdcard/` 0,5, `fonts/` 0,5, `software/` 0,4, **`cfcard/cfcard.po` 0,3**, `roms/` 0,2. C'est le seul canal sans installation, donc celui où le visiteur juge en dix secondes.
  - **Correction : `cfcard.po` ne vaut pas 52 % du gain, mais 2 %.** C'est un fichier **creux** — 33 553 920 octets logiques, **800 Ko sur le disque**, l'essentiel étant des zéros, qui se compressent à néant. Le chantier décrit (fetch asynchrone + état « image en cours de chargement » côté carte, `Memory`'s constructeur ouvrant l'image inconditionnellement au démarrage, vérifiable seulement en navigateur) reste du travail délicat **pour 0,3 Mo : à ne pas faire**.
  - **La vraie cible est `pic/`, la moitié du téléchargement** : des photos décoratives de *Aide → Photos* que la majorité des visiteurs n'ouvre jamais, et que le workflow sait déjà servir en HTTP à côté de la page (il y copie `build-wasm/pic/icon.png`). Les sortir du préchargement MEMFS pour un `fetch` à l'ouverture de la fenêtre — ou les passer en WebP — ramène le premier pixel de 14 à ~7 Mo. `cassettes/` (2,5 Mo, dont le `WOZ_talk.mp3` qui ne sert qu'au preset Fantasy) en second, pour descendre vers 5 Mo.
- [ ] **Rejouer la borne Pi sur un Pi réel** `[S · solid]` — les scripts `packaging/raspberrypi/` sont portés de NeoST et validés en local (compilation GLES de bout en bout, cascade GLSL forcée sous llvmpipe, helpers `config.txt`/`cmdline.txt` testés en bac à sable), mais **jamais exécutés sur un Pi**. À vérifier sur place : `[CRT] GLSL …` dans `journalctl -u pom1-kiosk@pi`, absence de craquement audio à `POM1_AUDIO_LATENCY=120`, plein écran sans WM, et `--uninstall` qui rend bien `config.txt`/`cmdline.txt`.
- [ ] **CI borne Pi (artefact `cortex-a72` avec PGO)** `[M · nice]` — NeoST entraîne son PGO sur un runner ARM64 (`pi-borne.yml`) pour éviter ~1 h de compilation sur le Pi ; POM1 n'a que l'AppImage aarch64 générique du job de release.

### Solidité (audit août 2026)

> **Livré** → `[CHANGELOG.md](CHANGELOG.md)` : la bibliothèque d'objets `pom1_core` (1435 → 355 compilations), `-Wall -Wextra` + `-DPOM1_WERROR=ON` en CI, les jobs build-only macOS/Windows, le job sanitizer nocturne (`-DPOM1_SANITIZE=`, timeouts ×5), `src/LockOrder.h` + `lock_order_smoke`, `tools/check_doc_paths.py` + `doc_paths_sync`, le binaire sous test passé à `test_lib_micro.py`, et `pic/` divisé par deux. **22 août 2026** : les trois cassages que cette passe avait elle-même introduits sur les plateformes qu'aucun job par-push ne compile ou n'exécute (WASM `deltaTime`, `RC1106` sur `POM1.rc`, `C2589` MSVC dans `RewindBuffer.cpp`) — d'où les quatre premières cases ci-dessous, qui ferment le quadrant plutôt que les trois bugs.

> **La forme du trou, mesurée ce jour-là** : Linux compile *et* teste ; macOS et Windows compilent sans jamais tester ; le WASM n'est bâti que par `Deploy Pages`, donc **après** le merge sur `main` ; les sanitizers sont nocturnes. Les trois cassages ont atterri dans exactement ce quadrant, et il a fallu deux allers-retours de CI pour les voir, faute de pouvoir les reproduire en local.

- [ ] **Job de compilation WASM dans `ci.yml`** `[S · critical]` — `ci.yml` compile Linux, macOS, Windows et jusqu'au palier GLES du Pi, mais **jamais le WASM** : le bundle n'est bâti que par `Deploy Pages`, sur `main`, après le merge. Le 22 août 2026 la passe avertissements a commenté le nom du paramètre de `updateCpuExecution(float /*deltaTime*/)` — lu uniquement dans la branche `#if POM1_IS_WASM` deux lignes plus bas — et 92 tests locaux plus trois jobs de bureau sont restés verts pendant que le seul build qui lit cette ligne mourait à 29 %. Même forme que l'étape « GLES 3.0 tier » déjà présente : `emcmake cmake && cmake --build`, sans déploiement, ~4 min avec le cache emsdk. **Le meilleur rapport valeur/coût de tout ce document.**
- [ ] **`ctest` sur macOS et Windows** `[S · critical]` — les deux jobs construisent les 50 binaires de test puis les jettent : la suite ne tourne que sur Linux. **Aucun test de POM1 n'a donc jamais été exécuté sous Windows**, la plateforme dont les utilisateurs sont les moins outillés pour diagnostiquer quoi que ce soit. ~2 min par job, et « ça linke » devient « ça marche ».
- [ ] **Smoke navigateur sur la page publiée** `[M · solid]` — `Deploy Pages` téléverse sans jamais charger la page : une erreur JS, un échec d'init WebGL2 ou un `POM1.data` tronqué se déploient **au vert**. Playwright/Puppeteer headless sur `POM1.html` : attendre `onRuntimeInitialized`, assérer zéro exception console et un canvas non noir. Entre « le build passe » et « le site marche » il n'y a rien aujourd'hui.
- [ ] **Essai à blanc du packaging, hebdomadaire** `[S · solid]` — `release.yml` ne tourne que sur tag, donc une casse de packaging se découvre **pendant** la publication : c'est exactement la cicatrice du dylib Homebrew à chemin absolu embarqué dans le `.dmg`, qui mourait au dyld sur toute machine Apple Silicon. Un `schedule:` hebdomadaire bâtissant les trois paquets sans rien publier convertit ce risque en information gratuite.
- [ ] **Flags de compilation partagés entre l'app et les tests** `[S · solid]` — `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_CRT_SECURE_NO_WARNINGS` et `/utf-8` sont posés par `target_compile_definitions(${PROJECT_NAME} …)`, donc sur la **cible POM1 seule** : `pom1_core` et les 50 cibles de test compilent les mêmes `.cpp` sous un autre jeu de macros. Le 22 août 2026 `POM1.exe` se construisait pendant que `test_rewind_buffer`, `test_cli_dispatcher` et `test_measured_cpu_rate` échouaient sur `RewindBuffer.cpp` (`C2589` : `min`/`max` en macros de `windows.h`). Trois TU se protègent déjà à la main (`AudioDevice.cpp`, `SocketHandle.h`, `NativeFileDialog.cpp`) — c'est le symptôme, chacun ayant payé la leçon séparément. Une bibliothèque `INTERFACE` (`pom1_build_flags`) liée par la cible principale, par `pom1_core` et par les tests supprime la classe entière.
- [ ] **`crt_params_sync` — épingler la pile CRT** `[S · solid]` — douze réglages vivent en **trois copies tenues à la main** : `CrtParams.h`, le GLSL de `CrtEffectStack.cpp` (`uBarrel`, `uShadowStrength`…) et le MSL de `CrtEffectStackMetal.mm` (`barrel`, `shadowStrength`…), avec des noms qui ne se correspondent pas un à un (`shadowMaskStrength` → `uShadowStrength` → `shadowStrength`). `CLAUDE.md` écrit noir sur blanc qu'un bouton ajouté à l'un doit l'être à l'autre « ou macOS diverge silencieusement » — et **rien ne le vérifie : aucun test ne couvre la pile CRT**. Un script dans l'idiome de `imgui_pin_sync` / `doc_paths_sync` (lire les trois fichiers, appliquer la table de correspondance des noms — qui est elle-même la documentation manquante — et assérer l'ensemble identique) tient en ~40 lignes.
- [ ] **`--help` n'existe pas** `[S · nice]` — le message d'erreur d'un drapeau inconnu dit *« Run with --help for the supported list »* (`CliDispatcher.cpp`), et ce drapeau n'est pas reconnu : celui qui se trompe est renvoyé vers une impasse. Ajouter le drapeau et une page d'usage. (Les 48 drapeaux implémentés sont en revanche **tous** documentés dans `[doc/CLI.md](doc/CLI.md)` — vérifié un par un le 22 août 2026.)
- [ ] **Fuzzer les chargeurs de fichiers** `[M · solid]` — WOZMON hex, Intel HEX, TurboType `.TUR`, l'AIFF écrit à la main, `.d64`, les images de snapshot : les seules portes d'entrée de données non maîtrisées. Chacun a ses gardes, et chaque garde a été ajoutée **après** un bug — la couverture est donc empirique, pas systématique. Le cas le plus exposé est l'AIFF (POM1 le lit lui-même, miniaudio n'ayant pas de backend : flottant 80 bits décodé à la main, quatre largeurs PCM). Une cible libFuzzer par parseur, amorcée par le corpus déjà présent dans `software/`, `cassettes/`, `sdcard/`, sous ASan. Chaque plantage trouvé devient un cas de test à coût nul.
- [ ] **Filet de crash + bundle de diagnostic** `[M · solid]` — **la moitié journal est livrée** (août 2026 : `FileLogger` + `logs/pom1.log`, troisième enfant du `TeeLogger` — voir `[CHANGELOG.md](CHANGELOG.md)`), ce qui était le vrai préalable : la case supposait un journal à empaqueter et il n'en existait aucun. Reste le filet lui-même. Seuls `SIGINT`/`SIGTERM` sont interceptés (pour vider la cassette). Un `SIGSEGV` emporte le journal, la disposition des fenêtres pas encore autosauvegardée et tout le tampon de rewind, et l'utilisateur n'a rien à envoyer. Pour un logiciel maintenu par une personne et distribué à des amateurs sur quatre plateformes, c'est le maillon manquant du support. Gestionnaire de dernier recours écrivant trace d'appels + tampon de journal dans `crash/`, plus *Aide → Signaler un problème* qui assemble journal, snapshot, `ini/` et versions dans un zip. Aucune télémétrie : c'est l'utilisateur qui décide d'envoyer.
- [ ] **`-Werror` sur macOS et Windows** `[S · nice]` — les deux jobs build-only compilent avec `-Wall -Wextra` / `/W4` mais **sans** `-Werror` : l'arbre a été mesuré propre sous GCC uniquement, et AppleClang comme MSVC ont des jeux d'avertissements sensiblement plus larges (conversions signé/non signé, paramètres non référencés). Lire le compte dans les logs du premier run vert, corriger, puis ajouter `-DPOM1_WERROR=ON` à ces deux jobs comme sur Linux. **Premier relevé (22 août 2026)** : MSVC signale déjà huit `C4244` (`int` → `float`) sur `src/sidtrack/SidTrackerEditor.cpp:342-343` et un `C4701` dans le `stb_vorbis.c` vendu — une passe de nettoyage précède donc l'activation, et le vendu devra rester hors du périmètre `/WX` comme il l'est déjà de `/W4`.
- [ ] **Étendre le rang des verrous aux mutex des cartes** `[S · nice]` — `LockOrder.h` couvre les trois verrous du cœur. `SID::chipMutex`, `TerminalCard::cardMutex` / `screenshotResultMutex` et les verrous du modem restent hors table ; leur donner un rang (sous `Snapshot`, ou dans une bande dédiée aux périphériques) étendrait la vérification au seul endroit où il reste des verrous non ordonnés. **Ne couvre pas la classe de défaut corrigée dans `CassetteDevice` en août 2026** : un rang vérifie l'ORDRE, pas la DURÉE de détention ni une allocation sur le thread temps-réel — ni `LockOrder.h` ni TSan ne voient celle-là, seule une lecture du chemin d'appel la trouve.

### Refactors architecturaux (audit juillet 2026)

> Issus d'une revue architecturale transversale. Le cœur (CPU/Memory/bus) est propre ; la dette se concentre dans le fan-out « ajouter une carte » et les god objects UI.
>
> **Livré (août 2026 — passe « god files »)** → `[CHANGELOG.md](CHANGELOG.md)` : le **découplage `CliDispatcher` → `MainWindow_ImGui`** (table de presets sortie vers `MachinePresets.{h,cpp}`, sans UI ; le 5ᵉ trou de tests de juillet 2026 est comblé — **`cli_dispatcher_smoke`**, dont l'assertion réelle est qu'il *linke*), et **cinq god files découpés en code motion pur** (jeu de méthodes / lignes de code prouvé identique à chaque fois, 90/90 tests verts) : `Pom1BenchHost.cpp` 3957 → 2229 (+`_Lang` 571, `Targets` 456, `Cc65` 789), `MainWindow_Dialogs.cpp` 3559 → 1807 (+`_Settings` 594, `_Tutorials` 1227), `MainWindow_HardwareWindows.cpp` 2530 → 1752 (+`_SiliconStrict` 818), `EmulationController.cpp` 2143 → 867 (+3 TU), `MainWindow_Presets.cpp` 2740 → 2327. Le plus gros fichier propre restant est `Memory.cpp` (2532 l., déjà allégé en juillet) : **plus rien au-dessus de 2 600 lignes, contre quatre fichiers >2 500 et deux >3 500 avant la passe.**
>
> **Livré (juillet 2026)** → `[CHANGELOG.md](CHANGELOG.md)` : le **registry de cartes unique** (`Memory::cardSlots()` — les 4 listes hand-synced de `Memory.cpp` + la 5ᵉ que portait `snapshot_smoke` effondrées en une table ordonnée ; unicité-8-octets prouvée à la compilation ; ordre des sections épinglé) ; l'**extraction du snapshot I/O** vers `MemorySnapshot.cpp` (`Memory.cpp` : 2569 → 2157 l.) et le `RomLoadPolicy` nommé ; la **fuite d'include** `EmulationController → imgui` ; le **table-driving des fenêtres photo** ; **4 des 5 trous de tests** (Disassembler6502, A1IO_RTC, TerminalCard, WiFiModem — 74 → 78 tests).

- [ ] **Débogage au niveau source dans le DevBench** `[M–L · critical]` — l'écart le plus net entre ce que POM1 a et ce qu'il pourrait être : le Bench compile de l'asm et du C avec la vraie chaîne cc65, exécute le résultat sur le vrai 6502, puis laisse l'utilisateur seul avec des adresses hexadécimales. **Toutes les pièces existent séparément** : `ca65 -g` et `ld65 --dbgfile` sont dans le cc65 embarqué (le Bench ne passe ni l'un ni l'autre) ; `Symbols.cpp` lit déjà les étiquettes au format VICE, mais l'utilisateur doit charger le fichier à la main ; points d'arrêt, points de surveillance, pas-à-pas, pas-à-pas principal, désassembleur et anneau de trace du PC sont tous dans `EmulationController` ; l'éditeur avec coloration est dans `bench/CodeBench.cpp`. Ce qui manque est la correspondance ligne ↔ adresse et le câblage. Point d'insertion propre et déjà isolé : `Pom1BenchCc65.cpp`, le constructeur de commandes pur — **qui n'a lui-même aucun test**, alors que `CLAUDE.md` le décrit comme la partie pure, donc les deux se traitent d'un coup.
- [ ] **Épingler les modules décrits comme purs** `[S · solid]` — 26 modules de `src/` n'apparaissent dans aucune cible de test, et presque tous sont de l'UI. **Recompté août 2026 : 42 des 105 `.cpp` de `src/` n'apparaissent dans aucune cible de test**, et trois seulement sont hors UI et testables : `Pom1BenchCc65.cpp` (« des fonctions sur des chaînes et des chemins, sans MainWindow, sans EmulationController, sans ImGui » — vérifié, `bench/CodeBench.h` ne tire pas ImGui, la cible linkera sans UI), `Pom1BenchTargets.cpp` (table de données, comme `MachinePresets`) et la table `ConflictRule` de `MainWindow_SiliconStrict.cpp`, qui est de la politique de bus pure et demande une extraction préalable. Les autres candidats plausibles ne le sont pas : `HgrImageDecode.cpp` / `TmsImageDecode.cpp` sont des TU d'implémentation stb_image sans en-tête, `bench/Markdown.cpp` porte 75 appels `ImGui::`, et `bench/BenchLang.h` inclut `TextEditor.h`. Règle à retenir de la passe précédente : **quand un module est décrit comme pur dans la doc, il devrait avoir un test qui le prouve.**
- [ ] **Table-driver les fenêtres hardware** `[M · nice]` — le volet photo est livré (une table `PhotoWindowDef` + un `renderPhotoWindow` générique remplacent 8 paires `ensure<X>Texture()`/`render<X>PhotoWindow()`, −351 l. et −32 membres, et la boucle de teardown a corrigé 3 textures jamais détruites), et la **politique de conflit de bus est sortie du fichier de rendu** (août 2026 : `MainWindow_SiliconStrict.cpp` — table `ConflictRule` + `gateStrictPlug`/`wouldCreateConflict` + l'inspecteur ; `MainWindow_HardwareWindows.cpp` 2530 → 1752 l.). C'était le préalable : la règle Parmigiani n'est pas un détail de rendu. → `[CHANGELOG.md](CHANGELOG.md)`. **Reste ouvert** : les **52 blocs** `ImGui::Begin`/render par carte, à data-driver contre le registry de cartes (livré, `Memory::cardSlots()`). Les 66 flags `show`* du god object `MainWindow_ImGui` (439 membres) veulent devenir un `std::bitset` + enum — **mais 691 sites d'accès dans `src/`, pour 68 octets gagnés** : à ne faire qu'avec une réécriture scriptée et un diff relu, sinon le rapport risque/valeur est mauvais.
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
