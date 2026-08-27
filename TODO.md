# TODO

Backlog **ouvert** de l’émulateur POM1. Le travail livré vit dans
[`CHANGELOG.md`](CHANGELOG.md), les décisions techniques dans le code et `git log`,
et le logiciel 6502 dans [`dev/TODO6502.md`](dev/TODO6502.md).

## Règles

- Une case décrit un résultat vérifiable, pas l’historique qui y a conduit.
- Effort : **S** (<1 j), **M** (1–5 j), **L** (>5 j). Impact : **nice**, **solid**, **critical**.
- Un chantier n’apparaît qu’une fois. Les dépendances sont indiquées explicitement.
- Une réalisation quitte immédiatement ce fichier pour `CHANGELOG.md`.
- 🚫 signifie qu’une ressource externe empêche réellement d’avancer.

## Maintenant — consolidation du cœur

POM1 est un monolithe modulaire sain et très testé. La topologie, le cycle de vie
des cartes et le chemin audio temps réel sont stabilisés. La priorité est désormais
de séparer l’état émulé des services hôte sans réécriture du CPU, du bus, des
renderers ou du format de snapshot.

### Régression prioritaire

- [ ] **Rétablir l’auto-connexion BBS depuis `software/NET`** `[S · critical]` — le chargement de `bbs.fozztexx.com.txt` ou `bbs.retrocampus.com.txt` branche/réinitialise encore le Wi-Fi Modem, mais ne lance plus le programme d’auto-dial à `$0280`. Identifier la rupture entre chargement, action différée et démarrage CPU ; rétablir le lancement automatique sans modifier le comportement manuel d’`ATmodem.txt`. Ajouter un test sans Internet avec connecteur factice qui épingle chargement à `$0280`, exécution, commande `ATDT` émise et tentative de connexion unique.

Architecture cible : panneaux UI → commandes et vues immuables → façade applicative
thread-safe → `MachineCoordinator` (`CpuRunner`, `CardTopology`, `StateManager`) →
espace d’adressage, `PeripheralBus` et périphériques. Audio, fichiers, réseau et
rendu sont injectés à la racine de l’application.

### 1. Chargeurs purs et entrées hostiles (1–2 semaines)

- [ ] **Extraire `MemoryImageLoader` et les parseurs purs** `[M · critical]` — recevoir des octets et retourner écritures, zones, adresse d’exécution et diagnostics, sans accès à `Memory`, à l’UI, à l’audio ou au système de fichiers. Valider le résultat complet avant toute mutation de la machine. Couvrir d’abord WOZMON hex, Intel HEX, TurboType et snapshots, puis AIFF et D64.
- [ ] **Durcir les contrats des formats** `[M · solid]` — tailles maximales, arithmétique de longueurs vérifiée, offsets d’erreur, CRC quand le format le permet et absence de modification partielle en cas d’échec.
- [ ] **Fuzzer chaque parseur pur** `[M · solid]` — une cible libFuzzer par format, corpus issu de `software/`, `cassettes/`, `sdcard/` et des fixtures ; smoke borné par PR, campagne ASan longue la nuit ; chaque crash devient un test de régression.

> Sortie : les entrées malformées ne crashent ni ne bloquent POM1 et ne modifient
> jamais partiellement la machine ; les fuzzers tournent en CI.

### 2. Services hôte injectés (1–2 semaines)

- [ ] **Introduire `ResourceLocator`** `[M · solid]` — centraliser cwd, bundle, ROM, disques, cassettes et ressources web ; supprimer les sondes implicites du constructeur de `Memory`.
- [ ] **Injecter audio, fichiers et réseau** `[M · critical]` — fournir `IAudioService`, `IFileSystemService` et des factories réseau depuis `main_imgui` ; les tests doivent construire CPU + mémoire sans matériel audio, socket ni fichiers implicites.

> Sortie : `Memory` ne crée plus d’`AudioDevice` et ne découvre aucune ressource
> hôte ; un test hermétique construit le cœur avec des doubles en mémoire.

### 3. Propriété des périphériques (2–4 semaines)

- [ ] **Créer `PeripheralManager`** `[L · critical]` — transférer depuis `Memory` la propriété et le cycle de vie des cartes, les bindings `PeripheralBus`, les endpoints audio/réseau et l’exécution des `TransitionPlan`. Préserver `memRead()`, `memWrite()` et `PeripheralBus` comme interfaces stables pendant la migration.
- [ ] **Abaisser le cliquet architectural à chaque extraction** `[S · solid]` — mettre à jour `architecture_baseline.json` uniquement vers le bas. Cibles de phase : `Memory` < 2 500 lignes et `Memory.h` inclus par moins de 25 unités de traduction.

> Sortie : `Memory` ne porte plus que l’espace d’adressage, PIA et MMIO cœur ;
> aucun cycle de vie de carte ne dépend d’elle.

### 4. Vues et snapshots indépendants (1–2 semaines)

- [ ] **Extraire les DTO publiés** `[M · solid]` — définir `CpuView`, `MachineView`, `CardView` et les snapshots de cartes hors des classes concrètes ; conserver temporairement des alias de compatibilité.
- [ ] **Libérer `EmulationSnapshot.h` des périphériques concrets** `[M · solid]` — retirer notamment les dépendances vers `JukeBox`, `CodeTank`, TMS9918, réseau et imprimante ; basculer les consommateurs d’enums vers `CardTypes.h`.

> Sortie : `EmulationSnapshot.h` ne contient que des types de vue stables et son
> fan-out n’entraîne plus la recompilation des implémentations de cartes.

### 5. Façade applicative mince (2–3 semaines)

- [ ] **Extraire `CpuRunner`** `[L · critical]` — pacing, run, pause, step et slices, sans changer la sémantique headless/WASM.
- [ ] **Extraire `StateManager`** `[L · critical]` — snapshots, sauvegarde, restauration et rewind ; la façade applicative conserve le verrouillage et la publication atomique.
- [ ] **Remplacer les passthroughs par des commandes structurées** `[M · solid]` — préférer `applyCardConfiguration`, `applyMachinePreset` et `executeMachineCommand` aux wrappers par carte ; supprimer chaque ancienne API dès son dernier appelant.
- [ ] **Abaisser `EmulationController` sous 1 500 lignes** `[M · solid]` — le cliquet doit mesurer simultanément lignes, méthodes publiques et fan-out.

> Sortie : le contrôleur coordonne les commandes thread-safe mais ne contient ni
> moteur CPU, ni gestionnaire d’état, ni logique propre à une carte.

## Ensuite — interface et industrialisation

### 6. UI par panneaux (4–6 semaines, incrémental)

- [ ] **Faire du registre de fenêtres une fabrique d’`IPanel`** `[L · solid]` — chaque panneau possède visibilité, état transitoire, géométrie, modèle de vue et `render(AppContext&)` ; `MainWindow_ImGui` conserve menus, docking, layout et orchestration.
- [ ] **Migrer un panneau par PR** `[L · solid]` — ordre : Silicon Strict et presets, panneaux de cartes, debug, dialogues fichier, puis éditeurs. Ajouter un test de logique par panneau migré.
- [ ] **Supprimer le miroir matériel de l’UI** `[M · critical]` — l’état des cartes provient exclusivement de `MachineView`/`CardSet` ; l’UI ne garde que les champs en édition et les erreurs de validation.
- [ ] **Unifier fenêtres, menus et raccourcis par identifiant stable** `[M · solid]` — ouvrir ensuite une palette de commandes dérivée du registre. Préserver l’interdiction des raccourcis Ctrl+lettre, réservés aux codes de contrôle Apple-1.
- [ ] **Réduire le noyau `MainWindow_ImGui`** `[M · solid]` — cible : moins de 500 lignes de déclaration et moins de 5 000 lignes cumulées d’orchestration `MainWindow_*`.

### 7. Qualité, sécurité et chaîne de livraison (1–2 semaines)

- [ ] **Mesurer la couverture par module** `[S · solid]` — publier couverture lignes/branches et définir des seuils sur les parseurs, la topologie, les snapshots et le cœur CPU plutôt qu’un pourcentage global trompeur.
- [ ] **Ajouter une analyse statique incrémentale** `[M · solid]` — `clang-tidy` sur le code POM1 modifié, avec baseline initiale explicite ; ne pas analyser le code vendu.
- [ ] **Passer Windows en warnings-as-errors** `[S · solid]` — nettoyer les conversions POM1 restantes, exclure `stb_vorbis.c`, puis activer `/WX` dans le job Windows.
- [ ] **Épingler les GitHub Actions par SHA** `[S · solid]` — conserver le tag lisible en commentaire et automatiser les mises à jour de dépendances.
- [ ] **Produire SBOM et inventaire de licences** `[M · solid]` — attacher les deux aux releases et vérifier les composants vendus/bundlés.
- [ ] **Ajouter des budgets de performance** `[M · solid]` — seuils reproductibles pour débit CPU, callback audio, application d’un preset et rewind ; alerter sur tendance avant de bloquer une PR.
- [ ] **Créer un bundle local de diagnostic** `[M · solid]` — *Aide → Signaler un problème* assemble versions, journal, snapshot et configuration dans un zip explicitement choisi par l’utilisateur, sans télémétrie automatique. Éviter du travail non async-signal-safe dans un handler fatal.
- [ ] **Alléger le dépôt Git** `[M · nice]` — inventorier les gros PDF, ZIP, vidéos, images et binaires FPGA ; conserver les sources indispensables, déplacer les archives vers releases/LFS ou un dépôt documentaire, puis documenter leur provenance.
- [ ] **Automatiser la porte de sortie de consolidation** `[S · solid]` — réunir warnings-as-errors sur trois OS, matrice headless, navigateur WASM, sanitizers, fuzz smoke, couverture et bundle de diagnostic dans une checklist release.

## Plus tard — produit et fidélité

Ces travaux sont ouverts mais ne doivent pas interrompre la consolidation ci-dessus.

### Packaging et plateformes

- [ ] **Charger paresseusement les cassettes WASM** `[S · nice]` — retirer du téléchargement initial les 2,5 Mo de `cassettes/`, notamment `WOZ_talk.mp3` ; ne pas complexifier le chargement de `cfcard.po` sans mesure justifiant le gain.
- [ ] **Valider la borne sur un Raspberry Pi réel** `[S · solid]` — vérifier démarrage kiosk, GLSL, audio à `POM1_AUDIO_LATENCY=120`, plein écran sans WM et restauration correcte par `--uninstall`.
- [ ] **Produire l’artefact Pi `cortex-a72` avec PGO en CI** `[M · nice]` — entraîner sur runner ARM64 et conserver l’AppImage aarch64 générique.

### Snapshot, rewind et automatisation

- [ ] **Fermer les trous résiduels du snapshot** `[M · nice]` — position de cassette en flux, reconnexion propre modem/terminal, état interne libresidfp si une API amont le permet, et pied SHA-256 v2.
- [ ] **Ajouter un IPC de scripting à l’exécution** `[M · nice]` — `--cmd-fd` ou socket locale portant les verbes CLI sans mélanger contrôle, clavier et affichage telnet.
- [ ] **Charger des presets externes validés** `[M · nice]` — format versionné, validation par `CardTopology`, table C++ de repli et tests sur le résultat plutôt que sur le texte source.
- [ ] **Optimiser les deltas rewind TMS9918** `[M · nice]` — dirty-tracking des pages VRAM, seulement après profilage du coût de capture.
- [ ] **Éviter les reconfigurations au seek rewind** `[S · nice]` — ne pas réappliquer cartes et ROM lorsque les flags sont inchangés.

### Graphismes et fidélité

- [ ] **Valider le fetch SAT TMS9918 une ligne en avance** `[M · solid]` — mesurer d’abord sur silicium les écritures SAT en zone active, puis modéliser et tester la latence observée.
- [ ] **Partager le journal `VideoEvents` et le rejeu beam** `[L · solid]` — extraire géométrie/journal hors de `Memory`, faire adopter `BeamClock` à GEN2 puis remplacer le rattrapage eager du TMS9918 ; sérialiser le journal.
- [ ] **Ajouter la fidélité CRT 1976 optionnelle** `[M · nice]` — streaming du registre à décalage et bruit périodique déterministe, désactivés par défaut et couverts par une référence visuelle.

### Outils et langages

- [ ] **Étendre le débogage source au C et au WASM** `[M · solid]` — transporter les `.dbg` via cl65/cc65 web et prendre en charge plusieurs points d’arrêt.
- [ ] **Ajouter les exemples LOGO au popup du Bench** `[S · solid]` — exposer les dix fichiers existants sous un groupe LOGO en un clic.
- [ ] **Ajouter les variables chaîne au BASIC natif** `[L · nice]` — descripteurs ptr+len, heap, runtime chaîne, expressions typées et tests de pression mémoire.

### Périphériques

- [ ] **Intégrer le bootloader `flowenol/apple1-serial`** `[S · solid]` — choisir explicitement Terminal Card ou variante ACIA et réutiliser le pipeline de chargement pur.
- [ ] **Ajouter un périphérique SpeakJet/TTS optionnel** `[M · nice]` — router l’UART vers un backend TTS injecté sans dupliquer 6522/6551 ni rendre le service obligatoire.

## 🚫 Bloqué

- [ ] 🚫 **Chargeur TurboType 57 600 bauds** `[M · solid]` — attendre la spécification détaillée et une ROM/binaire du dropper d’Uncle Bernie ; à réception, implémenter parser `.TUR`/`.APL`, injection 8 bits sans écho, CRC, sentinelle et retour Woz Monitor.
