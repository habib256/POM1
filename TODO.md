# TODO

Travaux **ouverts** sur l'émulateur uniquement. Livré → `[CHANGELOG.md](CHANGELOG.md)` / `git log` · visite guidée → `[README.md](README.md)` · logiciel 6502 → `[dev/TODO6502.md](dev/TODO6502.md)`.

## Conventions

- **Un item = une case** — `- [ ] **Titre** [effort · impact] — détail`.
- **Étiquettes** `[effort · impact]` — effort : **S** (<1 j) · **M** (1–5 j) · **L** (>5 j / architectural). Impact : **nice** · **solid** · **critical**.
- `> citation` = périmètre, diagnostic ou **porte de sortie** de la (sous-)section qui suit.
- **Un chantier n'apparaît qu'à un seul endroit.** La feuille de route de consolidation est la colonne vertébrale : tout item d'infrastructure vit dans la phase qui le réclame, pas dans une section d'audit parallèle.
- Seuls les items ouverts vivent ici — le livré est levé vers `[CHANGELOG.md](CHANGELOG.md)`.
- 🚫 en préfixe = bloqué sur une ressource externe.

## Sommaire

- [🔧 Consolidation architecturale](#-consolidation-architecturale) — **la colonne vertébrale**, phases 0 → 6
- [📦 Packaging, distribution & démarrage](#-packaging-distribution--démarrage)
- [💾 Snapshot, scripting & presets](#-snapshot-scripting--presets)
- [⏪ State rewind — raffinements](#-state-rewind--raffinements)
- [🎨 Graphismes](#-graphismes) — moteur beam GEN2 · synchro beam/CPU TMS9918
- [🛠️ Outillage de développement](#-outillage-de-développement) — Bench · BASIC · LOGO
- [🔌 Périphériques & chargeurs](#-périphériques--chargeurs)
- [🖼️ Visuel & UX](#-visuel--ux)
- [⏸️ Différé · 🚫 Bloqué](#-différé--bloqué)

---

## 🔧 Consolidation architecturale

> **Diagnostic (audit du 26 août 2026)** : POM1 reste un monolithe modulaire sain, cœur CPU / bus / rendu robuste et très bien testé. La dette critique est concentrée dans le cycle de vie des cartes, les trois sources de vérité de la configuration (`MainWindow_ImGui`, `Memory::setXxxEnabled()`, `MachineConfig`) et les responsabilités système accumulées par `Memory` / `EmulationController`. **Aucune réécriture** : conserver CPU, `PeripheralBus`, renderers, snapshots incrémentaux et tests ; migrer par façades compatibles, une responsabilité à la fois.
>
> **Architecture cible** : panneaux UI → commandes / vues immuables → façade applicative thread-safe → `MachineCoordinator` (`CpuRunner`, `CardTopology`, `StateManager`) → espace d'adressage / `PeripheralBus` / périphériques. Audio, fichiers, réseau et rendu deviennent des services injectés.
>
> **Ordonnancement** : phases 0 → 1 → 2 sur le chemin critique ; phase 3 dès que la topologie est stable ; phases 4-6 incrémentales. Estimation globale **10-14 semaines développeur**, point de stabilisation essentiel après **5-7 semaines**.
>
> **Priorité produit** — **P0** : phases 0 à 3. **P1** : phases 4 à 6. **P2 après stabilisation** : nettoyage CMake résiduel et couverture snapshot restante. Les sections hors feuille de route (nouvelles cartes, shader GEN2 GPU, chaînes BASIC, tier binary16, `presets.json` externe) sont **différées jusqu'à la sortie de phase 2** ; seul le câblage des exemples LOGO peut continuer en parallèle, ne touchant pas la topologie.

### Phase 0 — Socle reproductible et frontières de build (2-4 jours)

- [x] **Rendre les dépendances CMake configurables et hors-ligne** `[S · solid]` — cache paths `POM1_IMGUI_DIR` / `POM1_KLAUS_BIN`, vérification SHA-256 locale après téléchargement, désactivation explicite du seul test Klaus sans fixture valide, et `imgui_pin_sync` compatible avec une archive sans `.git`.
- [x] **Matérialiser les couches dans CMake** `[M · solid]` — chaîne `pom1_ui` → `pom1_app` → `pom1_devices` → `pom1_core`, objets assertifs séparés dans `pom1_test_devices`, helper `pom1_add_smoke_test()` et liaison automatique vérifiée de tous ses consommateurs vers `pom1_devices`.
- [x] **Épingler les dépendances architecturales et leur tendance** `[S · solid]` — `architecture_check` interdit les nouveaux includes cœur/périphériques → UI et publie une baseline à cliquet : taille de `MainWindow_*`, `Memory`, `EmulationController`, fan-out des en-têtes et sources hors `pom1_test_devices`.

> **Porte de sortie** : un checkout disposant de ses dépendances locales se configure hors-ligne ; build Release natif avec `POM1_WERROR=ON` et inventaire CTest complet verts ; aucun changement de comportement émulateur.

### Phase 1 — Une source de vérité pour la topologie des cartes (~2 semaines)

- [x] **Introduire les identités et descripteurs de cartes stables** `[M · critical]` — `CardId`, `CardDescriptor` et `CardSet` portent désormais identifiant non localisé, libellé UI, plages d'adresses, dépendances, incompatibilités, variante, tag de snapshot et capacités dans le registry existant `Memory::cardSlots()`. `BusConflicts.h` référence les cartes par identifiant typé ; `card_registry_smoke` vérifie exhaustivité, unicité et cohérence symétrique.
- [x] **Extraire toute la politique de conflit dans `CardTopology`** `[M · critical]` — le module pur porte identités typées, distinction Strict/Fantasy, résolution déterministe, dépendances IEC → microSD, CodeTank → TMS9918 et XACI → ACI, ainsi que toutes les exclusions. `MainWindow_SiliconStrict.cpp` consomme la politique sans chaînes ; les setters de `Memory` exécutent un `CardTransitionPlan` générique et ne décident plus des cascades. Couvert par `card_topology_smoke`, les invariants du registre et les tests historiques de cartes/snapshots.
- [x] **Remplacer le `MachineConfig` positionnel par une configuration nommée** `[M · critical]` — `PresetId` est explicite et compatible avec les 13 index historiques, `kDefaultPresetId` remplace l'invariant « dernier élément », les cartes sont stockées dans un `CardSet` et les options Juke-Box / CodeTank dans des structures nommées. Tous les champs booléens de cartes ont été supprimés. `validateMachinePresets()` vérifie nombre, dépendances et conflits Strict/Fantasy ; le test CLI épingle aussi le `CardSet` exact des 13 presets.
- [x] **Produire puis exécuter un `TransitionPlan` déterministe** `[L · critical]` — `CardTopology::planConfiguration()` calcule sans allocation la fermeture des dépendances, les refus atomiques et l'ordre stable detach / configure / attach. `MachineCoordinator` exécute le plan et porte le DTO `CardConfigurationRequest` ; `EmulationController` conserve uniquement le verrou et la publication atomique. Headless, presets GUI différés, surcharges CLI/launcher et Bench utilisent ce chemin, sans indicateurs `pending*` par carte. Les quinze wrappers contrôleur `setXxxEnabled()` ont été remplacés par `setCardEnabled(CardId, bool)` ; les cascades et exclusions sont testées via l’exécuteur réel.
- [x] **Tester exhaustivement la politique de topologie** `[M · critical]` — `card_topology_smoke` parcourt les 256 paires ordonnées des 16 cartes en Strict et Fantasy, vérifie fermeture des dépendances, refus sans opérations, ordre/forme des plans et idempotence, puis valide les 13 presets et leurs 169 transitions ordonnées. Les boucles reposent sur `CardId::Count` et `kMachinePresetCount` : toute nouvelle carte ou preset étend automatiquement la matrice, sans liste de test parallèle.
- [x] **Épingler la politique de bus une fois extraite** `[S · solid]` — `card_topology_smoke` contient un oracle explicite des dix exclusions physiques (paires, ordre et diagnostics), refuse les doublons symétriques et épingle séparément la règle Strict-only SID/TMS9918. Ce golden test complète les tests matriciels : supprimer une ligne de production ne peut plus rendre le test silencieusement moins exigeant. Règle conservée : **quand un module est décrit comme pur dans la doc, il devrait avoir un test qui le prouve.**

> **Porte de sortie** : aucune règle de topologie dans `MainWindow_*` ; aucun conflit décidé dans `Memory` ; presets, CLI et UI consomment le même `TransitionPlan` ; toutes les transitions sont déterministes et testées.

### Phase 2 — Cycle de vie déterministe, indépendant des frames UI (1-2 semaines)

- [x] **Définir un cycle de vie explicite des périphériques** `[M · critical]` — `Peripheral` expose `Constructed → Attached → Reset → Active`. `MachineCoordinator` reset chaque carte hors bus avant son attache atomique, active immédiatement les périphériques prêts et garde l’ACI en `Reset` jusqu’à son inscription réelle au mixer. Les cartes réseau suivent la même transaction sous le verrou machine.
- [x] **Appliquer un preset comme une transaction machine** `[L · critical]` — le DTO unique porte topologie, options, RAM, fidélité silicium, reset froid, ROM système/cartes et activation cassette. Sous une seule section critique contrôleur : quiesce CPU → réglages pré-reset → detach de tous les endpoints → reset RAM/CPU/périphériques → CodeTank + BASIC/Monitor/Krusader/CFFA1 ROM → attach → activation des producteurs → reprise CPU → publication unique. Les erreurs typées (`TopologyRejected` / `DeviceConfigurationFailed` / `SystemRomLoadingFailed`) laissent une topologie valide sans exposer l'état intermédiaire. GUI et headless consomment le même chemin ; le stress contrôleur couvre CPU actif, CPU arrêté et transaction froide avec réglages machine.
- [x] **Éliminer le délai magique de 15 frames** `[M · critical]` — supprimés `kCardEnableDeferFrames`, `pendingCardEnableFrames`, `finalizePendingCardPlugs()` et les états différés associés. La configuration est composée puis validée synchroniquement par une transaction `reset hors bus → attach → active`, sans temporisateur mural ou graphique.
- [x] **Prouver le démarrage sans rendu préalable** `[M · critical]` — `headless_preset_matrix` couvre les 13 presets avec `apply + load Intel HEX + run + 1 cycle` sans frame UI puis boot long. Le test contrôleur alterne SID / Juke-Box 32 fois sous CPU MAX avec snapshot atomique ; SID et cassette doivent produire des échantillons non nuls. Metal et OpenGL exécutent ces gates depuis leurs propres builds. Le smoke Chromium WASM exige désormais, via un probe C++ read-only, preset appliqué, CPU actif, RAM configurée, Woz Monitor présent, PC non nul et fréquence mesurée positive, en plus de WebGL2 et d'une frame non noire.
- [x] **Étendre la matrice headless aux combinaisons de cartes** `[S · solid]` — `--list-presets` expose les clés de cartes, puis `headless_preset_matrix` parcourt les **177 cartes absentes** des 13 presets. Son oracle indépendant ferme les trois dépendances et les onze conflits Strict ; les 159 combinaisons autorisées exécutent un programme dès le premier cycle sans frame, les 18 interdites doivent sortir par le rejet CLI typé, et les presets Fantasy restent permissifs. Trois probes injectent ensuite `8000R`, `9006R` et `F000R` à cycle déterministe et capturent directement le sink `$D012` headless : prompts SD CARD OS, CFFA1 et Krusader obligatoires. Cette preuve a révélé puis corrigé le chargement erroné de la ROM Krusader à `$A000` alors que l'image est liée à `$E000`.

> **Porte de sortie** : aucun cycle de vie cadencé par ImGui ; aucune fenêtre de course entre CLI / chargement et premier cycle CPU ; même comportement avec ou sans thread de rendu.

### Phase 3 — Audio temps réel et concurrence réellement exercée (1-2 semaines)

- [x] **Retirer verrous et allocations du callback audio** `[M · critical]` — le registre est un tableau fixe immuable double-buffer : `mixSources()` ne prend plus `sourcesMutex` et n'alloue pas ; `removeSource()` est une barrière de durée de vie testée. Le SID consomme son ring SPSC lock-free. La cassette pulse, ses clics mécaniques et son mode *AudioStream* utilisent des rings SPSC fixes. Le décodeur miniaudio est alimenté par le thread d'émulation dans un ring PCM ; `fillAudioBuffer()` ne prend plus aucun mutex, n'alloue plus et ne fait plus d'I/O. Le test headless vide explicitement le clic mécanique avant de vérifier le contenu PCM décodé.
- [x] **Faire voir le thread de rendu à TSan** `[M · solid]` — `concurrent_frontends_smoke` lance simultanément le vrai producteur `EmulationController`, un consommateur snapshot/rendu synthétique et un callback qui mixe les vraies sources audio, matériel désactivé. Pendant ce temps, le thread pilote alterne SID/Juke-Box et écrit en RAM : le test vérifie snapshots complets, PCM fini et activité des trois rôles. Il dure 350 ms par défaut et 5 s dans le job TSan nocturne (`POM1_CONCURRENCY_STRESS_MS`), où la campagne ciblée passe sans race.
- [x] **Étendre le rang des verrous aux mutex des cartes** `[S · nice]` — la hiérarchie vérifiée devient `State > Rewind > Keyboard > Snapshot > Peripheral > PeripheralInner`. Le rang `Peripheral` couvre SID, Terminal, modem Wi-Fi, télémétrie, PR-40, GT-6144 et le décodeur cassette ; `PeripheralInner` couvre l'unique imbrication interne, la file de résultat de capture Terminal. Le smoke accepte les ordres légaux et prouve par sous-processus que périphérique→snapshot, inner→périphérique et deux cartes de même rang abortent. Cela vérifie l'ordre, pas la durée de détention ni les allocations temps réel.
- [x] **Mesurer les invariants temps réel** `[S · solid]` — `RealtimeDiagnostics` agrège acquisitions, attente/détention maximale de `stateMutex`, nombre/durée maximale des callbacks et underruns/débordements SID/cassette. L'instrumentation est compilée uniquement avec les assertions (`POM1_REALTIME_DIAGNOSTICS`) et disparaît entièrement en Release. Le stress concurrent impose 500 ms max d'attente, 100 ms de détention et 50 ms par callback ; le test SID déterministe précharge 100 ms, consomme 4096 frames et exige zéro underrun/débordement.

> **Porte de sortie** : zéro `std::mutex` / allocation dans le callback ; le triangle émulation × rendu × audio est réellement exercé sous TSan ; zéro race et zéro underrun dans le scénario de stress de référence.

### Phase 4 — Extraire les responsabilités sans réécriture (2-3 semaines)

- [ ] **Extraire des chargeurs de mémoire purs** `[M · solid]` — créer `MemoryImageLoader` et des parseurs par format qui reçoivent des octets et retournent écritures / zones / adresse d'exécution / diagnostics, sans accès à `Memory`, audio, UI ou système de fichiers. `Memory` ne fait qu'appliquer un résultat validé. **Cette frontière devient le point d'entrée des fuzzers de la phase 6.**
- [ ] **Injecter la découverte des ressources et les services plateforme** `[M · solid]` — déplacer les sondes du cwd, chemins ROM / disques / cartes et création du périphérique audio hors du constructeur de `Memory`, derrière `ResourceLocator` et des interfaces de services fournies par l'application. Les tests construisent le cœur sans matériel audio ni fichiers implicites.
- [ ] **Créer `PeripheralManager`** `[L · critical]` — lui transférer propriété et cycle de vie des cartes, bindings `PeripheralBus`, endpoints audio / réseau et application du `TransitionPlan`. Réduire progressivement `Memory` à l'espace d'adressage, PIA et MMIO cœur ; préserver `memRead()` / `memWrite()` et `PeripheralBus` comme interfaces stables.
- [ ] **Définir des DTO de snapshot indépendants des classes de cartes** `[M · solid]` — sortir `CpuView` / `CardView` et les snapshots de cartes à portée namespace, alias membres conservés pour ne toucher aucun site d'appel, puis retirer les includes concrets de `EmulationSnapshot.h`. **C'est aussi le nœud de fan-out d'en-têtes suivant** : après la libération de `Memory.h`, `JukeBox.h` et `CodeTank.h` ne sont retombés que de 105 à **57 TU** (contre 105 → ~20 pour les sept autres cartes) parce qu'`EmulationSnapshot.h`, très largement inclus, a besoin de `JukeBox::Snapshot` / `CodeTank::Snapshot` — des structs **imbriqués** par valeur, le même blocage que les enums un cran plus haut. Quatre autres en-têtes (`MachinePresets.h`, `CliDispatcher.h`, `MemoryViewer_ImGui.h`, `MainWindow_ImGui.h`) ne se servent que des trois enums et peuvent basculer sur `[src/CardTypes.h](src/CardTypes.h)` seul. Ne partager de gros buffers immuables qu'après profilage : la priorité est la frontière de type, pas une micro-optimisation de copie.
- [ ] **Faire de `EmulationController` une façade mince** `[L · critical]` — l'axe *fichier* est réglé (4 TU) ; reste l'axe **type** : extraire de vraies classes `CpuRunner` (pacing, run / pause / step / slice) et `StateManager` (snapshot / rewind) au lieu de 207 méthodes sur une seule façade, en conservant la prise de verrou dans une façade applicative thread-safe. Coûteux : **~110 des 207 méthodes sont des passthroughs d'une ligne** vers `memory->setXxxEnabled()`, appelés depuis tout le MainWindow — renommer déplacerait des centaines de sites. Le vrai remède est de les remplacer par des commandes data-driven `CardId` / configuration adossées à `Memory::cardSlots()`. Migrer par groupes d'appelants et supprimer les wrappers devenus morts à chaque PR.

> **Porte de sortie** : `Memory` ne crée plus d'audio, ne sonde plus le filesystem et ne décide plus des conflits ; `EmulationSnapshot.h` n'inclut plus les cartes concrètes ; CMake interdit les dépendances inverses ; les anciennes API ne subsistent que si un appelant réel les utilise encore.

### Phase 5 — Décomposer l'UI par panneaux (3-5 semaines, incrémental)

- [ ] **Faire du registre de fenêtres une fabrique / propriétaire d'`IPanel`** `[L · solid]` — le registre existe (`WindowDescriptor` porte `render` / `gate` / `desktopOnly` / `dock`, liste unique des 68 fenêtres, épinglé par `window_registry_sync`), mais **les 17 135 lignes de la classe ne bougent pas et les 68 flags restent des membres de `MainWindow_ImGui`**. Chaque panneau doit devenir un objet possédant son propre `visible`, son état transitoire, sa géométrie, son modèle de vue et son `render(AppContext&)` ; `MainWindow_ImGui` ne conserve que menu, dock, layout et orchestration. Faisable **une fenêtre par PR** derrière le registre, au lieu d'un big-bang. *(L'angle `std::bitset` proposé en juillet est abandonné : il attaquait 68 octets de mémoire, pas le couplage, pour 691 sites d'accès à réécrire.)*
- [ ] **Migrer les panneaux dans l'ordre de risque architectural** `[L · solid]` — commencer par Silicon Strict / presets afin de consommer `CardTopology`, poursuivre par les panneaux de cartes, puis debug et dialogues fichier. Les **17 entrées `render == nullptr`** (éditeurs qui auto-branchent leur carte, MemoryViewer, chooser) sont les premières candidates : leur bloc d'état dédié EST déjà ce qu'un objet porterait. Bons quick wins, mais ils ne doivent pas retarder l'extraction de la politique de configuration.
- [ ] **Supprimer le miroir matériel autoritaire de l'UI** `[M · critical]` — les booléens « carte active » et variantes viennent exclusivement de la vue publiée / `CardSet` ; seuls visibilité, champs en cours d'édition et erreurs de validation restent locaux au panneau. Une commande UI demande une transition et affiche son résultat, sans muter préventivement plusieurs booléens.
- [ ] **Router fenêtres et raccourcis par identifiant stable, puis ouvrir la palette de commandes** `[M · solid]` — `shortcuts[]` (`MainWindow_Keyboard.cpp`) associe encore une touche à un **pointeur de méthode**, pas à une fenêtre : rien ne relie un raccourci à son entrée de menu. Avec la clé du registre comme identifiant stable — jamais le titre traduit — un raccourci devient `("Bench", Ctrl+B)`, et la palette de commandes (absente de POM1, présente dans POM2) se réduit à un filtre sur `windowRegistry()`. Menus, raccourcis, layout et palette partagent alors la même clé. **Invariant à préserver** : `shortcuts[]` ne doit **jamais** porter un accord CTRL+lettre, qui rendrait le code de contrôle intypable côté Apple-1.

> **Porte de sortie** : `MainWindow_ImGui` est une coquille applicative ciblée à moins de 400-500 lignes de déclaration ; aucun booléen UI ne constitue l'état réel d'une carte ; chaque panneau migré est testable indépendamment.

### Phase 6 — Entrées hostiles, support et portabilité (1-2 semaines, parallélisable)

- [ ] **Fuzzer les chargeurs de fichiers, en garde continue** `[M · solid]` — WOZMON hex, Intel HEX, TurboType `.TUR`, l'AIFF écrit à la main, `.d64` et les images de snapshot sont les seules portes d'entrée de données non maîtrisées. Chacun a ses gardes, et **chaque garde a été ajoutée après un bug** — la couverture est empirique, pas systématique. Le cas le plus exposé est l'AIFF (POM1 le lit lui-même, miniaudio n'ayant pas de backend : flottant 80 bits décodé à la main, quatre largeurs PCM). Une cible libFuzzer par parseur, amorcée par le corpus déjà présent dans `software/`, `cassettes/`, `sdcard/` ; imposer tailles maximales, validation des longueurs / CRC et erreurs structurées ; smoke borné par PR, campagne longue sous ASan la nuit. Chaque plantage trouvé devient un cas de test à coût nul. Se branche sur les chargeurs purs de la phase 4.
- [ ] **Filet de crash + bundle de diagnostic** `[M · solid]` — la moitié journal est en place (`FileLogger` + `logs/pom1.log`), qui était le vrai préalable. Reste le filet : seuls `SIGINT`/`SIGTERM` sont interceptés (pour vider la cassette). Un `SIGSEGV` emporte le journal, la disposition des fenêtres pas encore autosauvegardée et tout le tampon de rewind, et l'utilisateur n'a rien à envoyer. Gestionnaire de dernier recours écrivant trace d'appels + tampon de journal dans `crash/`, plus *Aide → Signaler un problème* qui assemble journal, snapshot, `ini/` et versions dans un zip. **Aucune télémétrie** : le bundle reste strictement local, c'est l'utilisateur qui décide d'envoyer.
- [ ] **`-Werror` sur Windows** `[S · nice]` — Linux et macOS passent `-DPOM1_WERROR=ON` ; le job `windows` compile en `/W4` sans `/WX`. **Relevé du 22 août 2026** : huit `C4244` (`int` → `float`) sur `src/sidtrack/SidTrackerEditor.cpp:342-343` et un `C4701` dans le `stb_vorbis.c` vendu — une passe de nettoyage précède l'activation, et le vendu devra rester hors du périmètre `/WX` comme il l'est déjà de `/W4`. À mesurer sur une machine Windows, pas à corriger à l'aveugle.
- [ ] **Documenter et automatiser la porte de sortie de consolidation** `[S · solid]` — checklist release réunissant build warnings-as-errors sur les trois OS, matrice presets + combinaisons (phase 2), WASM browser smoke, sanitizers, fuzz smoke et création locale d'un bundle de diagnostic. Ne déclarer la consolidation terminée qu'une fois ces gardes observées vertes sur CI.

> **Porte de sortie** : les entrées malformées ne crashent ni ne bloquent l'émulateur ; zéro warning traité en erreur sur les plateformes supportées ; un rapport utilisateur contient versions, journal, snapshot et configuration sans télémétrie automatique.

---

## 📦 Packaging, distribution & démarrage

- [ ] **Chargement paresseux des assets WASM — reste `cassettes/`** `[S · nice]` — la moitié `pic/` est faite ; reste `cassettes/` (2,5 Mo sur le fil, dont le `WOZ_talk.mp3` qui ne sert qu'au preset Fantasy) pour descendre vers 5 Mo — plus délicat : `CassetteDevice` charge le fichier au moment de l'application du preset, pas à l'ouverture d'une fenêtre.
  - **Ne pas faire `cfcard.po`** : fichier **creux** — 33 553 920 octets logiques, 800 Ko réels, 0,3 Mo gzip — le chantier (fetch asynchrone + état « image en cours de chargement », `Memory` ouvrant l'image au démarrage) est délicat pour 2 % du gain.
- [ ] **Rejouer la borne Pi sur un Pi réel** `[S · solid]` — les scripts `packaging/raspberrypi/` sont portés de NeoST et validés en local (compilation GLES de bout en bout, cascade GLSL forcée sous llvmpipe, helpers `config.txt`/`cmdline.txt` testés en bac à sable), mais **jamais exécutés sur un Pi**. À vérifier sur place : `[CRT] GLSL …` dans `journalctl -u pom1-kiosk@pi`, absence de craquement audio à `POM1_AUDIO_LATENCY=120`, plein écran sans WM, et `--uninstall` qui rend bien `config.txt`/`cmdline.txt`.
- [ ] **CI borne Pi (artefact `cortex-a72` avec PGO)** `[M · nice]` — NeoST entraîne son PGO sur un runner ARM64 (`pi-borne.yml`) pour éviter ~1 h de compilation sur le Pi ; POM1 n'a que l'AppImage aarch64 générique du job de release.

## 💾 Snapshot, scripting & presets

- [ ] **Trous résiduels du snapshot** `[M · nice]` — format de base, charges utiles des 12 cartes et section CPU sont en place (mai 2026). Restent : la **position de lecture cassette en cours de flux** (recharger le fichier tape par son chemin à la relecture du snapshot + seek sur le `playbackIndex` sauvegardé) ; `WiFiModem` / `TerminalCard` en « drop and reconnect » propre au chargement (aujourd'hui laissés déconnectés) ; les intégrateurs de filtre / la phase d'oscillateur internes à libresidfp (le moteur ne les expose pas — demanderait un patch amont) ; le pied de page SHA-256 (annoncé dans `SnapshotIO.h` comme douceur v2).
- [ ] **IPC de scripting à l'exécution** `[M · nice]` — `--cmd-fd <N>` (ou socket Unix) lisant des commandes ligne à ligne pendant que l'émulateur tourne — mêmes verbes que les flags CLI, mais pour des séquences avec état. Le telnet sur `:6502` porte les frappes et l'affichage ; ce canal porte le contrôle sans polluer le flux clavier. Dépend des verbes CLI et du snapshot ci-dessus.
- [ ] **`presets.json` externe** `[S · nice]` — le prérequis est en place (`kMachinePresets[]` vit dans `MachinePresets.{h,cpp}`, TU sans UI). Reste à charger la table depuis un JSON sous `doc/` (ou à côté de l'exécutable) pour que l'utilisateur ajoute des presets sans recompiler. Loader dans `MachinePresets.cpp`, table C++ conservée en repli. Attention : `preset_ram_profiles_smoke` **parse le fichier source en texte** — un chargeur JSON devra lui donner une autre prise. **Différé jusqu'à la sortie de phase 2** (la configuration nommée de la phase 1 change la forme de la table).

## ⏪ State rewind — raffinements

- [ ] **Dirty-tracking VRAM pour des deltas TMS9918 plus fins** `[M · nice]` — la section VRAM de 16 Ko est diffée par blocs contre le blob complet précédent à chaque capture ; une bitmap de pages sales vivante réduirait le coût de diff par capture sur les frames chargées en graphismes.
- [ ] **Coût du seek sur les presets chargés en cartes** `[S · nice]` — `rewindSeekTo` réutilise `loadSnapshotFromBuffer`, dont le dispatch FLAGS ré-applique les setters de cartes (et peut recharger des ROM) à chaque cran du slider. Sauter la ré-application quand le jeu de flags est inchangé, pour garder le glissement fluide.

---

## 🎨 Graphismes

### Moteur beam GEN2

- [ ] **Chemin GPU shader (desktop)** `[L · nice]` — porter `NtscPostProcessor` POM2 (mêmes noyaux FIR + matrice que le chemin composite CPU) sur le *shared video texture layer*. **Défer jusqu'à un besoin perf concret** : le décodage NTSC tourne sur un buffer 280×192 (~3-4 M MAC/frame), invisible en profil desktop ; le port déplace les **mêmes maths** vers un fragment shader → image byte-identique (épinglée `hgr_convert_smoke`), **zéro capacité visuelle ajoutée**. Ne le faire que si (1) un post-traitement lourd plein écran (courbure, bloom, scanlines shader, phosphore par pixel, NTSC à résolution interne >280) rend le CPU goulot, ou (2) un profil montre la démod comme coût réel. Coût : 2 chemins à garder byte-identiques + shader décliné GLSL/MSL/WebGL (dont le patch sampler Metal). Prérequis déjà livré → reportable **sans dette**.

### TMS9918 — synchro beam/CPU sous-ligne

- [ ] **Fetch sprite SAT « une ligne en avance » sous-ligne** `[M · solid]` — la ligne n doit afficher les sprites fetchés pendant n-1 (le latch seamless mode/blank existe ; reste la latence de fetch SAT exacte). Effet visible seulement sur écritures SAT mid-active (rares) → à valider sur silicium (Parmigiani) avant de modéliser la latence.
- [ ] **Journal VideoEvents + renderer par rejeu, adoption GEN2** `[L · solid]` — remplacer le rattrapage *eager* par un journal `(cycle,kind,value)` per-cycle (h,v) composé par **rejeu** entre sync points (découplage total + rewind-friendly). GEN2 a déjà ce journal (`gen2RecordingEvents`, `Memory.cpp`), snapshot/rewind inclus. Reste : (a) **généraliser** le journal en facilité partagée (hors `Memory`) + faire adopter `BeamGeometry`/`beamPosAt` (`src/BeamClock.h`) par le rejeu GEN2 (aujourd'hui géométrie GEN2-privée dans `GraphicsCard::frameCycleToPos`) ; (b) **faire adopter le journal+rejeu par le TMS9918** (remplacer `renderBeamCatchUp`/`syncSpriteScanToBeam` eager + sérialiser son journal comme GEN2). Objectif cycle-granularity commun POM1/POM2.

---

## 🛠️ Outillage de développement

### DevBench

- [ ] **Débogage au niveau source — étendre aux cibles C et au WASM** `[M · solid]` — le MVP asm/desktop est en place (`ca65 -g` + `ld65 --dbgfile`, `src/DbgFile.cpp`, point d'arrêt source depuis la toolbar du Bench). Reste : **les cibles C** (cl65 accepte `-g` mais `--dbgfile` doit traverser `-Wl` dans `benchCSpecLinkCmd`/`benchCSpecCl65Cmd`, et la table de lignes C passe par les `.dbg` du code généré) ; **le WASM** (le cc65-en-navigateur ne passe ni `-g` ni `--dbgfile` — même chantier côté `cc65_bench.js`) ; et des **points d'arrêt multiples** (le MVP s'aligne sur l'unique breakpoint CPU de la machine ; en gérer N demande soit un jeu de breakpoints dans `M6502`, soit un multiplexage réarmé au vol).

### BASIC dans le Bench

> Les deux chantiers ci-dessous sont **différés jusqu'à la sortie de phase 2** (priorité produit).

- [ ] **Variables chaîne (`A$`) dans le compilateur natif** `[L · nice]` — aujourd'hui le lexer rejette tout identifiant suivi de `$` (`BasicCompilerApplesoft.cpp`, « string variables need a later phase ») ; seuls les littéraux chaîne de `PRINT` existent. Chantier transverse : descripteurs (ptr+len) comme classe de variable parallèle à `V_`/`_I`, une région heap découpée dans `basicc_native.cfg`, un `basicrt_string.s` (alloc/copie/concat + `LEN`/`MID$`/`CHR$`/`STR$`), et un chemin d'expression *typé chaîne* dans le lexer/`expr` (tout est numérique aujourd'hui). Touche lexer + parser + codegen + runtime + cfg linker.
- [ ] **Tier float compact (binary16 / virgule fixe) pour coords bornées** `[L · nice]` — la largeur (2 vs 4) est abstraite par `vw()`/`W`, mais ~15 sites d'émission codent le binary32 en dur (`fpLoadConst`, `fpNeg`, `emitIfFalse`, signe FOR, tous les `jsr fp_*`). Demande : une nouvelle valeur `FpMode`/format sur `Codegen`, des helpers d'émission parallèles, un runtime `basicrt_fixed.s` (`fx_add/fx_mul/fx_div/fx_cmp/…`), un jeu de symboles + gating `-D` dédié, un dimensionnement linker, et une 3ᵉ branche dans la sélection de phase (`compile()`). Utile seulement quand la précision binary32 est superflue (jeux/anim à coords bornées).

### LOGO dans le Bench

- [ ] **Livre d'exemples LOGO dans le popup *Examples*** `[S · solid]` — les 10 `.logo` de `sketchs/logo/` existent (et sont préchargés MEMFS côté web) mais ne sont atteignables que par *File → Load* ; les câbler dans `kP1Examples[]`/`examples_` (groupe « LOGO », ouverture 1-clic) comme les exemples asm/C, pour la découvrabilité. **Seul item hors feuille de route qui peut avancer en parallèle** : il ne touche pas la topologie.

---

## 🔌 Périphériques & chargeurs

> Nouvelles cartes **différées jusqu'à la sortie de phase 2** : chacune ajoute une entrée à la topologie que la phase 1 est en train de refondre.

- [ ] **Bootloader série flowenol apple1-serial** `[S · solid]` — [https://github.com/flowenol/apple1-serial](https://github.com/flowenol/apple1-serial) — bootloader / terminal sur port série (complète TurboType / 8BitFlux). Passe par la Terminal Card ou sa propre variante ACIA ; vraisemblablement un chargeur de format texte au-dessus de `Memory::loadHexDump` et du pipeline de collage.
- [ ] 🚫 **Chargeur TurboType 57 600 bauds** `[M · solid]` — **En attente de Bernie (échange courriel 2026-06-24) : spec détaillée + une ROM/binaire du dropper nécessaires avant implémentation.** Format d'Uncle Bernie, distribué par le *Keyboard Serial Terminal* 8BitFlux (ATtiny + quartz 11 MHz + MAX232 + 74LS244). Protocole : amorçage à vitesse Wozmon (200 ms/newline, 20 ms/car.) installant un dropper en RAM qui **saute les échos `$D012`** et diffuse les octets à 57,6 kbps avec CRC courant, sentinelle + vérification CRC, saut à l'entrée. Charge 4 Ko en <30 s contre ~2 400 bauds sous Wozmon. Côté POM1 : parser `.TUR`/`.APL`, basculer la Terminal Card en 8 bits bruts avec injection sans écho (`Ctrl-T` donne déjà le 8 bits ; le sans-écho est nouveau), vérifier le CRC, rendre la main à Wozmon. *Note émulateur :* `loadHexDump` *gère déjà le multi-blocs et les marqueurs* `T`*/*`X`*, et charge instantanément — TurboType n'a de valeur que pour l'authenticité / la démonstration du protocole, pas pour la vitesse de chargement.*
- [ ] **Briel Multi I/O — SpeakJet** `[M · nice]` — les blocs 6522 / 6551 doublonnent microSD / MODEM ; la valeur propre est de router le flux d'octets de l'UART vers un pont TTS (eSpeak, `say` sous macOS) pour donner une voix à l'Apple-1. À livrer comme périphérique optionnel séparé, afin qu'il coexiste avec microSD.

---

## 🖼️ Visuel & UX

- [ ] **Fidélité CRT 1976 (opt-in, désactivé par défaut)** `[M · nice]` — deux sous-effets sous le toggle CRT existant :
  1. **Streaming du registre à décalage** `[S · nice]` (timing Signetics 2519) — les caractères arrivent à ~60/s, le scroll matériel décale le buffer une ligne à la fois, l'affichage gèle pendant les rafales CPU. À associer au preset 4 Ko nu.
  2. **Bruit de points du registre à décalage** `[S · nice]` (horloge 2504 / 2513) — statique périodique, **non aléatoire** — ~40 × 3 sous-cellules par caractère, dérive de phase horizontale de 1 px d'une ligne à l'autre, dernière rangée plus courte. Nouveau `drawShiftRegisterNoise()` après la passe de fond, double boucle déterministe, `alpha ≈ crtScanlineAlpha * 0.25`, teinté par `phosphorTint`.

---

## ⏸️ Différé · 🚫 Bloqué

> Spec connue, code tractable, mais conditionné à un déclencheur réel (logiciel exerçant la feature, demande utilisateur, matériel disponible). À promouvoir quand le déclencheur apparaît. **🚫 Bloqué** = en attente d'une ressource externe hors de notre contrôle.

- [ ] **Woz Machine floppy d'Uncle Bernie** `[L · nice]` — Disk II 5,25" : machine à états de Woz (74LS299 + 74LS259), Timing Fix Circuit (GAL16V8) absorbant le jitter de rafraîchissement DRAM, émulation GCR piste/secteur, chargeur `.dsk` / `.woz`, soft switches `$C0Ex`, horloge asynchrone de lecteur 74LS123. Ne vaut le coup que si du logiciel disquette Apple-1 d'origine refait surface.
- [ ] **Joystick / paddle analogique (télémétrie)** `[différé — matériel inexistant]` — **Décision 2026-06-16 : ne PAS implémenter.** Les paddles analogiques (`$C064`/`$C070` + timer 558) sont du matériel **Apple II**, pas Apple-1 — les modéliser émulerait une carte qui n'existe pas (règle « une vraie carte à la fois »). Côté télémétrie le digital est déjà couvert (FIFO `TELE_IN` + injection clavier `$D010`), et aucun logiciel Apple-1 réel n'utilise de paddle. À promouvoir seulement si une carte paddle Apple-1 réelle apparaît.
