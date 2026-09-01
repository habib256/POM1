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

## Maintenant — trois chantiers, dans cet ordre

POM1 est un monolithe modulaire sain et très testé : 118 tests verts en ~55 s,
oracles CPU cycle-exacts, fuzzers qui ont trouvé de vrais défauts, ordre des
verrous prouvé. Le cœur n'est pas le problème.

Le problème est le **périmètre** : sur ~85 600 lignes de `src/`, l'émulateur
proprement dit en représente moins de 30 %, l'UI 20 % sans couverture directe,
et l'outillage de développement 21 % — un second produit qui partage un
processus, un build, une matrice de portage et un mainteneur. Les trois
chantiers ci-dessous attaquent cela dans l'ordre du risque réel.

Les grands refactors d'architecture précédemment planifiés (`PeripheralManager`,
`CpuRunner`, `StateManager`, migration panneau par panneau) sont **écartés** :
voir la section dédiée plus bas. Ils totalisaient 11 à 19 semaines de refactor
pur, sans valeur utilisateur, sur un cœur actuellement stable.

Architecture pour les humains : [`ARCHITECTURE.md`](ARCHITECTURE.md). Invariants
et pièges : [`CLAUDE.md`](CLAUDE.md).

### 1. Isoler l'environnement de développement (une décision restante)

L'isolation est livrée (`CHANGELOG.md`) : `-DPOM1_DEVTOOLS=OFF` produit un
émulateur complet, sans avertissement, qui démarre et passe `ctest -L emulator`
(90/90) ; la frontière tient à 16 arêtes dans 4 fichiers `MainWindow_*` et
`architecture_check` refuse la 17ᵉ. Mesure macOS/Metal, arbre neuf, `-j8`,
tests désactivés : **149 → 104** unités de traduction, **4,13 Mo → 2,86 Mo**
de binaire (−31 %), **27 s → 20 s** de build.

- [ ] **Décider du packaging** `[S · solid]` — la mesure ci-dessus est là, il reste à trancher : release unique avec outillage (statu quo), ou build « émulateur seul » pour la borne et le WASM. Deux points à traiter avec la décision : les jobs de `release.yml` embarquent cc65 inconditionnellement, et le préchargement WASM de `dev/` + `sketchs/` (~310 Ko) n'est utile qu'à la DevBench — les deux ne coûtent rien tant que `POM1_DEVTOOLS=ON` reste le défaut de release.

### 2. Services hôte injectés — livré

L'injection du service audio est livrée (`CHANGELOG.md`), et la construction
paresseuse de `pom1::SID` avec elle : la mesure qui justifiait l'injection était
mal attribuée — dans un binaire de test, `AudioDevice(false)` coûte **0,07 ms**,
et les ~120 ms d'un cœur hermétique étaient la **première construction de
`pom1::SID`**. Les deux gains sont mesurés : plus aucun test n'ouvre le
périphérique audio de l'hôte, et un cœur nu se construit en 0,3 ms au lieu de
135 ms (`hermetic_core_smoke` §5).

Le gel de `Memory` est livré et devient une règle permanente, plus un chantier :
`architecture_check` mesure `memory_public_methods` (188) et
`controller_public_methods` (201) et refuse toute croissance — aucune nouvelle
méthode publique, aucun nouvel inclus de `Memory.h` (59 unités de traduction),
et toute extraction abaisse les plafonds.

La découverte de ressources est livrée elle aussi : les 52 sondes `../` écrites
à la main passent toutes par `ResourceLocator`, et `resource_probes_sync`
(`tools/check_resource_probes.py`) refuse la 53ᵉ.

> **Chantier clos.** `Memory` ne crée plus d'`AudioDevice` ni de `pom1::SID`
> dans l'application livrée, `hermetic_core_smoke` §4-§5 construit le cœur sur
> un double en mémoire en 0,3 ms, et il n'existe plus qu'un seul ordre de
> recherche pour les données de POM1.

### 3. Sortir les décisions de l'UI (2–3 semaines restantes, incrémental)

17 100 lignes de `MainWindow_*` sans test direct, et c'est là que vivaient les
défauts connus (backspace destructif, six défauts du plein écran, auto-dial BBS
perdu). La méthode a déjà fait ses preuves — `src/Apple1KeyMap.h`,
`src/WindowGeometry.h`, `src/StagedCardConfiguration.h` — mais ne couvre que
quelques centaines de lignes. **Viser les ~2 000 lignes qui portent des
décisions, pas les 17 000 qui dessinent.**

Le miroir matériel est supprimé (`CHANGELOG.md`) : les seize `bool xEnabled` de
`MainWindow` n'existent plus, l'état des cartes vient de `currentCards()` —
transaction en cours si elle est ouverte, `CardSet` publié sinon — et les
commandes passent par `setCardPlugged()`. Reste, pour ce chantier :

- [ ] **Extraire les décisions de layout et de plein écran** `[M · solid]` — la règle d'attente sur `DisplaySize`, l'arbitrage plein écran natif macOS, la persistance `.size` et le choix « quelle géométrie s'applique » sont des fonctions pures ; les six défauts d'août 2026 y vivaient et aucun n'était épinglable sur place.
- [ ] **Extraire les décisions de presets et de topologie** `[M · solid]` — quel preset, quelles cartes, quel ROM, quelles fenêtres ouvertes : `src/MachinePresets.h` et `src/CardTopology.h` portent déjà les données et la politique ; l'UI ne doit plus contenir que la composition.
- [ ] **Unifier fenêtres, menus et raccourcis par identifiant stable** `[M · solid]` — un registre unique alimentant menus, raccourcis et persistance, puis une palette de commandes qui en dérive. Préserver l'interdiction des raccourcis Ctrl+lettre, réservés aux codes de contrôle Apple-1 (`shortcuts_sync`).
- [ ] **Un test par décision extraite** `[S · solid]` — chaque extraction arrive avec son smoke test qui ne lie ni ImGui ni GLFW, comme `mainwindow_logic_smoke` et `staged_card_configuration_smoke`.

> Sortie : les décisions de l'UI sont testées hors ImGui ; `MainWindow_*` ne
> contient plus que menus, docking, dessin et orchestration.

## Ensuite

### 4. Dette ciblée (à traiter quand elle gêne, pas avant)

- [ ] **Libérer `EmulationSnapshot.h` des périphériques concrets** `[M · solid]` — retirer les dépendances vers `JukeBox`, `CodeTank`, TMS9918, réseau et imprimante ; basculer les consommateurs d'enums vers `src/CardTypes.h`. Justification mesurable : temps de recompilation, pas esthétique. À faire quand le fan-out coûte réellement.
- [ ] **Remplacer les passthroughs par des commandes structurées** `[M · solid]` — préférer `applyCardConfiguration` et `setCardEnabled` aux wrappers par carte sur `EmulationController` (~215 méthodes publiques) ; supprimer chaque ancienne API dès son dernier appelant. Aucun grand refactor : on ne retire que ce qui n'a plus d'appelant.

### 5. Qualité, sécurité et chaîne de livraison (1–2 semaines)

- [ ] **Mesurer la couverture par module** `[S · solid]` — publier couverture lignes/branches et définir des seuils sur les parseurs, la topologie, les snapshots et le cœur CPU plutôt qu'un pourcentage global trompeur.
- [ ] **Ajouter une analyse statique incrémentale** `[M · solid]` — `clang-tidy` sur le code POM1 modifié, avec baseline initiale explicite ; ne pas analyser le code vendu.
- [ ] **Passer Windows en warnings-as-errors** `[S · solid]` — nettoyer les conversions POM1 restantes, exclure `stb_vorbis.c`, puis activer `/WX` dans le job Windows.
- [ ] **Épingler les GitHub Actions par SHA** `[S · solid]` — conserver le tag lisible en commentaire et automatiser les mises à jour de dépendances.
- [ ] **Produire SBOM et inventaire de licences** `[M · solid]` — attacher les deux aux releases et vérifier les composants vendus/bundlés.
- [ ] **Ajouter des budgets de performance** `[M · solid]` — seuils reproductibles pour débit CPU, callback audio, application d'un preset et rewind ; alerter sur tendance avant de bloquer une PR.
- [ ] **Créer un bundle local de diagnostic** `[M · solid]` — *Aide → Signaler un problème* assemble versions, journal, snapshot et configuration dans un zip explicitement choisi par l'utilisateur, sans télémétrie automatique. Éviter du travail non async-signal-safe dans un handler fatal.
- [ ] **Alléger le dépôt Git** `[M · nice]` — inventorier les gros PDF, ZIP, vidéos, images et binaires FPGA ; conserver les sources indispensables, déplacer les archives vers releases/LFS ou un dépôt documentaire, puis documenter leur provenance. Repère : `.git` pèse 387 Mo, dont un `.po` de 32 Mo, un PDF de 18,5 Mo et une vidéo de 8,3 Mo. Le coût de ce chantier ne fait qu'augmenter.
- [ ] **Automatiser la porte de sortie de consolidation** `[S · solid]` — réunir warnings-as-errors sur trois OS, matrice headless, navigateur WASM, sanitizers, fuzz smoke, couverture et bundle de diagnostic dans une checklist release.

## Écarté — architecture non retenue pour l'instant

Ces chantiers ont été planifiés puis écartés après évaluation. Ils ne sont pas
absurdes ; ils sont **mal rentables à un mainteneur** : refactor pur, sans
valeur utilisateur, avec un risque de régression réel sur un cœur stable et
vert. Conservés ici pour être réactivés si un besoin concret apparaît — et ce
besoin doit être nommé au moment de la réactivation.

- **`PeripheralManager`** `[L]` — transférer depuis `Memory` la propriété et le cycle de vie des cartes. Écarté : `src/CardTopology.h` et `src/MachineCoordinator.h` fournissent déjà la politique et le plan de transition ; déplacer la propriété ne corrige aucun défaut connu. Remplacé par « geler `Memory` » (chantier 2). *Réactiver si* : une deuxième machine (POM2) doit partager la gestion des cartes.
- **`CpuRunner` et `StateManager`** `[L]` — extraire le pacing et l'état hors de `EmulationController`. Écarté : le découpage en 4 unités de traduction a déjà réglé la lisibilité, et le contrôleur est le seul point où l'ordre des verrous est tenu — le fragmenter déplace le risque sans le réduire. *Réactiver si* : un second frontend a besoin du moteur sans le contrôleur.
- **Abaisser `EmulationController` sous 1 500 lignes** `[M]` — cible de ligne sans défaut associé. Remplacé par la suppression des passthroughs au fil de l'eau (chantier 4).
- **Fabrique d'`IPanel`, migration panneau par panneau, et cible « noyau `MainWindow_ImGui` sous 500 lignes »** `[L]` — 4 à 6 semaines pour ré-héberger du code de dessin déjà fonctionnel. Écarté au profit de l'extraction des seules décisions (chantier 3), qui apporte la testabilité sans la réécriture. *Réactiver si* : les panneaux doivent devenir dynamiques (plugins, panneaux externes).
- **Extraire les DTO `CpuView` / `MachineView` / `CardView`** `[M]` — utile en principe, mais `src/SnapshotPublisher.h` remplit déjà le rôle. Ne garder que la partie mesurable : libérer `EmulationSnapshot.h` (chantier 4).


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
