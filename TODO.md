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

- [ ] **Rendre les dépendances CMake configurables et hors-ligne** `[S · solid]` — ajouter les cache paths `POM1_IMGUI_DIR` et `POM1_KLAUS_BIN` ; pour Klaus, télécharger sans `EXPECTED_HASH`, ne calculer / comparer le SHA-256 qu'après un téléchargement réussi, et désactiver explicitement le test avec un message actionnable si aucun binaire local ou réseau n'est disponible. Le contrôle `imgui_pin_sync` doit fonctionner dans une archive source sans `.git`, ou annoncer proprement qu'il n'est pas applicable.
- [ ] **Matérialiser les couches dans CMake** `[M · solid]` — faire évoluer la bibliothèque d'objets de tests vers des cibles logiques `pom1_core`, `pom1_devices`, `pom1_app` et `pom1_ui`, sans big-bang ; centraliser le câblage répétitif des smokes dans un helper `pom1_add_smoke_test()` et lier chaque cible au plus petit ensemble de couches nécessaire.
- [ ] **Épingler les dépendances architecturales et leur tendance** `[S · solid]` — ajouter un contrôle de direction des includes / liens et publier une baseline simple (taille de `MainWindow_ImGui`, `Memory`, `EmulationController`, fan-out des en-têtes, nombre de sources hors cible de test). Le garde doit refuser une nouvelle dépendance UI → cœur ou périphérique → UI, sans imposer immédiatement une baisse de tous les compteurs historiques.

> **Porte de sortie** : un checkout disposant de ses dépendances locales se configure hors-ligne ; build Release natif avec `POM1_WERROR=ON` et inventaire CTest complet verts ; aucun changement de comportement émulateur.

### Phase 1 — Une source de vérité pour la topologie des cartes (~2 semaines)

- [ ] **Introduire les identités et descripteurs de cartes stables** `[M · critical]` — créer `enum class CardId`, `CardDescriptor` et `CardSet` : identifiant non localisé, libellé UI, plages d'adresses, dépendances, incompatibilités, variante / options, tag de snapshot et capacités. Étendre le registry existant `Memory::cardSlots()` au lieu de créer une table concurrente.
- [ ] **Extraire toute la politique de conflit dans `CardTopology`** `[M · critical]` — déplacer `ConflictRule`, `wouldCreateConflict`, les comparaisons de chaînes de `MainWindow_SiliconStrict.cpp` et les cascades de `Memory::setXxxEnabled()` vers un module pur. Modéliser explicitement au minimum IEC → microSD, CodeTank → TMS9918, XACI → ACI et les exclusions Silicon Strict / Fantasy. `Memory` ne doit plus décider ce qu'il faut désactiver : il attache ou détache la configuration validée qui lui est demandée.
- [ ] **Remplacer le `MachineConfig` positionnel par une configuration nommée** `[M · critical]` — conserver les 13 presets et leurs index historiques via une table de compatibilité, mais stocker les cartes dans `CardSet`, leurs options dans des champs nommés et le défaut dans un `PresetId` explicite au lieu de l'invariant « dernier élément ». Ajouter une validation au démarrage / à la compilation des identifiants, dépendances et conflits de chaque preset.
- [ ] **Produire puis exécuter un `TransitionPlan` déterministe** `[L · critical]` — `MachineCoordinator::planConfiguration()` calcule la fermeture des dépendances, les refus et l'ordre detach / configure / attach ; `applyConfiguration()` exécute ce plan sous le verrou d'état. Garder temporairement les setters publics de `EmulationController` comme wrappers de compatibilité, puis supprimer chaque wrapper dès que ses appelants UI / CLI ont migré.
- [ ] **Tester exhaustivement la politique de topologie** `[M · critical]` — tests purs de toutes les paires de cartes, dépendances / cascades, modes Strict et Fantasy, idempotence, validation des 13 presets et matrice des 169 transitions preset → preset. Chaque nouvelle carte devra fournir son descripteur et étendre automatiquement la matrice, sans nouvelle liste maintenue à la main.
- [ ] **Épingler la politique de bus une fois extraite** `[S · solid]` — **42 des 105 `.cpp` de `src/` n'apparaissent dans aucune cible de test** (recompte août 2026), presque tous de l'UI. Le seul candidat hors UI et réellement testable est la table `ConflictRule` de `MainWindow_SiliconStrict.cpp` : c'est de la politique de bus pure, et son test devient trivial dès que la case *Extraire toute la politique de conflit* ci-dessus l'a sortie de l'UI. Les autres candidats plausibles ne le sont pas : `HgrImageDecode.cpp` / `TmsImageDecode.cpp` sont des TU d'implémentation stb_image sans en-tête, `bench/Markdown.cpp` porte 75 appels `ImGui::`, et `bench/BenchLang.h` inclut `TextEditor.h`. Règle : **quand un module est décrit comme pur dans la doc, il devrait avoir un test qui le prouve.**

> **Porte de sortie** : aucune règle de topologie dans `MainWindow_*` ; aucun conflit décidé dans `Memory` ; presets, CLI et UI consomment le même `TransitionPlan` ; toutes les transitions sont déterministes et testées.

### Phase 2 — Cycle de vie déterministe, indépendant des frames UI (1-2 semaines)

- [ ] **Définir un cycle de vie explicite des périphériques** `[M · critical]` — remplacer le contrat minimal actuel par les états `constructed → attached → reset → active`, avec opérations idempotentes et ordre documenté. Le raccordement au bus doit être terminé avant le premier cycle CPU ; l'audio et le réseau ne deviennent actifs qu'après le reset et la disponibilité de leur producteur.
- [ ] **Appliquer un preset comme une transaction machine** `[L · critical]` — pause CPU → detach des ressources sortantes → configuration / chargement ROM / reset → attach au bus → activation audio / réseau → publication d'un snapshot cohérent → reprise CPU. En cas d'échec, retourner une erreur structurée et conserver ou restaurer une configuration valide ; ne jamais exposer un état intermédiaire à l'UI.
- [ ] **Éliminer le délai magique de 15 frames** `[M · critical]` — identifier avec un test reproductible la cause du SID / cassette silencieux ou cassé lors d'un branchement immédiat, corriger l'ordre d'initialisation ou amorcer explicitement les rings, puis supprimer `kCardEnableDeferFrames`, `pendingCardEnableFrames`, `finalizePendingCardPlugs()` et tous les booléens `pending*` associés. Aucun remplacement par un autre temporisateur mural ou graphique.
- [ ] **Prouver le démarrage sans rendu préalable** `[M · critical]` — tests « apply preset + load + premier cycle » avec zéro frame UI, changement de preset pendant l'exécution, activation / retrait répétés, sortie SID / cassette non vide, et parité desktop headless / OpenGL / Metal / WASM. Ajouter ces scénarios à la matrice headless existante.
- [ ] **Étendre la matrice headless aux combinaisons de cartes** `[S · solid]` — `headless_preset_matrix` boote les presets tels que livrés ; les combinaisons que le mode strict *autorise* (`--enable`/`--disable` par-dessus un preset, `wouldCreateConflict` côté UI) et celles qu'il refuse ne sont pas parcourues. Même harnais, un second axe : pour chaque preset, chaque carte absente que `gateStrictPlug` accepterait, boot + Monitor. Et une assertion plus forte que « PC dans le Monitor » là où une ROM de carte a un prompt (SD CARD OS, CFFA1, Krusader) : un `--paste` + capture du flux `$D012` via `--telemetry-log`. **Préalable du harnais de concurrence en phase 3.**

> **Porte de sortie** : aucun cycle de vie cadencé par ImGui ; aucune fenêtre de course entre CLI / chargement et premier cycle CPU ; même comportement avec ou sans thread de rendu.

### Phase 3 — Audio temps réel et concurrence réellement exercée (1-2 semaines)

- [ ] **Retirer verrous et allocations du callback audio** `[M · critical]` — remplacer `AudioDevice::sourcesMutex` dans `mixSources()` par un petit tableau fixe ou une liste immuable double-buffer publiée atomiquement. Les producteurs alimentent des rings lock-free ; décodage cassette, ajout / retrait de sources et destruction restent hors callback. Définir et tester la durée de vie garantissant qu'une source retirée n'est libérée qu'après le dernier callback qui peut encore la voir.
- [ ] **Faire voir le thread de rendu à TSan** `[M · solid]` — angle mort structurel : le job sanitizer nocturne lance `ctest`, or **seuls 7 des 94 fichiers de test instancient `EmulationController`** et aucun ne fait tourner d'UI. La seule paire de threads qui existe chez un utilisateur — *thread de rendu × thread d'émulation* — n'est donc jamais instrumentée. La discipline est pourtant bonne (audit du 23 août 2026 : **0 méthode sur 207 ne touche `memory->`/`cpu->` sans `stateMutex`**), mais c'est une propriété vérifiée à la lecture, pas par une machine. Le harnais doit lancer **simultanément** producteur `EmulationController`, consommateur snapshot / rendu synthétique et callback audio synthétique : smoke court par PR, campagne complète sous TSan nocturne. Préalable : la matrice headless de phase 2.
- [ ] **Étendre le rang des verrous aux mutex des cartes** `[S · nice]` — `LockOrder.h` couvre les trois verrous du cœur. `SID::chipMutex`, `TerminalCard::cardMutex` / `screenshotResultMutex` et les verrous du modem restent hors table ; leur donner un rang (sous `Snapshot`, ou dans une bande dédiée aux périphériques) étendrait la vérification au seul endroit où il reste des verrous non ordonnés. **Ne couvre pas la classe de défaut corrigée dans `CassetteDevice`** : un rang vérifie l'ORDRE, pas la DURÉE de détention ni une allocation sur le thread temps-réel — ni `LockOrder.h` ni TSan ne voient celle-là, seule une lecture du chemin d'appel la trouve.
- [ ] **Mesurer les invariants temps réel** `[S · solid]` — instrumentation debug / benchmark du temps maximal de détention de `stateMutex`, du temps du callback, des underruns et des débordements de rings ; seuils prudents dans un stress test, métriques désactivables et sans coût notable en Release.

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
