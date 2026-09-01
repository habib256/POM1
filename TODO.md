# TODO — ce qui reste, et ce qui ne se fera pas

Ce fichier n'est pas une file de sprint : c'est la **liste de passation**. Il dit
ce qui peut encore être terminé, ce qui serait beau mais demande du temps, ce qui
n'a de sens que si le développement continue, et ce qui a été délibérément
écarté. Le travail livré vit dans [`CHANGELOG.md`](CHANGELOG.md), les décisions
techniques dans le code et `git log`.

**Le logiciel 6502 a son propre backlog, [`dev/TODO6502.md`](dev/TODO6502.md),
et c'est la meilleure porte d'entrée** pour quelqu'un qui découvre le projet :
des programmes d'un après-midi, à résultat visible à l'écran, sur des
bibliothèques déjà écrites et testées. Ce fichier-ci est celui de l'émulateur, et
il est plus aride.

Architecture pour les humains : [`ARCHITECTURE.md`](ARCHITECTURE.md). Comment
contribuer : [`CONTRIBUTING.md`](CONTRIBUTING.md). Invariants et pièges :
[`CLAUDE.md`](CLAUDE.md).

## Règles

- Une case décrit un résultat vérifiable, pas l'historique qui y a conduit.
- Effort : **S** (<1 j), **M** (1–5 j), **L** (>5 j). Impact : **nice**, **solid**, **critical**.
- Une réalisation quitte immédiatement ce fichier pour `CHANGELOG.md`.
- 🚫 signifie qu'une ressource externe empêche réellement d'avancer.

## Où en est le projet

127 tests verts en ~2 min, deux oracles CPU cycle-exacts, quatre campagnes de
fuzzing dont deux ont trouvé de vrais défauts, ordre des verrous prouvé, cinq
tiers de CI en warnings-as-errors. Un clone neuf compile et passe la suite.

Les trois chantiers structurants sont **clos** :

1. **Isoler l'environnement de développement** — `-DPOM1_DEVTOOLS=OFF` produit un
   émulateur complet qui passe `ctest -L emulator` ; la frontière tient à 16
   arêtes dans 4 fichiers et `architecture_check` refuse la 17ᵉ. Mesure : 149 →
   104 unités de traduction, 4,13 → 2,86 Mo de binaire, 27 → 20 s de build.
2. **Services hôte injectés** — plus aucun test n'ouvre le périphérique audio de
   l'hôte ; un cœur nu se construit en 0,3 ms au lieu de 135 ms. Une seule
   recherche de ressources (`ResourceLocator`), et `resource_probes_sync` refuse
   la 53ᵉ sonde écrite à la main.
3. **Sortir les décisions de l'UI** — neuf seams purs, tous à 100 % de couverture
   de lignes ; le module `ui` passe de 2,4 % à 3,9 % sur 14 468 lignes, les neuf
   autres modules inchangés. Ce qui reste non couvert dessine.

Les grands refactors d'architecture planifiés puis écartés (`PeripheralManager`,
`CpuRunner`, `StateManager`, migration panneau par panneau) sont documentés en
bas de ce fichier, avec la raison et la condition de réactivation.

## 1. Finissable — bornée, vérifiable, une session chacune

Le travail qu'on peut réellement terminer, par ordre de rendement.

- [ ] **Décider du packaging** `[S · solid]` — la mesure ci-dessus est là, il reste à trancher : release unique avec outillage (statu quo), ou build « émulateur seul » pour la borne et le WASM. Deux points à traiter avec la décision : les jobs de `release.yml` embarquent cc65 inconditionnellement, et le préchargement WASM de `dev/` + `sketchs/` (~310 Ko) n'est utile qu'à la DevBench — les deux ne coûtent rien tant que `POM1_DEVTOOLS=ON` reste le défaut de release.
- [ ] **Charger paresseusement les cassettes WASM** `[S · nice]` — retirer du téléchargement initial les 2,5 Mo de `cassettes/`, notamment `WOZ_talk.mp3` ; ne pas complexifier le chargement de `cfcard.po` sans mesure justifiant le gain.
- [ ] **Intégrer le bootloader `flowenol/apple1-serial`** `[S · solid]` — choisir explicitement Terminal Card ou variante ACIA et réutiliser le pipeline de chargement pur.
- [ ] **Éviter les reconfigurations au seek rewind** `[S · nice]` — ne pas réappliquer cartes et ROM lorsque les flags sont inchangés.
- [ ] **Automatiser la porte de sortie de consolidation** `[S · solid]` — réunir warnings-as-errors sur trois OS, matrice headless, navigateur WASM, sanitizers, fuzz smoke, couverture et bundle de diagnostic dans une checklist release.

## 2. Le beau travail — grand, cadré, pas urgent

Ce qui reste d'intéressant. Rien ici n'est bloquant ; tout y est du vrai
artisanat d'émulation ou d'outillage, et chacun peut être pris isolément.

- [ ] **Partager le journal `VideoEvents` et sérialiser le rejeu beam** `[L · solid]` — **l'adoption de `BeamClock` par le GEN2 est faite** (`CHANGELOG.md`) : les deux moteurs vidéo calculent désormais cycle→(ligne, colonne) par la même horloge, prouvé exhaustivement sur deux trames entières. Reste : extraire la géométrie et le journal hors de `Memory`, remplacer le rattrapage *eager* du TMS9918 par un `renderUntil(beam)` bâti sur la même horloge, et sérialiser le journal dans le snapshot pour que le rewind rejoue le faisceau. Le dernier point est le seul qui touche au format de snapshot — à traiter séparément.
- [ ] **Confirmer le modèle de synchro trame sur du matériel réel** `[M · solid]` — `src/TerminalTiming.h` livre `FieldSync` : PB7 reste occupé jusqu'au prochain passage du balayage plutôt qu'un décompte fixe, ce qui est le comportement d'un terminal à registre à décalage. Le débit est préservé (une écriture par trame, épinglé), mais **le point de verrouillage est modélisé au bord de trame alors qu'en vrai il suit le curseur qui descend l'écran**. Confirmer à l'oscilloscope sur une section terminal, ou depuis les notes de timing de Woz ; ni l'un ni l'autre n'est dans cet arbre. Tant que ce n'est pas fait, le modèle reste hors bundle et désactivé par défaut (`--display-field-sync`). Le bruit périodique déterministe reste à faire.
- [ ] **Valider le fetch SAT TMS9918 une ligne en avance** `[M · solid]` — mesurer d’abord sur silicium les écritures SAT en zone active, puis modéliser et tester la latence observée.
- [ ] **Charger des presets externes validés** `[M · nice]` — format versionné, validation par `CardTopology`, table C++ de repli et tests sur le résultat plutôt que sur le texte source.
- [ ] **Ajouter un IPC de scripting à l’exécution** `[M · nice]` — `--cmd-fd` ou socket locale portant les verbes CLI sans mélanger contrôle, clavier et affichage telnet.
- [ ] **Ajouter les variables chaîne au BASIC natif** `[L · nice]` — descripteurs ptr+len, heap, runtime chaîne, expressions typées et tests de pression mémoire.
- [ ] **Ajouter un périphérique SpeakJet/TTS optionnel** `[M · nice]` — router l’UART vers un backend TTS injecté sans dupliquer 6522/6551 ni rendre le service obligatoire.
- [ ] **Étendre le débogage source au C et au WASM** `[M · solid]` — transporter les `.dbg` via cl65/cc65 web et prendre en charge plusieurs points d’arrêt.

> **Un point de départ concret pour le rejeu beam** : `src/BeamClock.h` existe
> déjà, pur et testable, et son propre en-tête nomme ce qui manque — « GEN2 keeps
> its own absolute-cycle journal today; it can adopt this geometry once the
> journal/replay path is unified ». Le TMS9918 l'utilise, le GEN2 non. Faire
> adopter `BeamClock` au GEN2 **sans toucher au format de snapshot** est un
> premier pas isolé, vérifiable par les images témoins existantes.

## 3. Entretien — n'a de sens que si le développement continue

**Si le projet s'arrête, cette section est caduque.** Ce sont des investissements
dans la vitesse d'un développement futur, pas dans la qualité de ce qui est
livré : rien ici ne corrige un défaut ni n'ajoute une capacité.

- [ ] **Ajouter une analyse statique incrémentale** `[M · solid]` — `clang-tidy` sur le code POM1 modifié, avec baseline initiale explicite ; ne pas analyser le code vendu.
- [ ] **Ajouter des budgets de performance** `[M · solid]` — seuils reproductibles pour débit CPU, callback audio, application d'un preset et rewind ; alerter sur tendance avant de bloquer une PR.
- [ ] **Créer un bundle local de diagnostic** `[M · solid]` — *Aide → Signaler un problème* assemble versions, journal, snapshot et configuration dans un zip explicitement choisi par l'utilisateur, sans télémétrie automatique. Éviter du travail non async-signal-safe dans un handler fatal.
- [ ] **Produire SBOM et inventaire de licences** `[M · solid]` — attacher les deux aux releases et vérifier les composants vendus/bundlés.
- [ ] **Libérer `EmulationSnapshot.h` des périphériques concrets** `[M · solid]` — retirer les dépendances vers `JukeBox`, `CodeTank`, TMS9918, réseau et imprimante ; basculer les consommateurs d'enums vers `src/CardTypes.h`. Justification mesurable : temps de recompilation, pas esthétique. À faire quand le fan-out coûte réellement.
- [ ] **Remplacer les passthroughs par des commandes structurées** `[M · solid]` — préférer `applyCardConfiguration` et `setCardEnabled` aux wrappers par carte sur `EmulationController` (~215 méthodes publiques) ; supprimer chaque ancienne API dès son dernier appelant. Aucun grand refactor : on ne retire que ce qui n'a plus d'appelant.
- [ ] **Fermer les trous résiduels du snapshot** `[M · nice]` — position de cassette en flux, reconnexion propre modem/terminal, état interne libresidfp si une API amont le permet, et pied SHA-256 v2.
- [ ] **Optimiser les deltas rewind TMS9918** `[M · nice]` — dirty-tracking des pages VRAM, seulement après profilage du coût de capture.
- [ ] **Produire l’artefact Pi `cortex-a72` avec PGO en CI** `[M · nice]` — entraîner sur runner ARM64 et conserver l’AppImage aarch64 générique.

## 4. Hors de portée sans quelque chose — ou quelqu'un — d'autre

- [ ] **Valider la borne sur un Raspberry Pi réel** `[S · solid]` — vérifier démarrage kiosk, GLSL, audio à `POM1_AUDIO_LATENCY=120`, plein écran sans WM et restauration correcte par `--uninstall`.
- [ ] 🚫 **Chargeur TurboType 57 600 bauds** `[M · solid]` — attendre la spécification détaillée et une ROM/binaire du dropper d'Uncle Bernie ; à réception, implémenter parser `.TUR`/`.APL`, injection 8 bits sans écho, CRC, sentinelle et retour Woz Monitor. C'est le plus réjouissant des items bloqués : charger à 57 600 bauds dans une machine de 1976.

- [ ] **Alléger le dépôt Git** `[M · nice]` — inventorier les gros PDF, ZIP, vidéos, images et binaires FPGA ; conserver les sources indispensables, déplacer les archives vers releases/LFS ou un dépôt documentaire, puis documenter leur provenance. Repère : `.git` pèse 387 Mo, dont un `.po` de 32 Mo, un PDF de 18,5 Mo et une vidéo de 8,3 Mo. Le coût de ce chantier ne fait qu'augmenter.

> **Avis contraire sur l'allègement du dépôt, si le projet s'arrête.** La logique
> « le coût ne fait qu'augmenter » s'inverse à l'arrêt : réécrire l'historique
> d'un dépôt qu'on s'apprête à figer casse tous les clones existants et tous les
> liens de commit, pour un gain purement esthétique sur un arbre qui ne grossira
> plus. À ne faire que si le développement reprend vraiment.

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
