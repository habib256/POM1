# Changelog

Notable shipped work, recorded as it ships — both the **emulator** (lifted from
`TODO.md`) and the **6502 software** (`dev/lib/` libraries, `dev/codetank/`
cartridge composition, `sketchs/` programs — lifted from `dev/TODO6502.md`). The authoritative commit-level history
is `git log`; the user-facing feature tour is `README.md`; open work lives in
`TODO.md` (emulator) and `dev/TODO6502.md` (6502). Format loosely follows
[Keep a Changelog](https://keepachangelog.com/). Versions track the string in
`src/main_imgui.cpp` / `README.md`.

## [Unreleased]

### Changed — le journal de commutateurs appartient enfin à la carte

Dernier déplacement du chantier beam. Le journal par trame — l'enregistrement des
bascules mode/page mi-ligne que le renderer rejoue pour découper une trame —
vivait dans `Memory`, qui ne possède aucun timing vidéo, alors que tout le reste
de l'état de la carte vivait dans `Gen2VideoScanner`. Il y est désormais, à côté
du compteur de cycles qui date ses entrées et de l'état d'affichage que ses
entrées modifient : `journalEvent` / `publishFrame` / `resetJournal` /
`restorePublishedFrame`.

**Relocalisation pure** : les octets `GEN2VID` du snapshot sont inchangés, et les
deux accesseurs publics de `Memory` se contentent de transmettre — donc
`memory_public_methods` ne bouge pas. `memory_lines` descend de **4012 à 3988**
et le plafond suit : c'est le sens dans lequel ce cliquet est censé aller.

Le chantier beam est clos à une décision près, et `TODO.md` la formule comme
telle plutôt que comme une tâche : faut-il que `TMS9918::advanceCycles` passe
d'un commit progressif par ligne à un `renderUntil(beam)` paresseux ? Ce n'est
pas évidemment un défaut — ce commit progressif est précisément ce qui rend
visibles les changements mi-trame dont vivent les démos raster, et le renderer
est calibré par image témoin. À trancher avec une mesure, pas au jugé.

### Added — un rewind rejoue-t-il vraiment le faisceau ? Maintenant c'est prouvé

Les bascules de commutateurs mi-trame du GEN2 vivent dans un journal par trame,
sérialisé dans le snapshot (`GEN2VID`, v5+) précisément pour qu'une restauration
ne les perde pas. `MemorySnapshot.cpp` l'écrit noir sur blanc : sans lui, une
trame à split restaurée « perd ses bascules par ligne et affiche le simple
verrou de fin de trame ».

**Rien ne testait cette affirmation.** Les tests de snapshot vérifient que la
section EXISTE et que son nom n'entre pas en collision ; les tests beam rendent
des splits mais n'en sauvegardent jamais un. La seule chose pour laquelle la
sérialisation avait été écrite n'était pas vérifiée.

`gen2_journal_snapshot_smoke` journalise une bascule TEXT mi-trame, la publie au
basculement de trame, prend un snapshot, restaure dans une machine neuve, et
compare **les pixels rendus**. Sa §4 est un **contrôle** : elle re-rend la
machine restaurée journal jeté et exige que l'image change — sans quoi tout le
test passerait aussi contre un snapshot qui aurait perdu le journal.

**Ce contrôle a servi deux fois**, et c'est l'argument pour en écrire :

- la page texte était remplie de `$20` — un espace **inverse**, donc un bloc
  blanc plein, pixel pour pixel identique au remplissage HGR tous bits à un
  qu'il était censé contraster. Le test ne distinguait pas les deux modes.
  L'espace normal est `$A0` ;
- la phase du scanner GEN2 est **aléatoire à l'allumage** (fidélité silicium),
  donc le split tombait sur une ligne différente à chaque exécution. Le test la
  fige comme le fait le chemin headless de la CLI, et **dérive** la ligne du
  split depuis le journal au lieu de la supposer — affirmer contre une ligne
  codée en dur, c'eût été affirmer contre mon arithmétique plutôt que contre la
  machine.

Cinq exécutions consécutives, résultat identique.

Fan-out `GraphicsCard.h` 10 → 11 et `Memory.h` 59 → 60 : un test dont le sujet
est « un snapshot de cette machine rejoue-t-il le faisceau de cette carte ? » ne
peut pas s'écrire sans inclure les deux.

### Changed — le TMS9918 rejoint l'horloge partagée, et un décalage d'un cycle est épinglé

Suite immédiate de l'adoption GEN2 ci-dessous. `TMS9918.cpp` construisait déjà
une `pom1::BeamGeometry` et appelait `beamPosAt` pour le rattrapage de faisceau,
mais portait **à côté deux copies écrites à la main** de la même division
cycle→ligne (`frameCycleCounter * 262 / kCyclesPerFrame`), plus un `262` en dur
dans la géométrie elle-même. Les trois sites passent par `pom1::rawLineAt` et une
constante nommée `kTotalScanlines`. Aucun changement de comportement : `rawLineAt`
écrête à une trame là où le calcul inline pouvait la dépasser, ce qui est invisible
car les deux résultats sont ensuite repliés sur `kScreenHeight` par le `std::min`
qui suit.

**Un décalage d'un cycle, trouvé en épinglant la relation.**
`kActiveDisplayCycles` est le **dernier cycle de la ligne 191**, pas le premier de
la VBlank : l'expression prend le plancher là où la frontière de ligne prend le
plafond. `renderBeamCatchUp` testant `>= kActiveDisplayCycles`, ce cycle unique de
la dernière ligne active est traité comme de la VBlank par le rattrapage.
**Délibérément laissé tel quel** — le renderer est épinglé par une image témoin,
la ligne est de toute façon committée entière au `advanceCycles` suivant, et
« corriger » un cas sur 17 062 dans un chemin calibré coûterait plus qu'il ne
rapporte. Épinglé par `tms9918_per_scanline` pour que ça reste un écart connu
plutôt qu'une incohérence inexpliquée.

Les constantes de temps NTSC (`kCyclesPerFrame`, `kTotalScanlines`,
`kActiveDisplayCycles`) rejoignent le bloc « Public timing constants » qui
existait déjà à côté d'elles.

**Ce qui n'est PAS fait**, et le `TODO.md` le dit maintenant : le commit
progressif par ligne de `advanceCycles` n'est pas devenu un `renderUntil(beam)`
paresseux. Ce n'est pas évident — le commit progressif est précisément ce qui rend
visibles les changements mi-trame — et cela méritera sa propre décision.

### Changed — le GEN2 adopte l'horloge beam partagée

POM1 a deux moteurs vidéo cycle-exacts et ils calculaient chacun leur position
de faisceau : le TMS9918 par `pom1::beamPosAt` (`src/BeamClock.h`), le GEN2 par
une formule privée dans `GraphicsCard::frameCycleToPos`, portée verbatim de
l'`Apple2Display` de POM2. L'en-tête de `BeamClock.h` nommait lui-même l'écart —
« GEN2 keeps its own absolute-cycle journal today; it can adopt this geometry
once the journal/replay path is unified ». C'est cette adoption.

`Gen2VideoScanner::beamGeometry()` décrit la carte dans le vocabulaire partagé.
L'unité horizontale native du GEN2 est la **colonne d'octet**, pas le pixel : la
carte émet un octet d'affichage par cycle CPU, 40 par ligne à partir du cycle 25.
Avec `ticksPerCpuCycle = ticksPerPixel = 1`, un *tick* est un cycle est une
colonne — et la fonction partagée se réduit exactement à l'arithmétique
historique.

**Prouvé exhaustivement, pas par échantillonnage** : `gen2_beam_geometry_smoke`
balaie **chaque cycle** d'une trame de 262 lignes et d'une trame de 312 (37 310
au total), comparés à la formule d'avant **re-dérivée dans le test** plutôt
qu'appelée — demander au nouveau code d'être d'accord avec lui-même ne prouve
rien.

**Le balayage a servi dès la première exécution.** Router la colonne par le `x`
de `beamPosAt` était faux : `x` est annulé hors zone active, or
`forEachBeamSegment` **trie** les événements par `(scanline, byteCol)`,
événements VBL compris — et ce sont précisément ceux qui fixent l'état de départ
de la trame suivante. Les réduire tous à la colonne 0 aurait laissé leur ordre
relatif à un tri instable. D'où deux primitives nouvelles dans `BeamClock.h` :
`lineTickAt` (l'ordinal intra-ligne, défini pendant le blanking) et `rawLineAt`
(la ligne non écrêtée), `beamPosAt` étant réexprimé sur les deux. Le défaut est
apparu au cycle 12506 d'une trame de 262 lignes.

`beam_clock_smoke` gagne une section pour ces primitives — elle aussi corrigée
deux fois : elle supposait `cyclesPerFrame / totalLines` exact, ce qui vaut pour
le GEN2 (17030/262 = 65) mais **pas** pour le TMS9918, dont la trame ne se divise
pas en lignes entières. Les propriétés sont désormais affirmées en balayant et en
détectant les changements de ligne, sans hypothèse d'inverse — et la remise à
zéro de l'ordinal est affirmée comme *remise à zéro*, pas comme valeur exacte,
parce que l'arithmétique d'origine peut rendre 1 au premier cycle d'une ligne
quand la division tombe mal. Comportement hérité délibérément : le rendu TMS9918
est calibré dessus.

Ce qui prouve que **le beam fonctionne** reste inchangé et vert :
`gen2_beam_race_smoke` (splits verticaux), `gen2_horizontal_split_smoke` (splits
mi-ligne, la fonctionnalité phare de la carte de Bernie),
`gen2_floatingbus_smoke` et l'image témoin `gfx_regress_gen2_testcard`.

Fan-out de `GraphicsCard.h` 9 → 10 : un test dont le sujet est cette carte ne
peut pas s'écrire sans l'inclure — même cas que `hermetic_core_smoke` pour
`Memory.h`.

### Added — fidélité du terminal 1976 : PB7 peut se verrouiller sur le balayage

L'affichage de l'Apple-1 n'est pas une mémoire d'écran. Le texte recircule dans
un registre à décalage balayé au rythme vidéo, donc un caractère ne peut être
verrouillé que lorsque le balayage atteint le curseur : **PB7 reste occupé
jusqu'au passage suivant**, pas pendant un intervalle fixe compté depuis
l'écriture. POM1 utilisait depuis toujours un décompte fixe de `CPU_HZ/60`.

`src/TerminalTiming.h` (en-tête pur, `constexpr`) porte les deux modèles.
`FixedDelay` reste le défaut — c'est ce sur quoi chaque programme livré et
chaque image témoin sont validés. `FieldSync` verrouille l'attente sur la trame
de 60 Hz : **65 × 262 = 17030 cycles**, la période raster résolue avec Uncle
Bernie dans `doc/GEN2_RELEASE.md` Q4, que le `1/60 s` dérivé de POM1 manquait de
15 cycles — un écart sans conséquence tant que rien n'y était verrouillé, ce que
`FieldSync` change précisément.

**Ce qui rend le changement sûr** : sous `FieldSync` l'attente varie avec la
phase — c'est tout l'intérêt — mais **l'intervalle entre deux écritures ne varie
pas**. Un programme qui imprime aussi vite que la machine le permet imprime
toujours exactement un caractère par trame, quelle que soit la phase de départ
et quel que soit le nombre de cycles brûlés par sa boucle d'attente. Les 60
caractères par seconde documentés survivent donc au changement de modèle ; seule
la répartition de l'attente se déplace. C'est la section 3 de
`terminal_timing_smoke`, et c'est l'assertion qui autorisait à toucher au chemin
de timing le plus exécuté de la machine.

Accessible par `--display-field-sync` et par une case dans l'inspecteur Silicon
Strict. **Aucun preset ne l'arme**, contrairement à ses voisins : le modèle est
*raisonné depuis le schéma, pas mesuré sur une section terminal*. L'en-tête dit
ce qui est établi, ce qui est arithmétique et ce qui est hypothèse — le point de
verrouillage est modélisé au bord de trame alors qu'en vrai il suit le curseur
qui descend l'écran. `TODO.md` porte désormais la confirmation matérielle comme
tâche, à la place de l'item d'origine.

Deux tests : `terminal_timing_smoke` (5 sections, ne lie rien) pour le modèle, et
`pia_ddr_smoke` §9 pour le **câblage** — le test pur ne peut pas voir si `Memory`
consulte réellement le modèle, et cette section distingue les deux par
construction (après une trame de 17030 cycles, `FixedDelay` est encore occupé,
`FieldSync` ne l'est jamais).

**Cinq plafonds levés, et c'est dit ici plutôt qu'édité en silence dans le JSON.**
`memory_public_methods` 188 → 190, `controller_public_methods` 201 → 203, plus
les trois plafonds de lignes. La première version respectait le gel de `Memory`
en faisant passer le modèle par un paramètre de constructeur par défaut, comme
l'avait fait le seam audio ; cela coûte la bascule à chaud, et le mainteneur a
tranché qu'un knob de fidélité que personne ne peut basculer machine en marche
ne valait pas la pureté. Un plafond qui bouge sans raison inscrite dans le
fichier est un cliquet qui cesse de vouloir dire quelque chose.

### Changed — rendre le dépôt reprenable par quelqu'un d'autre

Mesuré plutôt que supposé : un clone neuf du dépôt, suivi de la procédure
documentée, **a échoué à la première étape**.

```
fetch-pack: unexpected disconnect while reading sideband packet
ERREUR : le clonage de Dear ImGui a échoué.
-- Configuring incomplete, errors occurred!
```

Coupure réseau passagère — la seconde tentative a réussi, et la construction
complète passe alors (126/126, zéro avertissement, 263 s). Mais le défaut est
réel : `tools/ensure_imgui.sh` faisait un `git clone` d'un seul coup, sans
reprise, et c'est le chemin d'acquisition de **dix** appelants —
`setup_pom1.sh`, `CMakeLists.txt`, les deux workflows CI, trois conteneurs
d'empaquetage et l'installeur Pi. Un hoquet réseau faisait échouer aussi bien le
premier `setup` d'un nouveau venu qu'une build de release, avec un message qui
ne suggérait même pas de réessayer. Trois tentatives avec attente croissante,
nettoyage du clone partiel entre chaque (sans quoi la reprise échouerait pour
une seconde raison, trompeuse), et un message final qui donne la commande à
relancer.

**Et le premier contact se faisait dans la mauvaise langue.** La documentation
est en anglais ; `ensure_imgui.sh` (32 lignes accentuées) et `setup_pom1.sh`
(14) parlaient français. C'est littéralement la première sortie qu'un
développeur étranger voit. Les deux scripts sont passés en anglais, avec une
ligne `LANGUAGE:` en tête qui dit pourquoi.

**Le README ne s'adressait jamais à un contributeur.** Sa section « Write your
own Apple 1 software » parle d'écrire du 6502 *pour* l'émulateur ; son unique
pointeur développeur était en fin de fichier, dans *Resources*, et renvoyait
vers `CLAUDE.md` — pas vers `ARCHITECTURE.md`, qui se présente pourtant comme
« the human entry point ». Nouvelle section **Hacking on POM1 itself** avec les
quatre commandes qui mènent du clone au test, et le pointeur corrigé.

Nouveau **`CONTRIBUTING.md`** : le savoir existait mais éclaté entre
`ARCHITECTURE.md` §5, la section *Testing* de `CLAUDE.md` et douze scripts de
garde. Il rassemble la boucle build/test, les deux lanes et leur règle de
défaut, le tableau des douze portes, la règle cardinale des plafonds (« jamais
en lever un pour faire passer la CI »), les règles de maison, et comment écrire
un test — y compris le piège `constexpr` trouvé cette semaine.

Enfin `concurrent_frontends_smoke` est déclaré **`REPEAT UNTIL_PASS:3`**. Il
mesure des durées de verrou en temps réel, et un runner partagé peut faire
sauter ses bornes sans que rien n'aille mal (une fois sur deux exécutions,
state-hold 279 ms contre 100 ms). La reprise est le correctif honnête plutôt que
de relâcher les bornes : ces bornes SONT la raison d'être du test. Trois échecs
indépendants échouent toujours. Ce n'est pas que de l'hygiène de CI — un nouveau
venu dont la première contribution passe au rouge sur un aléa conclut qu'il a
cassé quelque chose.

### Added — une palette de commandes (F9), dérivée des tables existantes

Dernière puce du chantier 3. La moitié « menus » était déjà faite et ne
l'annonçait pas : `MainWindow_Menu.cpp` construit tout le menu *Windows* depuis
`windowRegistry()`, groupé par `kind`, avec les panneaux de cartes non branchées
grisés. Restait la palette.

`src/CommandPalette.h` (en-tête pur, ni ImGui ni GLFW) porte la seule chose qui
soit une décision : ce qu'une requête filtre et dans quel ordre. La liste, elle,
est **dérivée** — reconstruite à chaque ouverture depuis `windowRegistry()`,
`pom1::shortcuts::kBindings` et `kMachinePresets[]` — donc une fenêtre, un
raccourci ou un profil nouveaux y apparaissent sans que personne ne les inscrive
quelque part. C'est la propriété qui compte : la palette ne peut pas diverger de
ce que l'application contient.

Détails qui évitent les doublons : les trois raccourcis qui basculent une fenêtre
du registre (F1/F2/F3) affichent leur accélérateur sur la **ligne de la fenêtre**
au lieu d'être listés une seconde fois comme commandes ; les fenêtres de type
`Dialog` sont exclues, comme dans le menu *Windows*, ce qui fait aussi que la
palette ne se liste pas elle-même.

Au passage, l'effet d'une commande n'est plus écrit qu'**une fois** : le `switch`
du répartiteur clavier devient `MainWindow_ImGui::runShortcutCommand()`, que la
palette appelle aussi. Sans cela la palette en aurait ajouté une seconde copie.

Nouveau test **`command_palette_smoke`** (6 sections, ne lie rien). **Il a trouvé
un vrai défaut du modèle** : `« D e b u g Console »` battait `« Debug Console »`
sur la requête `debug`, parce que chaque lettre isolée comptait comme un début de
mot. Un utilisateur qui tape un mot veut le mot : la prime de contiguïté passe
au-dessus de celle de début de mot.

La palette s'ouvre par **F9** ou depuis la barre de menus. Elle a sa ligne de
registre — `window_registry_sync` a refusé le drapeau `showCommandPalette` tant
qu'elle n'existait pas.

### Changed — les cinq tiers de CI passent en warnings-as-errors

Linux et macOS (Metal + OpenGL) portaient la porte depuis août. Windows et WASM
la portent désormais aussi, et **les deux étaient mal diagnostiqués**.

**Windows.** Ce qui a tenu le drapeau à l'écart pendant trois cycles rouges est
un C4244 levé *à l'intérieur* d'un en-tête MSVC — et la note qui accompagnait le
job **envoyait la recherche sur la mauvaise piste**. Elle annonçait
`std::fill<vector_iterator<uint8_t>, int>` dans `<xutility>`, et affirmait que
POM1 « ne contient aucun `fill(` », donc que le site exigeait la trace
d'instanciation et une machine Windows.

Mesuré sur un vrai job Windows : le diagnostic est **`<utility>(277)`, le
constructeur convertisseur de `std::pair`** — `pair<uint8_t,uint8_t>::pair<int,int>` —
et MSVC imprime `see reference to function template instantiation` en nommant
l'appelant deux lignes plus bas. Deux sites, trouvés par un `grep pair<uint8_t` :
le `pokeSidRegisters({{0, 0x34}, …})` de `concurrent_frontends_smoke_test.cpp` et
le `{REG_V1_CR, 0x00}` de `silenceRegisters()`. Les deux écrivent désormais
`uint8_t{…}`. **Le premier est une source de TEST** : `/W4` et `/WX` s'appliquent
aussi aux binaires de test, ce qui est voulu — ils compilent les mêmes sources de
périphériques que l'application.

La leçon porte sur la note plus que sur l'avertissement : un diagnostic imputé à
un en-tête standard nomme quand même son appelant, et une affirmation du type
« ceci ne peut pas se greper » mérite d'être revérifiée avant de coûter un
quatrième cycle. (Six `std::fill` sur `std::vector<uint8_t>` ont reçu un
`static_cast<uint8_t>` explicite pendant la poursuite de l'ancienne note ; MSVC
ne les a jamais signalés, mais les casts s'alignent sur les cinq `std::fill_n`
voisins et sont conservés.)

**WASM.** Cinq fonctions que seuls les chemins NATIFS appellent — le lecteur de
sidecar `.size`, le constructeur de filtres du sélecteur natif et trois
auxiliaires du bundle cc65 — étaient définies sans condition alors que leurs
seuls appelants vivaient dans un `#if !POM1_IS_WASM`. Gardes posées sur les
définitions. (Le décompte annoncé était « deux statiques inutilisées » ; il y en
avait cinq.)

Coût mesurable : les 11 lignes de gardes font monter `mainwindow_lines` de 16780
à 16791. C'est le seul type de hausse que ce plafond accepte — une qui nomme ce
qu'elle achète.

### Added — les dix croquis LOGO livrés deviennent atteignables (et vérifiés)

`sketchs/logo/` embarque dix programmes tortue **APPLE-1 LOGO V2.6** depuis
juillet 2026, et les deux cibles interpréteur du Bench existent depuis autant de
temps. Il manquait le lien : rien dans l'interface ne les nommait. Ils
apparaissent désormais dans le popup **Examples** sous le groupe *LOGO turtle*,
rangés pour que chacun s'appuie sur le précédent — une forme, puis une procédure
paramétrée, puis la récursion, puis `RANDOM`.

Ils ouvrent sur la cible **TMS9918** et pas au choix, pour une raison de fond :
`injectLogo` choisit la disposition RAM de l'interpréteur (`proc_table`,
`n_procs`, entrée à froid) d'après **l'index de cible**, pas d'après la machine
vivante. Une ligne qui laisserait les deux en désaccord poserait des adresses TMS
dans une machine GEN2. Les listings restant machine-neutres, basculer le Mode sur
*LOGO GEN2 HGR* les exécute sans toucher au source.

**Deux tests, parce que deux choses pourrissent séparément.** Nouveau
`logo_sketches_smoke` (6 sections, ne lie que `LogoProgramLoader`) : chaque
croquis se compile pour **les deux** interpréteurs, chaque écriture atterrit dans
la table de procédures ou sur `n_procs`, le chargeur est pur — et le Bench offre
**exactement** le catalogue livré, ni un `.logo` oublié ni un lien mort (la table
est lue comme du TEXTE, elle vit dans une unité UI qu'aucun binaire de test ne
lie). Il refuse aussi l'orthographe `RT`/`LT`, que ce dialecte n'a pas.

Et `bench_logo_inject_smoke` gagne un troisième bloc qui **exécute les dix sur
l'interpréteur GEN2 réel** et vérifie que la tortue a dessiné (79 à 303 octets
allumés dans le framebuffer). L'écart entre les deux tests est le point : le
chargeur stocke les corps de procédure en source brut et n'inspecte aucun nom de
commande, donc un écart de dialecte — un identifiant de plus de six caractères,
deux opérations arithmétiques dans un argument — se compile parfaitement et
échoue seulement sur la machine.

### Changed — la couverture de l'UI mesurée : 2,4 % → 3,9 %, et une leçon sur `constexpr`

`TODO.md` donnait « `ui` à 2,4 % de couverture de lignes sur 14 407 » comme
chiffre de départ du chantier 3. Après les quatre extractions, mesure sur une
suite verte : **3,9 % sur 14 468 lignes** (558 couvertes contre ~346). Les neuf
autres modules sont inchangés au dixième près — le travail n'a donc pas été
déplacé ailleurs, ce qui est la première chose à vérifier.

Les quatre nouveaux en-têtes ont d'abord été **déclarés dans le module `ui`** de
`tools/coverage.py` : sans cela ils tombaient dans le fourre-tout `devices`.
C'est délibéré et c'est le sens du chiffre — la ligne dit quelle part de la
**logique** de la couche UI est testée, et reclasser ces lignes ailleurs
améliorerait la mesure en déplaçant le travail. Précédent déjà en place :
`Apple1KeyMap`, `WindowGeometry` et `FullscreenExpand` y étaient.

**Un seam `constexpr` a besoin d'appels à l'exécution en plus des
`static_assert`.** Le détail par fichier a montré six seams à 100 % et
`ShortcutTable.h` à **30 %** — alors que chacune de ses propriétés était en fait
*prouvée*, et plus solidement qu'un `assert` d'exécution ne le peut : un
`static_assert` ne peut pas être sauté. L'instrumentation ne voit simplement pas
une évaluation à la compilation. `shortcut_table_smoke` affirme désormais chaque
propriété **deux fois** — une fois pliée, une fois à travers un blanchiment
`volatile` (et un pointeur de fonction pour la surcharge sans argument) — ce qui
porte le fichier à **100 % de lignes, de fonctions et de régions**. Les sept
seams sont maintenant à 100 %.

### Added — « quel répertoire implique quelle carte » : une table au lieu de deux

Chantier 3 de `TODO.md`. Charger un programme depuis `software/Graphic HGR/`
branche la carte GEN2 et ouvre son framebuffer ; depuis `software/NET/`, cela
branche le modem Wi-Fi — ou le **réinitialise** si une session BBS est déjà
ouverte. Cette correspondance était une chaîne de sept `else if` faits de
`path.find("/X/") || path.find("\\X\\")` dans `performMemoryLoad()`, et le
sélecteur de fichiers portait **la même relation à l'envers**, sous un
commentaire qui le disait à voix haute : *« This is the reverse of the
auto-enable-by-source-dir mapping in performMemoryLoad() »*. Deux tables, une
seule relation.

`src/SoftwareDirRules.h` (en-tête pur, `constexpr`) porte les deux directions.
Ce que cela achète, ce sont des règles fausses de façon **invisible** :

- **la correspondance porte sur un COMPOSANT de chemin**, sous les deux
  conventions de séparateur *et* sous un mélange des deux (ce que produit un
  chemin relatif en `/` joint à une base Windows). Une recherche de
  sous-chaîne nue se déclencherait sur `software/NETWORKING/` ; oublier la forme
  antislash rend chaque règle silencieusement morte sous Windows ;
- **la première règle gagne** sur un chemin portant deux marqueurs. C'était
  implicite dans l'ordre des `else if` ; c'est désormais une propriété testée ;
- **toutes les règles ne sont pas un défaut de sélecteur.** Le contenu de la
  microSD vit sur le système de fichiers de la carte, pas sous `software/` :
  elle se branche au chargement mais ne doit jamais être le dossier que le
  sélecteur ouvre. C'était un commentaire ; c'est un champ.

Les conséquences restent portées par la règle qui les veut : l'éviction des
cartes de stockage avant chargement (les deux cartes graphiques — sinon les
fenêtres `$6000-$AFFF` de microSD/CFFA1 masquent le programme, ce qui se voit
comme une carte noire et non comme une erreur) et la réinitialisation au
rechargement (le seul modem). *Ce que* réinitialiser veut dire reste côté UI,
donc une future règle qui lèverait le drapeau ne peut pas couper la session BBS
de quelqu'un par inadvertance.

Au passage, les trois branches qui appelaient `emulation->setCardEnabled()`
passent comme les autres par `setCardPlugged()`, qui relit l'instantané : le
reste de la trame voit les cascades déclenchées plutôt que la copie d'avant.

Nouveau test **`software_dir_rules_smoke`** (6 sections, lane `emulator`, ne lie
rien). `bbs_autodial` — qui charge réellement `software/NET/bbs.*.txt` et
vérifie que le modem compose — reste vert de bout en bout.

### Added — la table de raccourcis cesse d'être récitée trois fois

Chantier 3 de `TODO.md`, troisième puce (moitié raccourcis). La table de
liaisons se présentait comme « la source unique de vérité pour les libellés de
menu **et** le répartiteur de touches » — et l'était, pour **trois** de ses huit
lignes. Les cinq autres portaient une action nulle et étaient réparties par une
échelle `else if (key == GLFW_KEY_F1) … else if (key == GLFW_KEY_F2) …` en
dessous, soit une deuxième copie indexée sur les mêmes valeurs. La fenêtre
*Help ▸ Keyboard Shortcuts* en tenait une **troisième**, écrite à la main, sous
un commentaire demandant de « mettre les deux à jour ensemble » : la liste que
l'utilisateur consulte pour apprendre les touches était la copie la plus
susceptible d'être périmée.

`src/ShortcutTable.h` (en-tête pur, `constexpr`, ni ImGui ni GLFW) porte les
huit lignes, chacune nommant une **commande** au lieu d'un pointeur de membre
autorisé à être nul, plus sa prose d'aide et sa politique de répétition
(seul le pas-à-pas maintenu la veut ; un reset matériel répété est un démarrage
qui n'aboutit jamais). Le répartiteur devient un `switch` sur l'énumération, et
la fenêtre d'aide **rend les lignes qu'elle décrit**.

**L'invariant devient du code.** « Jamais de Ctrl+lettre » — le répartiteur
passe avant l'Apple-1, donc une telle liaison masque le code de contrôle ASCII
de la même lettre et le rend intypable sur la machine émulée (c'est ainsi que
Ctrl+O/S/V/Q avaient mangé `$0F`, `$13` XOFF, `$16` et `$11` XON). La règle est
désormais un `static_assert` sur la vraie table dans `MainWindow_Keyboard.cpp` :
ajouter une telle ligne **casse la compilation**. `shortcuts_sync` est conservé
et repointé sur le nouvel en-tête — il lit le TEXTE, donc il attrape encore les
formes que le prédicat C++ accepterait (une touche écrite en nombre brut) et il
nomme le code de contrôle masqué, qui est la partie qui explique le bug.

Nouveau test **`shortcut_table_smoke`** (6 sections, lane `emulator`, ne lie
rien du tout) : chaque ligne est utilisable, la recherche discrimine bien touche
*et* modificateurs (F5 et Ctrl+F5 sont un reset doux contre un cycle
d'alimentation qui efface la RAM), la répétition appartient à la ligne, aucun
accord n'est revendiqué deux fois, chaque commande est atteignable exactement
une fois — et l'invariant Ctrl+lettre est vérifié **avec un cas témoin** prouvant
que le contrôle se déclenche, comme `lock_order_smoke` le fait pour l'ordre des
verrous.

### Added — les décisions de presets sortent de l'UI, et trois règles cessent d'être écrites deux fois

Chantier 3 de `TODO.md`, deuxième puce. `MachinePresets.h` portait déjà les
DONNÉES et `CardTopology.h` la POLITIQUE des cartes ; ce qui restait dans
`applyMachineConfig()` était la composition — et **trois de ses règles étaient
énoncées deux fois**, dans deux fichiers, sous un commentaire demandant au
lecteur de maintenir les copies synchronisées. Une règle énoncée deux fois est
une règle qui peut se contredire.

`src/PresetDecisions.h` (en-tête pur : ni ImGui, ni GLFW, ni `MainWindow`) les
énonce une fois :

- **le paquet de fidélité silicium** — douze drapeaux, armés par douze lignes
  `= !fantasyPreset;` sur le chemin des presets et douze autres sur le bouton
  maître Silicon Strict. `siliconFidelity()` compose la structure ; le seul
  endroit qui l'applique est le nouveau `MainWindow_ImGui::applySiliconFidelity()`,
  et les deux chemins ne peuvent plus diverger ;
- **« où vit Applesoft Lite ? »** — `applesoftOnSdCard()`, un unique prédicat
  qui décide *à la fois* le profil ROM que le contrôleur charge et l'inventaire
  que la Memory Map affiche. C'étaient deux copies : la carte mémoire pouvait
  décrire une machine autre que celle qui tourne. Le chemin **headless** en
  portait une **troisième** ;
- **« est-ce un profil DevBench ? »** — écrit comme une plage d'indices
  (`presetIndex <= 2`) à un endroit et comme trois comparaisons de `PresetId` à
  l'autre.

Plus le choix de cassette (`tapeFor` — quelle bande, et laquelle est de la
*donnée* que l'ACI lit plutôt que de la musique) et les prédicats restants
(`animatesBoot`, `showsBanner`, `coldResetOnApply`).

**Les fenêtres passent par le registre.** `applyMachineConfig` récitait le jeu
de fenêtres deux fois de plus : 47 lignes de `showXxx = false;` puis une chaîne
de 36 `else if (n == "<titre>")`. Les deux sont remplacées par une boucle sur
`windowRegistry()`, qui gagne une colonne `resetOnPresetSwitch` (défaut *vrai* :
une fenêtre appartient à un preset sauf si l'utilisateur y travaille — Bench,
éditeurs, inspecteurs, Memory Viewer, Debugger). C'était bien un jeu qui
divergeait : **le tutoriel IEC manquait à la liste de fermeture**, seul de seize
tutoriels à rester ouvert d'un profil à l'autre. Corrigé par construction.
Vérifié sur une machine réelle : le profil 12 ouvre exactement `CassetteDeck` et
`Welcome`, les deux panneaux que sa table déclare.

Nouveau test **`preset_decisions_smoke`** (8 sections, lane `emulator`) : il lie
la **vraie** table de presets, donc chaque assertion porte sur les treize
machines livrées et non sur des fixtures — le paquet silicium est tout-ou-rien
et seuls les deux profils *Fantasy* le désarment, les deux compositions
coïncident, l'inventaire ROM s'accorde avec le profil ROM branche par branche,
une seule cassette n'est pas de la donnée, et les trois profils DevBench sont
bien les indices 0-2.

`mainwindow_lines` descend de 16950 à **16840** et le plafond suit ;
`sources_outside_test_devices` reste à 86 — les deux en-têtes sont *header-only*.

### Added — les décisions de layout et de plein écran sortent de l'UI

Chantier 3 de `TODO.md`, première puce. `src/LayoutDecisions.h` — en-tête pur,
sans ImGui, sans GLFW, sans `MainWindow` — reprend l'**arbitrage** que
`FullscreenExpand` (le *timing*) et `WindowGeometry` (le *format*) avaient laissé
derrière eux, et c'est là que vivaient les six défauts plein écran d'août 2026,
dont aucun n'était épinglable sur place :

- **quel rectangle est persisté** (`decidePersistedGeometry`) — toujours le
  rectangle *fenêtré*, jamais le cadre plein écran ; `GLFW_MAXIMIZED` n'est pas
  persisté depuis un espace natif macOS (AppKit y rapporte l'état zoomé de la
  fenêtre sous-jacente) ; un `--fullscreen` de borne n'a pas le droit de
  réécrire le profil en plein écran — le lancement suivant démarrerait plein
  écran sans raison visible — mais ne peut pas pour autant supprimer le mode 2,
  un espace natif étant toujours un geste de l'utilisateur ;
- **quelle géométrie s'applique** (`planLayoutRestore`, cinq branches) — le
  plein écran est une propriété de **session**, jamais de profil, et un espace
  natif macOS n'est jamais ré-entré par programme ;
- **ce que fait une réinitialisation** (`planLayoutReset`) — y compris le retrait
  de l'entrée « Apple 1 Screen » de `pendingLayout`, sans lequel le forçage en
  `ImGuiCond_Always` réécrit le rectangle fenêtré par-dessus l'expansion ;
- **comment une géométrie en attente s'applique** (`placementApply`) — forcée et
  conservée, ou `FirstUseEver` et consommée ;
- **la case Plein écran face à AppKit** (`fullscreenCheckboxState`,
  `planFullscreenToggle`) — le masque de style reste posé pendant toute
  l'animation de sortie (~0,5 s), donc l'état brut ferait re-cocher la case et
  inviterait un second clic qui remet la fenêtre dans l'espace ;
- **la taille de fenêtre qu'un preset demande** (`computeOsWindowSize`) — une
  seule fonction pour les **trois** copies de l'arithmétique (bureau, canvas
  WASM, pré-génération des `.size`), avec le plancher POM1 Fantasy sur le bureau
  et son absence délibérée dans le navigateur.

Nouveau test **`layout_decisions_smoke`** (8 sections, lane `emulator`) : il ne
lie que `WindowGeometry.cpp`, pour faire l'aller-retour réel
`decidePersistedGeometry` → sidecar → `planLayoutRestore` (§4). Aucun
gestionnaire de fenêtres n'est nécessaire pour que ces règles soient vraies.

L'en-tête étant *header-only*, `sources_outside_test_devices` reste à 86 ;
`mainwindow_lines` descend de 16955 à 16950 et le plafond suit. Le gain n'est
pas le nombre de lignes — c'est que ces décisions sont désormais assertables.


### Changed — le code POM1 est propre sous MSVC `/W4` (la porte, pas encore)

Le job Windows compilait en `/W4` **avertissements visibles mais non fatals**,
parce que le jeu d'avertissements de MSVC n'avait jamais été mesuré sur cet
arbre. Il l'est : **134 sites distincts**, dont **102 dans `stb_vorbis.c`** —
vendu, et déjà silencé côté GCC et clang par un bloc de pragmas, jamais côté
MSVC. Le `.c` est inclus dans `AudioDevice.cpp`, donc ses avertissements étaient
imputés à *cette* unité de traduction : un `warning(push, 0)` autour de
l'inclusion, exactement comme les deux autres compilateurs, et les 102
disparaissent. Même traitement pour `miniaudio.h`, y compris là où
`CassetteDevice.h` le tire dans tout ce qui l'inclut.

Restent **30 sites dans le code POM1**, tous corrigés plutôt que désactivés :

- **C4244**, conversions rétrécissantes — rendues explicites par `static_cast`
  (`M6502`, `TMS9918`, `HgrConvert`, `SidTrackerEditor`, six tests). Dans
  `CassetteDevice`, `std::llround` devient `std::round` : la valeur était de
  toute façon ramenée dans le domaine `double` juste après, et `llround` était à
  la fois un rétrécissement et un comportement indéfini sur les valeurs
  hors-bornes que cette arithmétique existe précisément pour survivre.
- **C4456/C4457**, déclarations masquantes — quatre renommages.
- **C4996**, `inet_ntoa` — remplacé par `inet_ntop` dans `TerminalCard` et
  `TelemetryPort`. Déprécié sur Windows, et il rend de toute façon un pointeur
  vers un tampon statique partagé sur toutes les plateformes.

**Un de ces masquages était un vrai défaut.** Dans `hgr_convert_smoke_test`, le
bloc « les bandes de letterbox restent noires » déclarait un `img` local de
100×192 puis passait au convertisseur… l'`img` extérieur de 280×192. Il testait
donc le letterbox sur une source qui n'en a pas. Corrigé, le test passe toujours
— l'assertion tient sur l'entrée qu'elle prétendait utiliser. C'est ce
qu'achète une porte d'avertissements, et ce qu'aucune relecture n'avait vu.

Le nom de remplacement n'est pas `small` non plus : `<windows.h>` (rpcndr.h) en
fait une macro pour `char`.

Le premier run MSVC a rendu **deux sites de plus**, tous deux vendus, et tous
deux instructifs :

- `libresidfp/WaveformGenerator.h` masque un membre de classe (C4458). Il entre
  par `SID.cpp`, qui n'avait pas le bracket — je l'avais mis autour de
  `stb_vorbis` et de `miniaudio` seulement. Corrigé.
- `stb_vorbis.c(4758)` déclenche C4701, « variable locale potentiellement non
  initialisée », **qu'aucun pragma ne peut couvrir**. Les avertissements de la
  plage 4700+ sont décidés pendant la **génération de code**, après l'analyse de
  l'unité de traduction entière : aucun état écrit dans le source n'atteint ce
  moment-là. Le `warning(push, 0)` autour de l'inclusion a échoué, puis un
  `disable` placé après le `pop` a échoué aussi — deux cycles de CI pour
  l'apprendre. La suppression est donc un **drapeau de compilation**,
  `/wd4701` sur ce seul fichier source, qui n'a pas d'état à perdre. C'est la
  seule concession du lot, sur une unité de traduction dont le code POM1 se
  réduit à de la colle miniaudio.

  Au passage, le commentaire de `tests/CMakeLists.txt` — « nothing vendored is
  compiled in this directory » — était faux : `AudioDevice.cpp` inclut
  `stb_vorbis.c` textuellement, et `SID.cpp` les en-têtes libresidfp. C'est
  précisément pourquoi les avertissements du vendu étaient imputés à
  `pom1_test_devices`.

**La porte n'est pas activée.** Trois runs ont buté sur une classe que la liste
initiale ne prédisait pas : un C4244 levé **à l'intérieur du `<xutility>` de
MSVC**, depuis une instanciation `std::fill<vector_iterator<uint8_t>, int>`,
imputée à `pom1_test_devices`. Or les sources POM1 ne contiennent **aucun**
`fill(` : le site d'appel se trouve par la trace d'instanciation, pas par grep,
et ça demande une machine Windows ou au moins un run qui imprime la chaîne de
notes complète. Basculer le drapeau à l'aveugle a coûté trois cycles de CI
rouges ; il repassera quand cette classe sera mesurée, pas avant. `/W4` reste
appliqué, donc le compte continue d'atterrir dans le journal.

Ce qui est acquis et permanent, en revanche : les 30 corrections, les brackets
autour du code vendu, le `/wd4701`, et le défaut de test qu'elles ont révélé.

Vérifié d'ici : suite complète verte (120/120) et build `-DPOM1_WERROR=ON` sous
clang sans une seule erreur.

**Au passage, le build WASM mesuré aussi** (`emcc` local, `-DPOM1_WERROR=ON`) :
trois défauts, dont un corrigé ici — `rewindBlobGeneration` était une variable
inutilisée sous WASM, le rewind y étant désactivé ; ses deux déclarations
passent sous le même `#if !POM1_IS_WASM` que les blocs qui les utilisent.
Restent deux fonctions statiques inutilisées (`hexDumpFilter`,
`loadSizeFile`) que le WASM ne compile pas ; le job WASM n'est donc pas barré
non plus.


### Fixed — plus une seule URL mouvante dans la chaîne AppImage

`build-bionic-image.yml` échouait depuis le **22 août**, indépendamment de la CI.
Le Dockerfile téléchargeait `linuxdeploy` et `appimagetool` depuis le tag
**`continuous`** — mouvant — puis vérifiait un SHA-256 épinglé. C'est une
contradiction : l'URL bouge, la somme ne peut pas, donc le seul aboutissement
possible est un échec dur **sans version à incrémenter**, seulement une chasse à
un nouveau hash qui se périmera à son tour.

Le coupable, identifié en téléchargeant les quatre artefacts et en les hachant :
`linuxdeploy` seul. L'amont a reconstruit son asset `continuous` le
**1ᵉʳ août** — `e87ee081…` → `36a2d7e2…` — et l'image a cessé de se construire
au premier run suivant. La somme d'`appimagetool` (`b90f4a8b…`), elle,
correspond toujours.

`linuxdeploy` passe donc sur un **vrai tag de release**,
`1-alpha-20251107-1` (le plus récent non-prerelease), sha
`c20cd71e…` — URL et somme vérifiées verbatim. Son asset ne peut plus être
réécrit sous ce tag.

`appimagetool` suit, sur **AppImageKit tag 13**, asset
`obsolete-appimagetool-x86_64.AppImage` — le préfixe fait partie de l'URL,
l'amont ayant renommé ses assets après publication. Sa somme `continuous`
n'avait pas *encore* dérivé, mais le dépôt n'est pas archivé et l'asset a été
retouché le 2025-07-26 : même mèche, pas encore allumée.

Basculer l'outil change le runtime embarqué dans ce que les utilisateurs
téléchargent, et deux faits rendent ça vérifiable plutôt que pariable : les
appimagetools du tag 13 en x86_64 **et** aarch64, ainsi que les runtimes qu'ils
embarquent, sont tous **ELF ET_EXEC** (mesuré — c'est la propriété
qu'AppImageLauncher exige et la raison même pour laquelle ce dépôt utilise le
vieil outil d'AppImageKit) ; et `release.yml` **barre déjà** l'AppImage produite
sur `readelf -h` = EXEC plus la magie `AI\x02`, sur les deux architectures. Une
régression échouerait bruyamment au lieu d'être publiée.

**`packaging/linux/build_appimage.sh` portait la même dérive, sans même une
somme de contrôle.** Ce chemin n'est pris que hors image bionic — build local,
sandbox — mais il téléchargeait les deux outils depuis `continuous`. Les deux
URL passent sur les mêmes tags immuables. Le runtime aarch64 reste épinglé sur
la release 12 : elle est déjà immuable, et on ne touche pas un chemin de release
qui marche pour l'élégance — noté au passage que `13/obsolete-runtime-aarch64`
est ET_EXEC lui aussi, si quelqu'un veut simplifier un jour.

Les cinq URL épinglées répondent 200, et les sommes ont été vérifiées verbatim
avant commit.


### Fixed — la CI était rouge depuis cinq jours, sur quatre causes distinctes

**4. Linux — un seuil de concurrence qui mesurait l'ordonnanceur, pas POM1.**
Une fois le build Linux réparé, `concurrent_frontends_smoke` est apparu : il
n'avait **jamais tourné** sur ce job, ayant été ajouté par le commit même qui
cassait la compilation. Il échouait sur `maxStateWaitNs` = 1,66 s contre une
porte à 500 ms — alors que le `maxStateHoldNs` restait à 16 ms. Personne n'avait
donc tenu le verrou trop longtemps : un attendeur avait simplement perdu
l'ordonnanceur.

Deux mesures ont tranché. D'abord, 500 ms était intenable : sur un Mac 8 cœurs
au repos, la même valeur va déjà de 57 à 350 ms. Ensuite — et c'est le point —
**cette porte ne détecte pas ce pour quoi elle existe** : en désactivant le
yield de priorité de `PriorityMutex` dans `emulationLoop` et en relançant huit
fois, elle n'a franchi les 500 ms **qu'une fois sur huit**. Faible pouvoir de
détection, fort taux de faux positifs.

Ce que le yield achète réellement se voit sur la **progression** du fil
concurrent : bascules de topologie terminées dans la même fenêtre, médiane ~117
avec le yield contre ~35 sans, sur huit runs chacun. Les distributions se
recouvrent sur un run isolé (116 contre 45), donc ce n'est pas encore un seuil ;
c'est la mesure sur laquelle une future porte devra se construire.

`maxStateWaitNs` devient donc une porte de **catastrophe** (5 s = un blocage) et
non de qualité. Restent barrées les grandeurs que POM1 contrôle vraiment, toutes
deux avec de la marge : le HOLD maximal mesuré 18-68 ms contre 100, et le
callback audio 7-33 µs contre 50 ms.

### Fixed — les trois premières causes

`ci.yml` échouait sur **chaque** push depuis `c72d4eae` (27 août) : les jobs
`linux` et `windows` rouges, `macos` et `wasm` verts. Cinq jours pendant
lesquels tous les garde-fous du dépôt tournaient dans un pipeline que personne
ne pouvait lire — exactement ce que `CLAUDE.md` dit déjà des suppressions de
sanitizers : « permanently red and therefore unread ».

**1. Linux — `requires` est un mot-clé C++20.** `src/CardTypes.h` déclarait
`CardSet requires;`. GCC le signale sous `-Wc++20-compat`, que son `-Wall`
inclut, et le job Linux est le seul à monter `-DPOM1_WERROR=ON` : erreur.
clang ne le dit pas, donc macOS restait vert et rien ne le reproduisait en
local. Le membre s'appelle `dependencies` — le mot que `CLAUDE.md` employait
déjà pour cette notion. Vérifié en recompilant l'arbre entier avec
`-Werror=c++20-compat` : plus aucune occurrence.

**2. Windows — deux débordements de pile, pas deux mystères.** MSVC réserve
1 Mo de pile par fil ; Linux et macOS en donnent 8. Les deux tests mouraient
sans **aucune** sortie — le CRT jette un `stdout` tamponné quand le processus
meurt, donc `--output-on-failure` ne montrait rien et le symptôme se lisait
comme un segfault inexplicable. Mesuré avec `clang -Wframe-larger-than=32768`,
qui est la façon de diagnostiquer ça depuis une machine qui n'est pas celle qui
plante :

| test | trame de `main` | |
|---|---:|---|
| `aci_tape_saving` | 2 130 928 o (2,03 Mo) | plantait |
| `measured_cpu_rate_smoke` | 801 344 o (783 Ko) | plantait |
| `concurrent_frontends_smoke` | 267 824 o (262 Ko) | passait |

Ce n'est pas une coïncidence : `sizeof(EmulationSnapshot)` vaut **260 Ko** et
`sizeof(EmulationController)` **261 Ko**, donc un test qui tient un contrôleur
et trois instantanés par valeur dépasse la ligne du mégaoctet par simple
arithmétique. `add_link_options(/STACK:8388608)` sous MSVC aligne Windows sur
les deux autres. Relever la réserve plutôt que réécrire les tests est
délibéré : l'asymétrie est celle de la plateforme, le même code tourne ailleurs
sans problème, un drapeau d'édition de liens ne peut rien régresser, et ça
couvre aussi le `POM1.exe` livré, dont la boucle de rendu copie le même
instantané de 260 Ko. C'est une RÉSERVE — Windows engage les pages à la
demande, donc le coût est en espace d'adressage, pas en mémoire.

**3. Windows — `resource_probes_sync`, régression à moi.**
`os.path.relpath` rend `src\MicroSD.cpp` sur Windows, donc une clé de liste
blanche écrite `src/MicroSD.cpp` ne correspondait à rien et les cinq sites
légitimes étaient signalés comme des infractions. Vert sur Linux et macOS,
rouge sur Windows seulement. Les séparateurs sont normalisés ; `coverage.py`
avait la même faiblesse latente et est corrigé au passage.


### Added — couverture par module, et quatre seuils qui tiennent

`-DPOM1_COVERAGE=ON` instrumente l'arbre (LLVM source-based coverage, LTO
forcé off) et **`tools/coverage.py`** conduit le cycle entier : configure,
build, `ctest` sous instrumentation, `llvm-profdata merge`, `llvm-cov export`,
puis un tableau **lignes + branches par module**.

Le refus explicite d'un pourcentage global est le cœur de l'affaire : une
moyenne sur 44 500 lignes mesurables mélange un 6502 cycle-exact et 14 400
lignes de dessin ImGui qu'aucun binaire de test ne lie. Elle bouge pour les
mauvaises raisons et s'améliore en testant ce qui est facile. Première mesure,
suite complète (120 tests, verts sous instrumentation) :

| module | fichiers | lignes | ligne % | branche % |
|---|---:|---:|---:|---:|
| cpu | 1 | 723/775 | **93,3** | 90,7 |
| parsers | 9 | 1973/2108 | **93,6** | 81,1 |
| topology | 5 | 392/436 | **89,9** | 84,6 |
| snapshot | 5 | 1020/1147 | **88,9** | 74,9 |
| memory | 3 | 1193/1502 | 79,4 | 71,8 |
| platform | 10 | 837/1378 | 60,7 | 53,6 |
| devices | 64 | 4542/8663 | 52,4 | 41,2 |
| control | 7 | 546/1596 | 34,2 | 34,9 |
| devtools | 57 | 3045/12529 | 24,3 | 20,0 |
| **ui** | 31 | **341/14407** | **2,4** | 3,5 |

Le tableau confirme la thèse de `TODO.md` avec un chiffre : **14 407 lignes
d'UI à 2,4 %**, et c'est là que vivaient les défauts connus (backspace
destructif, six défauts du plein écran, auto-dial BBS). Le chantier 3 a
maintenant sa mesure de départ.

**Quatre seuils seulement** — cpu 90, parsers 90, topology 85, snapshot 85 —
posés *sous* ce que la suite mesure déjà : ce sont des cliquets destinés à
repérer un module qui **perd** de la couverture (une branche ajoutée sans son
cas), pas des objectifs. Les six autres modules sont **rapportés et non
barrés** : un plancher sous 2,4 % d'UI serait un nombre qui fait semblant
d'être une promesse.

Nouveau job CI **`coverage`** (nocturne, avec les sanitizers et les fuzzers —
le build instrumenté est en `-O0` et la suite tourne plusieurs fois plus
lentement) : `tools/coverage.py --gate --json coverage.json`, le JSON étant
archivé pour suivre la tendance. `llvm-cov` et `llvm-profdata` sont résolus
**dans la version du clang qui a compilé l'arbre** (un décalage ne dit rien
d'autre que « unsupported coverage format version »), via `xcrun` sur macOS et
`/usr/lib/llvm-N/bin` sur Debian/Ubuntu.

Vérifié dans les deux sens : `--gate` passe sur l'arbre actuel, et un seuil
volontairement inatteignable (`--min cpu:99`) échoue en nommant le module. Le
build normal est inchangé — l'option est OFF par défaut, `build-coverage/` est
un arbre séparé (sinon l'instrumentation dés-optimiserait le `build/` de tout
le monde) et `ctest` reste à **120/120**.


### Security — les GitHub Actions sont épinglées au commit

`uses: actions/checkout@v5` ressemble à une version ; ce n'en est pas une. `v5`
est un pointeur **mutable** dans le dépôt de quelqu'un d'autre, que son
propriétaire peut déplacer à tout moment — et ces workflows portent les secrets
du dépôt : `release.yml` construit, signe et publie les binaires que les
utilisateurs téléchargent, `pages.yml` déploie le site. Une étiquette déplacée,
c'est du code arbitraire dans le job qui livre le produit.

Les **27 références** des cinq workflows nomment désormais un SHA de 40 hex,
avec la version d'origine en commentaire — la seule référence qui veuille dire
« exactement ce code-là ». Huit actions distinctes, résolues via l'API GitHub et
revérifiées commit par commit :

| action | commit | version |
|---|---|---|
| `actions/checkout` | `fbc6f39…` | v5.1.0 |
| `actions/cache` | `0057852…` | v4.3.0 |
| `actions/upload-artifact` | `ea165f8…` / `330a01c…` | v4.6.2 / v5.0.0 |
| `actions/download-artifact` | `634f93c…` | v5.0.0 |
| `actions/upload-pages-artifact` | `56afc60…` | v3.0.1 |
| `actions/deploy-pages` | `d6db901…` | v4.0.5 |
| `mymindstorm/setup-emsdk` | `6ab9eb1…` | v14 |

`upload-artifact` reste volontairement en v4 sur `ci.yml`/`pages.yml` et en v5
sur `release.yml`/`pi-borne.yml` : épingler n'est pas mettre à jour, et v5
change de comportement. C'est une décision séparée.

Le coût d'un épinglage, c'est que plus rien ne bouge tout seul — et une action
gelée est un problème de sécurité à son tour. D'où **`.github/dependabot.yml`**
(écosystème `github-actions`, mensuel, groupé en une seule PR) : Dependabot
réécrit le SHA **et** son commentaire ensemble, donc l'épingle reste lisible et
la relecture voit vers quelle version elle va.

**Nouveau garde-fou `action_pins_sync`** (`tools/check_action_pins.py`,
cinquième de la famille) : tout `uses:` qui n'est pas un SHA suivi de sa version
en commentaire échoue. **Hors ligne par construction** — il ne demande jamais à
GitHub si le SHA existe, un test qui a besoin du réseau est un test qui rougit
dans le train ; il tient la *forme*, la justesse du SHA étant l'affaire de
Dependabot et du relecteur. Il refuse aussi un SHA commenté avec deux versions
différentes selon le fichier (une montée de version à moitié faite). Vérifié
dans les deux sens : étiquette flottante réintroduite → rouge ; commentaire de
version retiré → rouge.

Les cinq workflows re-parsent proprement en YAML (27 `uses:` relus, tous des
SHA — donc le `# vX.Y.Z` est bien un commentaire et non une partie de la
valeur). Suite complète : **120 tests verts**.


### Changed — plus une seule sonde `../` écrite à la main

`pom1::ResourceLocator` portait l'ordre de recherche unique depuis sa création,
mais seul `Memory` le recevait : **65 littéraux `"../`** subsistaient dans
`src/`, dont **52 vraies sondes** réparties sur 20 fichiers — l'UI, les
dialogues, les presets, la DevBench, les hôtes des éditeurs, `GraphicsCard` et
`Screen_ImGui`. Chacune remontait le nombre de niveaux que son auteur avait jugé
suffisant (deux ici, trois là, quatre dans `findIniDefaultsFile`), et c'est
exactement la dérive que la classe existait pour arrêter.

Toutes passent désormais par `defaultLocator().find()` /
`.findDirectory()`. Trois choses tombent avec elles :

- **La moitié « à côté de l'exécutable » n'existait que sous `#if
  defined(_WIN32)`.** `find_app_icon_path`, `find_font_path`,
  `find_photo_jpeg_path` et `find_about_photo_jpeg_path` recopiaient chacune un
  bloc `GetModuleFileNameA` — donc sous Linux et macOS, un binaire lancé
  ailleurs que dans l'arbre n'affichait ni icône, ni police Font Awesome, ni
  photo, en silence. Le locator connaît ces racines sur les trois plateformes,
  plus les dispositions empaquetées (`.app/Resources/`, AppImage
  `share/POM1/`) qu'aucune liste écrite à la main n'avait.
- **`findIniDefaultsFile` réécrivait le locator en entier** — quatre préfixes
  cwd puis `executableDirectory()` + `Resources/` + `share/POM1/`. Elle tient
  maintenant en un appel.
- **`resolveDataDir`** prenait une liste de sondes ; elle prend un nom.

Cinq `"../` demeurent, et aucun n'est une sonde : le garde-fou de traversée
côté invité de `MicroSD` (un nom fourni par le 6502 ne doit pas sortir de
`sdcard/`) et la ligne « dossier parent » des quatre navigateurs de fichiers
intégrés aux éditeurs portables.

**Nouveau garde-fou `resource_probes_sync`** (`tools/check_resource_probes.py`,
quatrième de la famille `version_sync` / `imgui_pin_sync` / `doc_paths_sync`) :
un littéral `"../` dans le C++ de POM1 échoue, sauf aux cinq sites en liste
blanche — chacun avec sa raison écrite dans le script. C'est le point : cette
dérive ne revient jamais d'un coup, elle revient un site à la fois, chacun
localement raisonnable. Vérifié dans les deux sens (le garde-fou déclenche bien
sur une sonde réintroduite).

Plafonds `architecture_check` abaissés, ce que la règle du cliquet demande :
`mainwindow_lines` 17 378 → 16 955, `memory_lines` 3990 → 3978. Suite complète :
**119 tests verts**, et le build `-DPOM1_DEVTOOLS=OFF` compile toujours.


### Changed — la puce A1-SID est construite au branchement, plus à la construction du cœur

`Memory` fabriquait un `pom1::SID` dans son constructeur. libresidfp y calcule
ses tables de filtre : **~120 ms pour la première puce d'un processus**
(0,46 ms pour chacune des suivantes). Mesuré sur un binaire de test qui ne
touche jamais la carte, c'était **116 ms sur 121 ms — 96 % du temps
d'exécution**. Or `Memory` est construit dans **60 des 68** fichiers de test
qui le référencent, et **deux** seulement branchent l'A1-SID.

`Memory::sidChip()` construit la puce à la première utilisation, sous
`std::call_once` — l'UI et le fil d'émulation peuvent tous deux être le premier
à demander. Tous les accès passent par là, donc aucun appelant ne peut observer
un `sid` nul, et le format d'instantané est inchangé : la section « A1-SID »
est toujours écrite.

Le coût n'a pas disparu, il a **déménagé, et c'est délibéré** : le constructeur
d'`EmulationController` réchauffe la puce **avant tout verrou** et avant que le
fil d'émulation existe. Tous les sites où un frontend la toucherait en premier
tiennent `stateMutex` — `SnapshotPublisher` lit le modèle de puce à chaque
publication, `MachineCoordinator` attache la carte pendant un échange de
topologie — et payer les 120 ms là gèle le callback audio et le fil de rendu
d'autant. `concurrent_frontends_smoke` a attrapé exactement ça (`state-hold`
231 ms contre une porte à 100 ms) sur la première version du correctif.

Mesures, même machine, à chaud :

| | avant | après |
|---|---|---|
| `test_cpu_pc_length` (cœur nu) | 135 ms | 10 ms |
| `hermetic_core_smoke`, construction d'un `Memory` | — | 0,3 ms |
| `ctest` complet (118 tests, série) | 134,9 s | 128,2 s |

`hermetic_core_smoke` §5 épingle les deux moitiés : un cœur nu se construit en
moins de 60 ms — il n'existe pas d'accesseur « la puce est-elle construite ? »
sur lequel s'appuyer, la surface publique de `Memory` étant gelée, donc
l'observable est celui qui comptait — et brancher la carte construit la puce
**et** l'enregistre sur le mixeur injecté. Plafonds `architecture_check` :
`memory_lines` 3965 → 3990 et `controller_lines` 3079 → 3089, ces 35 lignes
étant la couture elle-même ; `memory_public_methods` reste à 188.


### Changed — l'UI ne garde plus de copie de la topologie matérielle

Seize `bool xEnabled` de `MainWindow` doublaient l'état des cartes de la
machine, écrits à chaque bascule, chaque preset, chaque option CLI et chaque
cascade — les commentaires se nommaient eux-mêmes : « mirror UI », « sync UI »,
« mutual exclusion ». Une seconde copie est une chose qui peut diverger, et
c'est la famille dont venait la régression d'auto-dial BBS. Ils ont disparu.

Trois membres les remplacent :

- **`currentCards()`** — la cible mise en scène tant qu'une transaction de
  cartes est ouverte, le `CardSet` publié par la machine sinon. La décision
  elle-même est pure et épinglée : `pom1::StagedCardConfiguration::effectiveCards`,
  §7 de `staged_card_configuration_smoke`. C'est ce que les miroirs servaient
  vraiment à faire : une case doit se cocher à l'instant du clic, alors qu'un
  branchement mis en scène n'atteint la machine qu'au commit.
- **`cardPlugged(CardId)`** pour demander, **`setCardPlugged(CardId, bool)`**
  pour ordonner. La seconde émet la commande **puis relit l'instantané**, si
  bien que la suite de la frame voit les cascades déclenchées (évictions
  Parmigiani, cartes filles) au lieu de la copie d'avant. Une copie
  d'instantané par clic utilisateur, contre une règle qui vit déjà dans
  `CardTopology`. `plugGen2()` fait la même chose pour la GEN2, qui se branche
  par son framebuffer.
- **`EmulationSnapshot::cards`** publie la topologie comme une seule valeur.

Effets de bord notables : `wouldCreateConflict`, `listParmigianiConflicts` et
`resolveParmigianiConflicts` ne reconstruisent plus un `CardSet` à la main
depuis dix miroirs (trois copies des mêmes dix lignes) ; le `gate` du registre
de fenêtres nomme un `CardId` au lieu d'un `bool MainWindow_ImGui::*` ; et
`gateStrictPlug` prend son drapeau **par valeur** — il n'y a plus de miroir à
remettre en place, donc refuser est toute la réponse.

Mesure : **472 références aux seize miroirs → 0** (218 appels de
`cardPlugged`/`setCardPlugged`/`currentCards`), 15 fichiers, plus de lignes
supprimées qu'ajoutées. 118/118 tests avec l'outillage, 90/90 sans, zéro
avertissement, `--headless` et la GUI vérifiés sur bascule de preset.

### Changed — l'audio est un service qu'on donne au cœur, plus un qu'il fabrique

`Memory` construisait son `AudioDevice` dans son constructeur : tout cœur, y
compris celui d'un test unitaire, allait chercher la carte son de l'hôte en se
levant. `src/AudioService.h` porte désormais le seam — `pom1::IAudioService`,
six méthodes, exactement ce que le cœur et le contrôleur utilisent — et
`main_imgui.cpp` possède l'`AudioDevice` (GUI **et** headless) puis le passe par
`MainWindow_ImGui` → `EmulationController` → `Memory`, qui n'en garde qu'un
pointeur. Même forme que `ResourceLocator` (où sont les fichiers ?) et
`DisplayDevice` (où vont les caractères ?) : la décision appartient à qui
construit la machine, pas à la machine.

Trois conséquences, chacune étant le vrai contenu d'une moitié du changement :

- `--audio-latency` devient un argument de constructeur au lieu du statique
  `AudioDevice::setPreferredLatencyMs`, dont la seule raison d'être était de
  franchir l'écart entre `main()` et un périphérique construit quatre couches
  plus bas. Un global de moins.
- `initializeAudioHardware` passe par défaut à **false**, sur `Memory` comme sur
  `EmulationController` : `Memory mem;` est écrit dans **50 fichiers de test**,
  et chacun ouvrait un vrai périphérique audio (~21 ms, ~100 ms pour le premier
  d'un processus, plus les erreurs miniaudio sur une machine CI sans son) pour
  des tests qui n'écoutent rien. Compté sur la ligne que miniaudio journalise
  elle-même, sur une suite complète : **161 ouvertures → 0**. Les deux frontends
  passent `true` explicitement, donc rien d'audible ne change — `--audio-latency
  120` journalise toujours `output cushion 120 ms (1764 frames x 3 periods)`.
- `~Memory` désenregistre ses sources (`removeSource` est une barrière de durée
  de vie). Un service injecté survit à la machine, donc l'ordre de déclaration
  des membres n'est plus la seule chose entre le mixeur et un `CassetteDevice`
  détruit.

`pom1::NullAudioService` est le double en mémoire — pas de mixeur, pas de
scratch, pas de thread — et n'est **pas** `AudioDevice(false)`, qui mixe encore
et reste ce dont les tests audio ont besoin. `hermetic_core_smoke` gagne une §4
qui construit un cœur dessus, le voit enregistrer sa cassette à l'activation
(pas à la construction — le magnéto a son rail à lui) et rendre ses sources à la
destruction.

**La mesure qui justifiait ce chantier était mal attribuée, et c'est corrigé
dans `TODO.md`.** Les ~133 ms d'un cœur hermétique n'étaient pas
l'`AudioDevice` : dans un binaire de test, `AudioDevice(false)` coûte
**0,07 ms**, et le coût réel est la **première construction de `pom1::SID`**
(tables de filtre libresidfp, ~150 ms ; 0,46 ms pour chaque SID suivant du même
processus). Un nouvel item `[S]` le note. Le gain de l'injection est ailleurs, et
il est mesuré, pas déduit.

### Added — l'environnement de développement est optionnel (`-DPOM1_DEVTOOLS=OFF`)

Les éditeurs HGR/TMS (paint + sprites), l'éditeur SFX bipeur, le tracker SID, la
DevBench et les compilateurs BASIC sont un **second produit** qui partageait le
processus, le build et la matrice de portage de l'émulateur : ~18 300 lignes
payées sur Linux / macOS Metal+GL / Windows / WASM / Pi GLES / borne PGO, et une
régression dans un éditeur graphique bloquait une release d'émulateur. L'option
`POM1_DEVTOOLS` (ON par défaut — rien ne change sans le demander) les retire de
la cible. L'émulateur reste complet : seules les fenêtres d'outillage et leurs
entrées de menu disparaissent, plus les colonnes *Create* / *Develop* du sélecteur
de profils et sa rangée Studio, qui redevient un sélecteur à deux colonnes.

Mesure macOS/Metal, arbre neuf, `-j8`, tests désactivés : **149 → 104** unités de
traduction, **4,13 Mo → 2,86 Mo** de binaire (−31 %), **27 s → 20 s** de build.
`-DPOM1_DEVTOOLS=OFF` compile **sans un seul avertissement**, démarre, tourne en
`--headless`, et passe `ctest -L emulator` à 90/90 ; la configuration par défaut
reste à 118/118.

Ce qui rend l'opération petite, c'est la discipline déjà en place : les éditeurs
étaient des modules portables derrière leurs seams hôtes (`IHgrPaintHost`,
`ITmsPaintHost`, `IBenchHost`), donc l'émulateur ne les nomme qu'en **4 fichiers
`MainWindow_*`**. C'est une récolte, pas un refactor. Détails :

- `POM1_DEVTOOLS_SOURCES` dans `CMakeLists.txt` porte la liste — ImGuiColorTextEdit
  en fait partie, `bench/CodeBench.cpp` étant son seul consommateur.
- Le macro est `POM1_DEVTOOLS` (`src/POM1Build.h`), CMake définissant
  `POM1_BUILD_NO_DEVTOOLS` — même forme que `POM1_BUILD_GLES` → `POM1_GL_ES`.
- **Les répertoires d'inclusion sortent aussi de la cible** : une inclusion
  d'éditeur qui échapperait à son `#if` ne compile pas, au lieu de rappeler
  silencieusement 87 fichiers.
- `tests/CMakeLists.txt` ne **déclare** pas les 28 tests d'outillage dans un build
  OFF — les deux bouts de la même partition que la voie `ctest -L emulator`.
- Une arête de la frontière disparaît au passage : `ini_defaults/` se résout
  désormais via `pom1::executableDirectory()` (`ResourceLocator.h`) au lieu de
  `bench::executableDir()`, ce que le cliquet a exigé en refusant l'arête devenue
  périmée. 17 → **16**.

### Added — deux voies de test, et deux cliquets qui tiennent la frontière

`ctest -L emulator` (90 tests) est la porte de release de l'émulateur et est
**verte sans cc65** ; `ctest -L devtools` (28) est la voie de l'environnement de
développement — éditeurs, chaîne DevBench/cc65, compilateurs BASIC et leur
runtime 6502. Chaque test déclaré porte exactement une des deux étiquettes,
assignées en fin de `tests/CMakeLists.txt` à partir de la liste
`POM1_DEVTOOLS_TESTS`. Le classement se fait sur le **sujet**, pas sur le nom :
`bench_basic_inject_smoke`, `bench_logo_inject_smoke`, `applesoft_*`,
`codetank_claudio_gate`, `lib_micro_tests` et les lints assembleur de `dev/`
vérifient le comportement de l'émulateur et restent dans la porte. Absent de la
liste vaut `emulator`, dans ce sens précis : un test d'outillage non étiqueté
atterrit dans la porte de release et la fait rougir sur une machine sans cc65 —
le défaut inverse l'aurait fait disparaître en silence. Le job Linux de `ci.yml`
exécute les deux voies en deux étapes : mêmes 118 tests, coût identique, et une
étape rouge nomme le produit cassé.

`tools/check_architecture.py` gagne deux cliquets, qui figent l'existant plutôt
que de décrire une cible :

- **La frontière de l'outillage** — toute inclusion d'un en-tête de
  `src/{bench,hgrpaint,hgrsprite,tmspaint,tmssprite,sfxbeep,sidtrack}/`, des
  fichiers `Pom1*Host`, des compilateurs BASIC, de `DbgFile` ou de
  `BenchDebugSession` depuis un fichier hors outillage est une arête de
  `allowed_devtools_dependencies`. Il y en a **17**, dans 4 fichiers, tous
  `MainWindow_*` : c'est ce qui rend le passage à `-DPOM1_DEVTOOLS=OFF` borné,
  et le cliquet est ce qui le maintient borné en attendant. Une arête nouvelle
  échoue ; une arête restée dans la base après la disparition de son dernier
  appelant aussi — la liste ne peut que décroître.
- **Les façades gelées** — `memory_public_methods` (188) et
  `controller_public_methods` (201) comptent les déclarations publiques au
  niveau de la classe. C'est le compte, pas le total de lignes, qui dit ce
  qu'un appelant peut demander à l'objet : une façade rétrécit en perdant des
  méthodes. `Memory` est gelé ; les passthroughs par carte d'`EmulationController`
  partent un par un avec leur dernier appelant.

Les trois cases correspondantes quittent `TODO.md` (chantiers 1 et 2).


### Changed — consolidation architecturale, phases 0 à 3

La première moitié de la consolidation est livrée et quitte `TODO.md`. Le build
matérialise désormais les couches `pom1_ui → pom1_app → pom1_devices → pom1_core`,
accepte des dépendances locales configurables et applique un cliquet architectural
sur les dépendances, la taille des façades et le fan-out des en-têtes.

La configuration matérielle a une source de vérité typée (`CardId`,
`CardDescriptor`, `CardSet`) et une politique pure dans `CardTopology`. Presets,
CLI, GUI et mode headless exécutent les mêmes `TransitionPlan`; les dépendances et
conflits des cartes sont couverts exhaustivement.

`MachineCoordinator` applique désormais presets et changements de cartes comme des
transactions déterministes, sans délai en frames : arrêt du CPU, détachement,
reset, configuration, attachement, activation et publication atomique. La matrice
headless couvre les 13 presets et leurs combinaisons de cartes avant toute frame de
rendu.

Enfin, le callback audio ne verrouille, n’alloue et ne réalise plus d’I/O. Les rings
SID/cassette sont SPSC, le retrait d’une source est une barrière de durée de vie,
la hiérarchie des verrous est vérifiée et le stress concurrent exerce ensemble
émulation, snapshots/rendu et mixage sous TSan. `RealtimeDiagnostics` mesure les
attentes, détentions, callbacks, underruns et débordements dans les builds de test.

### Fixed — POM1 ouvrait un port TCP que personne n'avait demandé

`TerminalCard::reset()` appelait `startServer()` sans condition, et
`Memory::resetMemory()` réinitialise toutes les cartes qu'elles soient branchées
ou non. Résultat : **tout** processus POM1 et **tout** binaire de test écoutait
sur `localhost:6502` alors que la Terminal Card est débranchée par défaut.

Mesuré : un `Memory` nu, audio désactivé, carte signalée `enabled? 0`, liait
quand même le port — deux fois — et un second cœur dans le même processus
échouait avec `failed to bind port 6502 (already in use?)`.

Le coût était déjà visible dans l'arbre : `terminal_card_smoke` porte en
en-tête « reset() calls startServer(), so it BINDS localhost:6502. This test
therefore never calls reset() ». Quelqu'un avait remarqué le défaut et l'avait
contourné **dans le test**, qui épinglait du coup les valeurs par défaut du
firmware depuis les initialiseurs de membres plutôt que depuis le `reset()` qui
les restaure réellement.

`TelemetryPort`, juste à côté, n'ouvre son serveur que lorsqu'il est activé
depuis toujours. `TerminalCard::setEnabled()` aligne la carte dessus,
`Memory::setTerminalCardEnabled()` le pilote, et `reset()` ne réécoute que si la
carte est branchée. `--terminal` fonctionne comme avant — les harnais telnet le
passent tous. Le contournement a disparu du test, qui couvre maintenant le vrai
chemin.

### Added — `hermetic_core_smoke`, le critère de sortie sous forme d'assertion

Un cœur construit pour un test ne touche que ce qu'on lui donne. Deux cœurs
coexistent dans un même processus sans socket, `resetMemory()` n'en ouvre pas,
une racine injectée qui contient une ROM est bien celle utilisée, et une racine
vide se rabat sur le moniteur intégré **en le signalant**.

Le test dit aussi explicitement ce qu'il ne prétend **pas** encore : `Memory`
construit toujours un `AudioDevice`, simplement sans initialiser le matériel
quand on le lui demande. Coût de construction : **133 ms contre 205 ms**, le
reste étant précisément cet objet audio — c'est la moitié restante de l'item.

### Fixed — les ressources étaient cherchées à des profondeurs différentes selon l'appelant

Chaque consommateur réimplémentait le même parcours « essayer `x`, puis `../x`,
puis `../../x` » — et ils n'étaient pas d'accord sur la hauteur à remonter.
`Memory::loadROM()` montait d'**un** niveau, les sondes sdcard/disks/cfcard de
**deux**, la sonde CodeTank de **trois**. Lancé depuis `build/tests/`, POM1
trouvait donc les images disque mais pas les ROMs, puis substituait
silencieusement son moniteur Woz intégré (`WARN: loaded from built-in
fallback`) et continuait. Vérifié dans les deux sens : depuis `build/tests/`,
les ROMs se chargent maintenant toutes.

`pom1::ResourceLocator` porte un ordre unique : le répertoire courant et trois
ancêtres, puis le répertoire de l'exécutable et les dispositions que les
empaqueteurs mettent autour (`Resources/` d'un `.app` macOS, `share/POM1/` d'un
AppImage), dédoublonnés. Un chemin **absolu** est rendu tel quel, jamais réécrit
— `--iec-disk /tmp/x.d64` et les sélecteurs de fichiers passent par là.

`defaultLocator()` rend **par valeur** et recalcule la moitié « répertoire
courant » à chaque appel : ce répertoire est un état vivant
(`pom1_macos_provision_user_data_dir()` fait un `chdir` au démarrage, les tests
en font vers des bacs à sable), et le mettre en cache faisait dépendre la
résolution du *moment* où le localisateur était touché pour la première fois.
`rom_fallback_smoke` a attrapé exactement ça — il trouvait encore les vraies
ROMs depuis son bac à sable. Seule la moitié dérivée de l'exécutable est mise en
cache ; elle ne bouge pas.

### Changed — `Memory` reçoit son localisateur de ressources

`Memory` prend un `ResourceLocator` (par défaut, donc tous les appelants
existants sont inchangés) et le **conserve** : `loadROM()` le consulte à chaque
changement de preset, pas seulement à la construction. Les quatre sondes
implicites du constructeur disparaissent.

C'est aussi le seul moyen de dire « aucune ROM nulle part » : les ressources
étant désormais cherchées à côté de l'exécutable, un `chdir` vers un répertoire
vide ne suffit plus — le binaire de test vit lui-même dans l'arbre.
`rom_fallback_smoke` exprime maintenant son intention avec
`ResourceLocator::rootedAt(sandbox)`, ce qui est plus juste que ce qu'il faisait.

### Added — campagne de fuzzing nocturne en CI

Les quatre cibles tournent déjà en pilote déterministe à chaque PR (~1 s). Le
nouveau job `fuzz` de `ci.yml` reconstruit les mêmes vérifications derrière
`LLVMFuzzerTestOneInput` avec clang — Apple clang et g++ ne fournissent pas
libFuzzer, ce qui est précisément pourquoi le pilote déterministe est le défaut
et non le repli — et laisse libFuzzer les piloter 15 minutes chacune.

Le corpus est mis en cache d'une nuit à l'autre et amorcé depuis les fichiers
livrés : les ~120 vidages hexadécimaux de `software/`, les enregistrements de
`cassettes/`, un `.d64`, et un instantané produit par une exécution headless
réelle. L'amorçage aplatit les chemins avec un préfixe dérivé du chemin, car le
même nom de base existe dans plusieurs sous-dossiers de `software/` et une copie
simple n'en gardait qu'un — un corpus silencieusement plus petit qu'il n'en a
l'air. Vérifié à sec : 66 vidages sur 66 arrivent.

Une trouvaille fait échouer le job et téléverse l'entrée fautive en artefact.
C'est elle le livrable : elle devient un cas de régression dans le `*_smoke`
correspondant, comme l'ont fait tous les défauts que ces cibles ont déjà
trouvés.

### Fixed — une carte échouant en cours de désérialisation laissait la machine modifiée

La porte structurelle ajoutée précédemment atteste la **forme** du fichier, pas
la charge utile propre à chaque carte : cette grammaire vit dans le
`deserialize` de la carte, et plusieurs d'entre elles restaurent des champs
avant de pouvoir rejeter ce qui suit. `PR40Printer` a déjà écrit son mode, sa
FIFO et quatre compteurs quand il valide le nombre de lignes du rouleau.

Mesuré avant correction, en forgeant ce compteur : la machine se retrouvait avec
le CPU **et** la RAM de l'instantané (`PC=$1234`, `$0300=$AA`) alors que
l'appelant recevait un échec — plus une imprimante à moitié restaurée par-dessus.

L'application est désormais encadrée d'un retour arrière : une copie de la
machine vivante est conservée et remise en place si la restauration échoue.
Mesures ayant motivé le choix plutôt qu'une refonte des quinze `deserialize` :
sérialiser coûte **16 µs** contre les **452 µs** que prend déjà une
restauration, soit 3,6 % — et cela couvre *toute* défaillance, y compris une
exception levée par une carte. Le chemin de recherche du rewind le paie aussi et
ne le voit pas.

Après correction, même entrée forgée : `PC=$BEEF`, `$0300=$55` — l'état
antérieur, intact.

### Fixed — les chaînes de secteurs D64 étaient parcourues au compteur, pas par détection de cycle

Un D64 est une structure **chaînée** : chaque bloc de répertoire et chaque
secteur de fichier nomme le suivant, et rien nulle part n'enregistre la longueur
d'une chaîne — on l'apprend en suivant les liens. Une image corrompue peut donc
faire pointer une chaîne sur elle-même, et la seule condition d'arrêt saine est
« je suis déjà passé ici ».

Les compteurs qui en tenaient lieu étaient des suppositions, fausses dans les
deux sens : le parcours de fichier autorisait 1000 sauts sur un disque de 683
secteurs, si bien qu'un secteur pointant sur lui-même produisait un « fichier »
de 254 Ko à partir d'une image de 174 Ko ; le parcours de répertoire n'en
autorisait que 256, soit moins que les 683 blocs qu'une chaîne de répertoire
pathologique mais **légale** pourrait occuper. `VisitedSectors` (683 bits) les
remplace tous les cinq : moins cher que le compteur, et exact.

Trouvé en fuzzant. Dans la même passe : les compteurs de blocs libres du BAM
sont désormais bornés par la géométrie au lieu d'être crus sur parole — un
disque tout à `$FF` annonçait « 8670 BLOCKS FREE » sur un disque qui en compte
664. Un secteur ne peut pas avoir plus de blocs libres qu'il n'en contient.
Cosmétique (la valeur alimente le pied de liste du répertoire), mais faux.

### Changed — le D64 se monte depuis la mémoire

`D64Image::mountBytes()` accepte les octets ; `mount(chemin)` lit via
`pom1::readFileBounded()` et délègue, de sorte qu'un fichier et un tampon
passent exactement les mêmes règles d'acceptation — un D64 fait 35 pistes, avec
ou sans les 683 octets d'erreur finaux, et rien d'autre n'en est un. Le format
devient au passage atteignable par un test ou un fuzzer sans disque sur disque,
ce qui est ce qui a permis de trouver les deux défauts ci-dessus.

### Fixed — une sauvegarde tronquée laissait la machine à moitié restaurée

Appliquer un instantané écrit directement dans la machine vivante, section par
section. Un fichier abîmé en cours de route laissait donc un **hybride** tout en
renvoyant un échec propre. Mesuré plutôt que supposé : un instantané réel de
116 933 octets tronqué à 200 octets renvoyait bien `false`, mais le compteur
ordinal valait déjà `$1234` — la valeur de l'instantané — au-dessus d'une RAM
jamais remplacée. Un PC qui pointe dans un programme absent de la mémoire, c'est
une machine qui va exécuter n'importe quoi. Le rejeu du rewind passe par les
mêmes blocs.

`pom1::validateSnapshot()` (pur, dans `SnapshotIO`) tranche à partir des octets
seuls avant que le moindre état ne bouge : magie, version, parcours des
sections avec vérification de chaque longueur contre les octets **restants**,
les deux longueurs légales de la section `MEM`, et le compteur d'événements de
`GEN2VID` borné par la charge utile réellement présente — ce compteur pilote un
`reserve()`. `Memory::loadSnapshot(chemin)` lit désormais le fichier et délègue
au chemin par tampon, de sorte qu'un fichier et un bloc de rewind franchissent
la **même** porte ; lire directement depuis le disque la contournait, et c'est le
chemin fichier qui voit les fichiers que POM1 n'a pas écrits.

La porte couvre tout le cas d'entrée hostile — troncature et longueurs forgées ;
la charge utile propre à chaque carte reste du ressort de son `deserialize`.

### Changed — une seule lecture de fichier bornée pour tous les analyseurs

`pom1::readFileBounded()` (`FileBytes.h`) remplace trois copies du même préambule
« vérifier la taille puis avaler le fichier ». L'ordre est tout l'intérêt :
avaler le fichier **est** l'allocation que la limite existe pour empêcher, donc
une borne vérifiée après la lecture a déjà perdu. Images mémoire, conteneurs
cassette et instantanés passent tous par là, ce qui rend la règle impossible à
oublier sur un quatrième site.

### Fixed — deux défauts des conteneurs cassette, trouvés en les fuzzant

**Une fréquence d'échantillonnage sans borne supérieure.** L'analyseur AIFF a
toujours été borné par son décodeur de flottant 80 bits ; l'analyseur WAV ne
bornait rien. Un fichier pouvait déclarer quatre milliards de hertz et être
accepté. La borne basse comptait davantage : les durées valent
`deltaSamples × CPU_HZ ÷ fréquence` puis sont réduites en `uint32`, et à 1 Hz un
intervalle de 5000 échantillons atteint déjà 5,1e9 — une conversion qu'UBSan
qualifie de « outside the range of representable values of type
`unsigned int` », c'est-à-dire un comportement indéfini, pas un débordement
défini. Les deux analyseurs bornent maintenant la fréquence à une fenêtre
plausible, et `pcmToDurations()` sature dans le domaine des doubles avant de
réduire — les fichiers décodés par miniaudio passent aussi par là.

**NaN et infini traversaient jusqu'au décodeur d'impulsions.** Un conteneur
flottant transporte n'importe quel motif binaire, et rien en aval n'y était
préparé : le décodeur compare chaque échantillon à un seuil, et toute
comparaison avec un NaN est fausse. Un WAV flottant corrompu se lisait donc
comme une ligne plate et ressortait en « no detectable cassette signal », ce qui
n'apprend rien à l'utilisateur sur le vrai problème. Les échantillons non finis
deviennent du silence, sont comptés et signalés.

### Changed — les conteneurs cassette WAV et AIFF deviennent des fonctions pures

`CassetteDevice` lisait le fichier, l'analysait, le mixait et rangeait le
résultat d'un seul tenant. Aucun des deux analyseurs ne pouvait donc être testé
ni fuzzé sans périphérique audio, et celui de l'AIFF — écrit par POM1 puisque
miniaudio n'a pas de moteur AIFF, et que l'AIFF est ce qu'émet le synthétiseur
`ACIace` d'Uncle Bernie — n'était gardé que par un seul test de bout en bout.

`src/PcmFile.{h,cpp}` reçoit des octets et rend du flottant mono, une fréquence
et un diagnostic. `CassetteDevice::loadPcmTape()` est le corps partagé
taille-lecture-analyse-décodage derrière `loadWavTape` et `loadAiffTape`. Rien de
partiel ne s'échappe : un refus ne porte aucun échantillon, et une largeur non
supportée est refusée **avant** de décoder la moindre trame — l'ancienne version
la découvrait depuis l'intérieur de la boucle, après avoir déjà mixé et rangé
toutes les trames précédentes.

**Bornes.** `kMaxPcmFileBytes` (256 Mo) est vérifiée avant la lecture du fichier.
`kMaxPcmFrames` n'est pas nouvelle : c'est la limite de trente minutes que
`loadMiniaudioTape()` applique depuis toujours, dont le commentaire dit qu'elle
« prevents accidental 2-hour podcast loads from chewing memory ». Les deux
analyseurs écrits à la main n'avaient **aucune limite**, donc le cas exact que
décrit ce commentaire était refusé en `.mp3` et accepté en `.wav`. La troncature
est signalée, jamais silencieuse.

`pcm_file_smoke` (douze sections) et `pcm_file_fuzz_smoke` couvrent l'ensemble ;
une campagne de 200 000 entrées sous ASan+UBSan est propre.

### Fixed — une adresse trop large ne s'interprète plus différemment selon l'OS

Trouvé en durcissant les chargeurs, pas en les fuzzant. Les analyseurs
convertissaient les jetons hexadécimaux avec `strtol`, dont le type `long` fait
64 bits sur macOS et Linux mais 32 bits sur Windows : un jeton trop large
saturait différemment sur chacun. Une ligne d'adresse TurboType `100000000` se
tronquait en `$0000` sur les hôtes 64 bits — les octets suivants partaient donc
en **page zéro** — pendant que l'hôte 32 bits plafonnait ailleurs et les
ignorait. Même fichier, deux machines, deux résultats, dont aucun n'était
l'intention du fichier.

Les adresses passent désormais par une accumulation vérifiée. L'analyseur
structuré en lignes n'a aucune règle de jeton fusionné sur laquelle se rabattre :
une adresse qu'il ne peut pas représenter est une ligne malformée, ignorée et
signalée. L'analyseur joint conserve sa lecture documentée du même jeton
(données fusionnées + adresse). Le comportement est identique sur toutes les
plateformes.

### Added — durcissement et fuzzing des chargeurs d'image mémoire

`kMaxMemoryImageBytes` (8 Mo) borne l'entrée : `Memory::loadHexDump()` vérifie la
**taille du fichier avant de le lire**, `parseMemoryImage()` revérifie pour tout
autre appelant, et les deux partagent `memoryImageTooLargeMessage()` pour ne pas
raconter deux histoires différentes de la même limite. Calibrage : le plus gros
programme livré fait ~100 Ko, une image couvrant les 64 Ko en WOZMON quelques
centaines de Ko. Les avertissements de quartet impair indiquent maintenant
l'**adresse** de la première occurrence, pas seulement un décompte.

`memory_image_fuzz_smoke` prend deux formes issues d'un seul fichier. Par défaut,
un pilote **déterministe** (graine fixe, donc un échec se reproduit à l'octet
près) rejoue un corpus de formes réelles, le mute vers la ponctuation qui pilote
l'analyseur, et soumet **chaque** résultat au contrat de l'en-tête : confinement
aux 64 Ko, `byteCount` cohérent avec les plages, aucune écriture sur une image
rejetée, `zones()` en accord plage par plage, sortie bornée par l'entrée, et
résultat identique à la seconde analyse. Avec `-DPOM1_FUZZERS=ON`, les mêmes
vérifications passent derrière `LLVMFuzzerTestOneInput` pour la campagne ASan
longue. Le pilote déterministe est le défaut et non le repli : Apple clang ne
fournit pas libFuzzer, donc une porte uniquement libFuzzer ne s'exécuterait
simplement pas sur macOS.

Une campagne de 60 000 entrées sous ASan+UBSan n'a révélé aucune faute mémoire.

### Changed — les chargeurs d'image mémoire deviennent des fonctions pures

`Memory::loadHexDump()` entrelaçait analyse, écritures dans `mem[]` et appels à
`pom1::log()`. Les trois dialectes vivent désormais dans
`src/MemoryImageLoader.{h,cpp}` : `pom1::parseMemoryImage(contenu, nom)` reçoit
des octets et un nom — jamais ouvert, il ne sert qu'au libellé des diagnostics
et à la seule règle d'extension dont les formats aient besoin, `.tur` — et rend
un `MemoryImage` complet : écritures en plages contiguës, adresse d'exécution,
nombre d'octets et diagnostics typés. Ni `Memory`, ni système de fichiers, ni
journal. `Memory::loadHexDump()` se réduit à analyser, décider, appliquer.

Le comportement de chaque dialecte est conservé à l'octet près — WOZMON hex,
Intel HEX et TurboType, y compris les pièges que chaque branche existe pour
éviter (fusion des zones, jeton de 1-2 chiffres avant `:` traité comme donnée,
jetons fusionnés données+adresse et données+run, marqueur `X` nu, détection
d'Intel HEX par la forme). Les 120 fichiers livrés sous `software/` sont tous du
hex WOZ 6502 ; aucun n'est de l'Intel HEX.

**Plus aucune mutation partielle.** L'ancien chargeur écrivait chaque
enregistrement Intel HEX en RAM au fur et à mesure et ne découvrait qu'ensuite
celui qui dépassait les 64 Ko du 6502 — les précédents étaient déjà en mémoire.
De même, un fichier n'établissant aucune adresse déversait ses octets errants en
page zéro avant de renvoyer une erreur. Une image rejetée ne porte désormais
aucune écriture.

`memory_image_loader_smoke` couvre les dix points sans machine émulée ni
fichier ; le cliquet architectural descend en conséquence (`memory_lines`
4247 → 3892, `sources_outside_test_devices` 87 → 86).

### Fixed — l'auto-connexion BBS depuis `software/NET` composait dans le vide

Charger `software/NET/bbs.fozztexx.com.txt` branchait bien le Wi-Fi Modem et
lançait bien le programme d'auto-dial à `$0280` — mais plus aucune connexion
n'aboutissait. Le programme tournait pourtant : il écrivait sa commande `ATDT`
dans de la RAM nue, parce que la carte n'était plus sur le bus.

`CardConfigurationRequest::cards` décrit une topologie **absolue** :
`MachineCoordinator` détache toute carte que la requête ne nomme pas. Or la
requête vide et l'absence de requête étaient indiscernables. `performMemoryLoad()`
— comme six chemins du DevBench — valide toute transaction ouverte avant de
toucher à la mémoire, et `applyMachineConfig()` a déjà validé la sienne : cet
appel de vidange trouvait donc une requête vide et l'appliquait, c'est-à-dire
demandait la machine sans aucune carte. Le modem quittait le bus quelques
microsecondes avant que le programme n'écrive en `$B000`.

`pom1::StagedCardConfiguration` porte désormais cette distinction. Une
transaction non ouverte ne valide rien, et le premier `stage()` initialise la
cible depuis la machine **vivante** — cartes et options de carte — de sorte
qu'un amendement (« brancher aussi le TMS9918 », les lanceurs du sélecteur de
profil, un changement de cartouche DevBench) s'ajoute au bus au lieu de le
remplacer par une machine mono-carte ; `applyMachineConfig()` continue d'écraser
`cards` en bloc. `clear()` réinitialise tout sauf `mode`, seul porteur du choix
Strict/Fantasy. Trois lanceurs du sélecteur amendaient après validation et
n'avaient donc plus aucun effet : ils valident maintenant leur amendement.

Le type est de la logique de valeur pure — ni ImGui, ni GLFW, ni `Memory` — donc
atteignable par un test (`staged_card_configuration_smoke`), suivant la même
règle de couture qu'`Apple1KeyMap` et `WindowGeometry`. Le nouveau `bbs_autodial`
épingle l'autre bout, hors ligne : les deux fichiers livrés chargent toujours en
`$0280` avec une chaîne `ATDT` terminée par un NUL, puis le même programme — sa
seule cible de composition réécrite vers un serveur BBS factice tenu par le
harnais sur `127.0.0.1` — atteint l'ACIA en `$B000`, se connecte **une seule
fois** et affiche `CONNECT`. Le comportement manuel d'`ATmodem.txt` est inchangé.

### Fixed — `--run … --step N` n'exécute que les N instructions demandées

Vrai défaut, trouvé en poursuivant les trois micro-tests TMS9918 qui échouaient sous
TSan : ce n'était **pas** une course, mais une non-reproductibilité du harnais
headless. `--run` passe par `jumpTo()`, qui **démarre le thread d'émulation**
(`runRequested.store(true)` + `notify_all`), et c'est le premier `stepCpu()` qui
finit par l'arrêter. Entre les deux s'écoule du **temps réel** — pendant lequel le
programme 6502 avance librement. Sur un poste au repos l'intervalle vaut quelques
microsecondes et rien ne se voit ; sous un sanitizer ou sur une machine chargée il
vaut des millisecondes, soit des milliers d'instructions, et
`--run X --step N` cesse de signifier « exécute exactement N instructions depuis X ».

D'où une boîte aux lettres RAM différente à chaque exécution, et des micro-tests
qui échouaient de façon **intermittente** — confirmé en local : le même binaire
échoue sous charge et passe à vide. Le premier correctif détectait qu'un `--step`
figurait au plan, mais appelait encore `jumpTo()` puis `stopCpu()` : il réduisait la
fenêtre sans la fermer. Le nocturne TSan du 26 août l'a reproduite sur
`t04_lr_fill.s`.

`runDeferredActions` utilise désormais `jumpToPaused()` dans ce cas. Le premier run
TSan suivant a confirmé que `t04_lr_fill.s` était réparé, mais a révélé la seconde
moitié du même défaut : le `--load` placé avant `--run` démarrait lui aussi le CPU.
Le programme pouvait donc avancer entre **Load et Run**, avant même d'atteindre le
nouveau saut arrêté. Les deux chargeurs CLI reçoivent maintenant l'intention du plan
et restent arrêtés de bout en bout dès qu'un `--step` est présent ; ni Load ni Run
n'arme le thread asynchrone avant les N `stepCpu()`.

Un `--load` / `--run` sans `--step` conserve son comportement interactif. Le test
d'exécution passe désormais par un vrai fichier binaire et épingle PC, RAM et état
arrêté après une attente réelle, couvrant les deux transitions. `lib_micro_tests`
conserve les assertions fonctionnelles 6502 / TMS9918 complètes.

### Fixed — `sid_audio_smoke` : un seuil chronodépendant, infranchissable sous instrumentation

Dernier obstacle après les suppressions ci-dessous : ce test exige plus de 100
échantillons non nuls et n'en obtenait que 88 sous TSan. Ce n'était **pas** une
course, mais une dépendance au temps réel inhérente au test — un vrai périphérique
audio tourne pendant qu'il s'exécute, et son callback **vide l'anneau du SID**
pendant que la boucle d'émulation le remplit. Instrumentée, l'émulation avance ~20×
plus lentement tandis que le callback garde sa cadence temps-réel : le drainage
l'emporte et il reste moins à lire. Le seuil connaît désormais l'instrumentation
(`__has_feature` côté Clang, `__SANITIZE_*` côté GCC) et descend à 20 dans ce cas.
Ce n'est pas du laxisme : la propriété testée est « les écritures atteignent la puce
à travers le bus », que 20 échantillons non nuls avec un pic réel établissent aussi
bien que 100 — même raisonnement que la mise à l'échelle ×5 des `TIMEOUT` déjà
présente dans `tests/CMakeLists.txt`. Le build normal conserve le seuil strict.

### Fixed — les deux jobs sanitizer étaient rouges pour du code qui n'est pas le nôtre

Déclenchés à la main pour vérifier le travail du jour sur la concurrence (atomiques
dans `CassetteDevice`, ordre de destruction), les deux jobs nocturnes sont tombés —
et le run nocturne du **commit précédant cette session** montre qu'ils tombaient
**déjà**. Ce ne sont pas des régressions : les deux échouent exclusivement dans
`src/third_party/libresidfp`. TSan signale une course dans
`FilterModelConfig::Randomnoise::getNoise()` (les tables de filtre sont construites
sur des threads de travail), UBSan un décalage de valeur négative dans
`ExternalFilter.h:134`. Les deux sont en amont, et atteints par presque toute la
suite puisque tout test qui construit un `Memory` construit un SID.

Conséquence : deux jobs en échec permanent pour une raison sur laquelle personne ne
peut agir — donc deux jobs que personne ne lit, et les courses qu'ils existent pour
attraper (thread d'émulation × thread de rendu × callback audio) ne remontaient
jamais. `tests/tsan.supp` et `tests/ubsan.supp` taisent ce bruit **tiers
uniquement**, avec la règle écrite en tête de chaque fichier : jamais un symbole
POM1 — une course chez nous est un bug à corriger, pas à masquer. Vérifié en local :
avec la suppression TSan, les tests qui abandonnaient passent.

*Note pour l'avenir :* lancer la suite instrumentée fait apparaître des dialogues
macOS « POM1 a quitté de manière imprévue ». C'est attendu — `halt_on_error=1` fait
abandonner le processus dès le premier rapport, et le rapport de crash porte la
signature `__tsan`. Ce n'est pas l'application qui plante.

### Fixed — le paquet Windows échouait en construisant des binaires de test

Premier essai à blanc du packaging depuis le 9 août (déclenché à la main, exactement
ce que le `schedule:` hebdomadaire ajouté ce matin fera tout seul) : Linux, Raspberry
et macOS passent, **Windows tombe** — non pas sur le paquet, mais sur `vcpkg
z-applocal`, l'étape de post-build que vcpkg attache à *chaque* exécutable, qui meurt
en `MSB3075` sur `test_rewind_buffer.exe`. Le job de packaging construisait en effet
l'arbre entier, ~100 binaires de test compris, alors qu'il n'expédie que `POM1.exe`.
La configuration passe désormais `-DPOM1_ENABLE_TESTS=OFF` : le mode d'échec
disparaît avec les cibles qui le portaient, et le job y gagne plusieurs minutes.
macOS faisait déjà l'équivalent (`--target pom1_imgui`), ce qui explique ses 2 min 55
face aux 7 min 40 de Windows. Les tests restent compilés **et exécutés** sur les
trois bureaux par `ci.yml` — c'est là qu'ils ont leur place, pas dans un packager.

### Fixed — `headless_preset_matrix` ne pouvait pas passer sur Windows (tiret cadratin + encodage local)

Suite directe du correctif MSVC : une fois le build Windows réparé, `ctest` a pu
s'exécuter là-bas **pour la première fois depuis que ce job existe**, et un test est
tombé — les 13 presets rapportés en échec avec « no 'headless run complete' line »,
alors que la ligne était **bien présente**, visible dans le journal juste sous chaque
échec. `tools/test_headless_presets.py` la cherchait avec un motif contenant un
**tiret cadratin** (`—`, U+2014), et `subprocess.run(text=True)` décode avec
l'encodage préféré de la machine : **cp1252 sur Windows**, quand POM1 écrit de
l'UTF-8. Le tiret arrivait donc en `â€"` et la recherche ne pouvait jamais aboutir.
Deux corrections, chacune suffisante et gardées toutes les deux : l'encodage est
**explicite** (`encoding="utf-8", errors="replace"`), et le motif accepte n'importe
quel séparateur (`\D+`) — l'assertion porte sur le compteur de cycles et le PC, pas
sur la ponctuation du message, et lier un test multiplateforme à un caractère
non-ASCII est précisément ce qui l'a cassé. Vérifié en rejouant le décodage cp1252
exact : l'ancien motif ne correspond pas, le nouveau si.

### Fixed — le build Windows était cassé sur `main` (C3493, capture implicite)

Trouvé en regardant enfin la CI : le job `windows` échouait **avant** le travail
d'aujourd'hui (déjà rouge au 23 août et sur le run nocturne), sur une divergence de
compilateurs. `evictMemoryMapRegionsForJukeBox` déclare deux `constexpr` locaux et
les lit depuis une lambda **sans capture** ; GCC et AppleClang l'acceptent, MSVC
refuse — `C3493: cannot be implicitly captured because no default capture mode has
been specified`, suivi de deux `C2064` en cascade. Les deux constantes deviennent
`static constexpr` : le stockage statique supprime toute question de capture et la
lambda reste sans capture. C'est exactement la classe de panne que le job Windows
par-push a été ajouté pour attraper — encore fallait-il lire son verdict.

### Fixed — le chemin audio ne fait plus attendre le thread d'émulation

Les deux défauts que l'audit général avait relevés dans `CassetteDevice` sont
corrigés — même famille que la cicatrice d'août : une **durée** de détention de
verrou, que ni `LockOrder.h` ni TSan ne voient, seule la lecture du chemin d'appel
la trouve.

**Publication de la position sans verrou.** `fillAudioBuffer` tient
`audioStreamMutex` pendant `ma_decoder_read_pcm_frames`, dont les recharges font un
`fread` disque sur le thread temps-réel ; en face, `SnapshotPublisher::publish`
lisait la position et la durée de lecture — donc réclamait ce même verrou — **en
tenant `stateMutex`**, à chaque trame. Avec une cassette en mode flux montée, chaque
tranche d'émulation pouvait ainsi se bloquer sur une lecture disque tout en tenant
le verrou d'état, et le moindre appel d'interface attendait derrière. `audioStreamCursor`
et `audioStreamTotalFrames` deviennent des atomiques et les deux accesseurs ne
verrouillent plus : une lecture déchirée est impossible et une position vieille d'une
trame est invisible dans un afficheur de progression.

**Clic mécanique synthétisé hors verrou.** `playMechanicalClick` allouait ~13 ko et
enchaînait ~3400 itérations de `sin`/`exp` **sous `audioMutex`** — que le callback
temps-réel réclame à chaque période, et, depuis `loadAudioStream`, avec
`audioStreamMutex` par-dessus. D'où une micro-coupure à chaque transition du deck
(insertion, éjection, programme↔flux) : négligeable sur un bureau, audible sur la
borne Pi. La forme d'onde est désormais calculée **hors du verrou**, dans un cache
indexé par fréquence d'échantillonnage ; le verrou ne couvre plus qu'une copie sans
allocation (le vecteur réutilise son stockage dès le deuxième clic). Cela remet le
code en accord avec la règle inscrite dans `AudioDevice.h` — le callback audio ne
doit ni allouer ni attendre un travail lent.

*Troisième piste de la même passe, écartée après vérification :* un
`SetNextWindowSize(..., FirstUseEver)` qui semblait suivre un `applyPendingLayout`
(ce qui écraserait la géométrie du preset) était un **faux positif de mon analyse** —
les deux appels appartiennent à des fonctions différentes. L'analyse corrigée, qui
tient compte des frontières de fonction, ne trouve aucune violation dans l'arbre.

### Fixed — audit général : use-after-free à la fermeture, et un Intel HEX commenté écrit n'importe où

Deux vrais défauts hors du DevBench, trouvés en élargissant la chasse au projet
entier (audit par sous-systèmes, chacun exigeant une preuve par le code plutôt
qu'une intuition).

**Use-after-free à l'arrêt.** Dans `MainWindow_ImGui`, `emulation` était déclaré
**avant** `screen` — or les membres meurent dans l'ordre inverse, donc `screen`
mourait en premier. Le hic : `~EmulationController` est le **seul** endroit où le
thread d'émulation est arrêté (`~MainWindow_ImGui` n'arrête pas le CPU,
`destroyPom1()` ne rend que des textures), et le contrôleur détient un pointeur
**brut** vers l'écran, installé comme `DisplayDevice` de `Memory`. Pendant
`~Screen_ImGui`, la tranche encore en vol pouvait donc exécuter une écriture
`$D012` → `displayDevice->onChar()` → verrouillage d'un `bufferMutex` détruit.
Crash à la fermeture dès qu'un programme produit de la sortie écran — une invite
BASIC suffit. Corrigé par l'ordre de déclaration, la discipline que `Memory.h`
applique déjà à `AudioDevice` face à ses `AudioSource`, avec le commentaire qui
interdit de les réordonner.

**Un Intel HEX précédé d'un commentaire était écrit en page zéro.**
`looksLikeIntelHex` ne franchissait que les lignes **vides** — pas les commentaires
(`; built by ca65`, ce que produisent les assembleurs et `srec_cat`) ni un BOM UTF-8
(ce qu'ajoute un éditeur Windows sans rien demander). Le fichier échappait donc à la
détection et repartait dans le parseur WOZMON, qui écrit les **en-têtes** des
enregistrements — compteur d'octets, adresse de chargement, type — comme des
**données**, à l'adresse courante. Mesuré : 24 octets écrits à `$140D` au lieu de 16
à `$0300`, **retour succès, aucun avertissement** — exactement la corruption
silencieuse que cet en-tête déclare empêcher. Détecteur et parseur franchissent
désormais commentaires et BOM (les deux **doivent** s'accorder sur ce qu'est un
enregistrement, sans quoi un fichier accepté par l'un meurt sur la première ligne de
l'autre). `intel_hex_smoke` épingle les deux cas et **la mutation le prouve** :
sans le correctif, l'assertion tombe sur le `$140D`.

### Added — `shortcuts_sync` : l'invariant « jamais de CTRL+lettre » cesse d'être une simple consigne

Trouvé en passant l'audit du débogage à un balayage **général** : `CLAUDE.md` et un
commentaire au-dessus de la table disent tous deux « n'ajoutez JAMAIS un accord
CTRL+lettre à `shortcuts[]` », et rien ne l'empêchait. `handleGlfwKey` distribue les
raccourcis **avant** que l'Apple-1 ne voie la touche, donc une telle entrée masque le
code de contrôle ASCII de cette lettre et le rend **intypable sur la machine
émulée** — Ctrl-C (interruption d'Integer BASIC) et Ctrl-H (éditeur de ligne
d'Applesoft Lite) étant ceux qu'on remarque. La panne est **silencieuse** : build
vert, tests verts, et un code de contrôle qui cesse discrètement de fonctionner.
Ce n'est pas hypothétique — `Ctrl+O/S/V/Q` (Charger/Sauver/Coller/Quitter) ont été
livrés puis retirés après qu'on les a trouvés en train d'avaler `$0F`, `$13` (XOFF),
`$16` et `$11` (XON). La table vit dans une TU d'interface qu'aucun binaire de test
ne lie, donc `tools/check_shortcuts.py` la lit comme texte — même forme que
`check_crt_params.py` / `check_window_registry.py`. Les accords de touches de
fonction restent autorisés (F1-F12 ne sont pas de l'ASCII), donc `Ctrl+F5` garde son
raccourci. **Vérifié par mutation** : réintroduire le `Ctrl+S` historique fait
rougir la garde, qui nomme le `$13` masqué.

### Fixed — chasse n°14 : le suivi du PC pointait des lignes d'un programme qui ne tournait pas

Encore une « limite connue » écrite sans être mesurée — j'avais noté qu'un
*File → Load* hors du Bench laissait la table décrire « le dernier build », en
présentant cela comme inoffensif. Ça ne l'est pas : la table survit au chargement,
et `sourceLineForPc` mappe alors le PC du **nouveau** programme sur les lignes de
**l'ancien** source. Les deux se chargent typiquement à `$0300`, donc les adresses
se recouvrent et le curseur se pose sur une ligne parfaitement plausible — et
fausse. Un débogueur qui montre une mauvaise ligne est pire qu'un débogueur muet.

En cherchant l'étendue, le cas s'est révélé plus large et plus banal que le
*File → Load* : **un simple Verify** adopte la table sans rien charger, si bien
que le suivi du PC mappait déjà, dans ce cas, le programme précédent. La table
décrivait le **binaire bâti**, jamais « ce qui est en mémoire » — la confusion
était dans la conception, pas dans un chemin particulier.

`EmulationController` publie donc un **`programGeneration()`** — compteur monotone,
même forme que le `rewindGeneration_` voisin — incrémenté par les quatre opérations
qui font qu'un octet à une adresse donnée appartient à un autre programme :
`loadBinary`, `loadBinaryToRam`, `hardReset`, restauration de snapshot. La session
n'active le suivi du PC qu'après un `markProgramLoaded(stamp)` (posé par les
chemins Run, jamais par Verify) et se tait dès que le compteur diffère. Le point
d'arrêt, lui, reste armable après un Verify — armer puis lancer est le geste
naturel. Les nouveaux scénarios de `bench_debug_session_smoke` épinglent les trois
états : table sans chargement (muet), chargée (suit), compteur changé (muet).

### Fixed — chasse n°13 : un point d'arrêt fantôme gelait la machine sans explication

Le pire symptôme de la série, trouvé en confrontant les commentaires de cycle de vie
de `Pom1BenchHost` au code qu'ils décrivent. Abandonner la session de débogage
(changement de profil, injection d'un interpréteur) effaçait notre **comptabilité**
mais pas le **point d'arrêt réel** posé dans le CPU. La plupart de ces chemins
réinitialisent la machine — et `M6502::reset()` désarme le point d'arrêt, ce qui
masquait le trou — mais **pas tous** : le démarrage à chaud de `injectBasic` saute
délibérément l'ensemble reset+rechargement pour préserver un programme tapé au
REPL (son propre commentaire le dit). Conséquence : un point d'arrêt laissé à une
adresse de l'ancien programme asm restait armé **sous l'interpréteur**, et dès que
le PC passait dessus — plausible, un Applesoft résident balaie largement la zone
`$0300+` où vivent les programmes du Bench — la machine se garait toute seule. Aucun
marqueur, aucune bannière, rien dans le Bench pour l'expliquer, puisque la session
avait justement tout oublié : côté utilisateur, « BASIC se fige au hasard ».

Plutôt que de rustiner ce seul chemin, la règle devient uniforme : `dropDebugSession()`
demande d'abord à la session si le point d'arrêt de la machine est **le nôtre**, le
retire dans ce cas, puis invalide — et les trois sites de reprogrammation passent par
là (un point d'arrêt appartenant à la fenêtre Debug est toujours laissé strictement
intact). L'**ordre est contraignant** et c'est ce que le nouveau scénario de
`bench_debug_session_smoke` épingle : une fois la session invalidée, elle ne peut
plus dire à qui appartient le point d'arrêt, donc une implémentation qui
invaliderait d'abord le laisserait fuir dans le programme suivant.

### Fixed — chasse n°12 : « les spans de données portent toujours `type=` » était faux

Passe consacrée à la leçon de la n°11 — **une limite affirmée n'est pas une limite
mesurée** — appliquée aux deux autres affirmations non chiffrées de ce code. L'une
d'elles était fausse. Le commentaire du parseur soutenait que l'attribut `type=`
marque les données et que « les spans d'instructions n'en portent jamais » ;
mesuré sur le vrai ld65, `type=` **référence un descripteur de type optionnel**
(les records `type id=N,val="…"` du fichier), pas un drapeau données/code — et il
n'est pas émis pour `.asciiz` ni `.dword`, qui se mappaient donc comme du code.
Cliquer sur une ligne de message armait un point d'arrêt mort. La moitié « aucune
instruction n'en porte » est en revanche confirmée.

Comptage sur le corpus 6502 du dépôt plutôt que sur une intuition : les deux
filtres (descripteur `type=` + segments sans `oname`, ce dernier livré en n°11)
couvrent **14 282 des 14 296** directives de données — `.byte` 12 439 (couvert sous
**toutes** ses formes : valeur seule, liste, `"chaîne"`, `"chaîne",0`), `.res`
1 761, `.word` 82, `.addr` 0 — et laissent **14 `.asciiz`** et 0 `.dword`. Le trou
est donc réel mais minuscule, et surtout **connu au chiffre près** au lieu d'être
décrit par un adjectif. Code, en-tête et `DEVBENCH.md` disent maintenant cela.

`bench_cc65_smoke` épingle la couverture contre le vrai outil : les quatre formes
de `.byte` et `.word` doivent s'accrocher au-delà de la ligne de données, le code
qui les entoure doit rester atteignable — et une assertion documente
délibérément la limite `.asciiz`, de sorte qu'un changement de cc65 se manifeste
par un test rouge plutôt que par un point d'arrêt qui ne déclenche pas.

### Fixed — chasse n°11 : cliquer sur une déclaration de variable armait un point d'arrêt mort

Le « point aveugle `.res` » que la chasse n°1 avait **documenté sans le mesurer**
s'est révélé être l'idiome le plus courant du corpus : **1761 lignes `.res`** dans
les sources 6502 du dépôt, puisque c'est ainsi qu'on déclare une variable. Vérifié
sur le vrai ld65 : leur span ne porte **pas** d'attribut `type=`, donc le filtre des
données de la chasse n°1 ne les voyait pas — cliquer sur une déclaration armait un
point d'arrêt à une adresse que le PC n'atteint jamais, **silencieusement**, très
exactement le défaut corrigé en n°1 mais pour un cas bien plus fréquent que les
tables `.byte`. Le discriminant existe et il est structurel : ld65 rapporte même un
segment `type = bss` comme `type=rw` (le type du cfg est donc inutilisable), mais un
segment écrit dans aucun fichier de sortie **n'a pas d'attribut `oname`** — il ne
contribue aucun octet au binaire, donc ne peut contenir aucun code. Ces segments
sont désormais exclus du mappage, ce qui couvre BSS, ZEROPAGE et tout segment non
chargé. Le point aveugle résiduel se réduit à un `.res` **à l'intérieur d'un segment
chargé** (un tampon inline entre deux instructions), réellement indiscernable dans
le fichier — et documenté comme tel, cette fois après mesure. Épinglé des deux côtés
(miniature + fixture réel avec segment BSS) et **vérifié par mutation**.

Corrigé au passage, sur la même piste : le message d'échec accusait `ca65 -g` alors
qu'un source ne produisant **aucun code** (que des équates et des commentaires — ld65
le lie sans broncher en un binaire de 0 octet) donne un fichier identique. Le message
nomme désormais les deux causes possibles au lieu d'envoyer chercher un `-g`
manquant qui ne manque pas.

### Fixed — chasse n°10 : le débogage échouait en silence, et pouvait adopter la table d'un autre build

Passe consacrée aux **chemins d'erreur**, là où la n°9 avait trouvé son bug.
**(1) Trois `return` muets.** Quand la table ne pouvait pas être adoptée — pas de
fichier de débogage, fichier vide, ou analyse en échec — le bouton point d'arrêt
n'apparaissait tout simplement pas, et **rien nulle part ne disait pourquoi**. Le
plus embarrassant est que le diagnostic existait déjà : `parseDbgFile` rédige des
messages destinés à l'utilisateur (« *was the source assembled with ca65 -g?* »)
et les trois `return` les jetaient. Le cas est concret — sans `-g`, ld65 écrit
quand même un fichier d'apparence plausible dont les records `line` n'ont pas de
`span=` (vérifié sur le vrai outil) — et rendait un problème de chaîne de
compilation indiscernable d'une fonctionnalité absente. La console du build affiche
désormais `[warn] source-level debugging unavailable: <cause>`, une fois, juste
après l'édition de liens censée la produire. **(2) Adoption d'un fichier étranger.**
`adoptDbgInfo` adoptait n'importe quel `/tmp/pom1_bench.dbg` trouvé, or les cibles C
partagent le chemin d'exécution et l'appellent **sans jamais avoir demandé de
`--dbgfile`** : un fichier résiduel (autre instance de POM1, build interrompu)
aurait décoré un programme C avec les lignes d'un programme asm. L'adoption est
maintenant conditionnée à un drapeau que seule la branche asm lève, après avoir
réellement demandé le fichier. `bench_cc65_smoke` épingle le cas « sans `-g` »
contre le vrai outil : l'analyse doit échouer **et** nommer la cause.

### Fixed — chasse n°9 : une compilation ratée perdait le point d'arrêt pour de bon

Premier bug trouvé **grâce** à l'extraction de la passe précédente : en auditant le
code le plus frais, la question « que devient la ligne à ré-armer quand un build
échoue ? » n'avait pas de bonne réponse. Le ré-armement de la passe n°3 mémorisait
la ligne dans une **variable locale au build** — or il y a **douze sorties
anticipées** entre l'effacement du point d'arrêt (en tête de build) et le
ré-armement (après l'édition de liens), une par diagnostic ca65/ld65/cl65. Autrement
dit : point d'arrêt ligne 10, faute de frappe, Verify, `ca65` refuse → le point
d'arrêt est effacé **et l'intention perdue** ; corriger la faute et recompiler ne le
ramenait pas. Rater une compilation étant l'événement le plus courant du
développement, c'était le chemin le plus fréquenté de la fonctionnalité. L'intention
vit désormais **dans la session** : `beginRebuild()` la mémorise (et retire la table
lui-même, puisque toutes les adresses vont bouger), `rearm()` la consomme — et un
build qui échoue n'appelle jamais `rearm()`, ce qui est exactement ce qui fait
survivre l'intention jusqu'au prochain build réussi. Seul `invalidate()` (la machine
elle-même qui change : profil appliqué, interpréteur injecté) l'abandonne, parce que
restaurer un point d'arrêt dans un autre programme piquerait une adresse qui a changé
de sens. Épinglé par deux scénarios de `bench_debug_session_smoke` (build raté puis
réussi ; changement de machine qui abandonne bien l'intention) — et le test a
lui-même attrapé le changement de sémantique de `beginRebuild`, qui retire maintenant
la table dans tous les cas.

### Changed — chasse n°8 : la machine à états du débogage sort de l'UI et devient testable (`BenchDebugSession`)

Huitième passe, et la dernière zone aveugle refermée. Le constat des sept
précédentes était toujours le même : les décisions du débogage source (basculer un
point d'arrêt, savoir si le point d'arrêt de la machine est encore le nôtre,
reporter la ligne armée à travers une reconstruction) vivaient dans
`Pom1BenchHost`, qui exige un MainWindow — donc **aucun test ne pouvait les
atteindre**, et c'est précisément là que la majorité des dix défauts se cachaient,
chacun vérifié à la lecture seule. `src/BenchDebugSession.{h,cpp}` les extrait en un
module **pur** — exactement la couture que `CLAUDE.md` prescrit déjà pour MainWindow
(*« anything that is a decision rather than a draw call belongs on this side »*).
Le point d'arrêt de la machine n'y est pas possédé : chaque question reçoit l'état
courant `(armé, adresse)` en argument et chaque réponse est une **intention** que
l'appelant exécute — c'est ce qui rend « ce point d'arrêt est-il encore le nôtre ? »
répondable sans émulateur. `Pom1BenchHost` n'est plus qu'un exécutant. Déplacement à
comportement identique (101 tests verts), avec un gain de conception au passage :
les trois issues d'un basculement (`Armed` / `Cleared` / `NoCode`) sont désormais
**distinctes**, là où l'hôte renvoyait `-1` pour deux d'entre elles et laissait
l'appelant redevenir devin par comparaison avant/après.

`bench_debug_session_smoke` exécute enfin les scénarios qui n'étaient que raisonnés :
la boucle Verify → armer → Run avec ré-armement à l'adresse **déplacée**, la fenêtre
Debug qui efface ou déplace le point d'arrêt partagé sous nos pieds (jamais
revendiqué, jamais effacé, jamais ressuscité), le basculement depuis une ligne
différente qui *s'accroche* sur la ligne armée, et une reconstruction où la ligne
armée ne produit plus de code. **Vérifié par mutation** (ignorer la vérification de
propriété fait tomber le test). Ce dernier scénario a révélé un **écart
documentation/code** : `DEVBENCH.md` affirmait qu'un point d'arrêt dont la ligne perd
son code « retombe », alors que l'accrochage le fait **glisser** vers la ligne de
code suivante — la doc dit maintenant ce que le code fait, et où le lire (la trace
console et le marqueur nomment tous deux la ligne réellement atteinte).

### Added — chasse n°7 : le protocole de débogage épinglé sur le VRAI 6502 (`bench_debug_protocol_smoke`)

Septième passe, **méthode changée plutôt que lecture répétée** : les six
précédentes étaient statiques, et leurs deux trouvailles les plus fines — le ▶ mort
après un arrêt, et le fait que le ré-armement efface le latch dont ce correctif
dépendait — ne reposaient que sur mon raisonnement, parce que `Pom1BenchHost` exige
un MainWindow vivant et qu'aucun ctest ne peut l'atteindre. Ce qui **est**
atteignable, c'est le protocole en dessous : ligne → adresse → armement →
exécution → arrêt → reprise. Le nouveau test bâtit un vrai programme
(`ca65 -g` + `ld65 --dbgfile`), le charge dans une vraie `Memory`, et rejoue
exactement la séquence de l'hôte contre un `M6502` réel. Il épingle : qu'une
adresse tirée d'une **ligne source** est bien une adresse par où le PC passe
réellement (l'assertion qu'aucune lecture statique ne peut faire — un parseur peut
être parfaitement cohérent et livrer une adresse jamais exécutée, ce que faisait
exactement le défaut des lignes de données) ; que l'arrêt précède le premier octet
de la ligne et retombe sur cette même ligne ; **que la reprise naïve re-piège sans
avancer** (le défaut « ▶ mort », maintenant démontré, plus supposé) ; que le
step-then-run avance et se ré-arme au tour de boucle suivant ; **que
`setBreakpoint()` efface le latch `tripped`** — la raison pour laquelle la
condition du ▶ ne peut pas s'écrire en termes de `isBreakpointTripped()` ; et qu'une
ligne de données n'a pas d'adresse à armer. Vérifié par mutation (retirer le filtre
des spans typés fait tomber le test). Aucun nouveau défaut trouvé : les six passes
précédentes tiennent à l'exécution.

### Fixed — chasse n°6 sur le débogage source : deux fragilités, aucun bug visible

Passe à **rendement décroissant, et c'est l'information utile** : aucun défaut
observable par l'utilisateur n'a été trouvé cette fois — seulement deux fragilités
introduites par les passes précédentes. **(1) Dépendance temporelle au fichier
`.dbg`** : le re-parse tardif de la passe n°4 relisait `/tmp/pom1_bench.dbg`
**depuis le disque** longtemps après le link (le temps d'un changement de preset,
de branchements de cartes, d'un flash de ROM), sur un chemin à **nom fixe** dans le
répertoire temporaire partagé — une seconde instance de POM1 compilant au même
moment pouvait l'avoir remplacé ou balayé entre-temps. Le texte est désormais lu
**une fois**, juste après le link quand le fichier vient d'être écrit, et conservé :
les re-parse ultérieurs n'ont plus de fichier à trouver. **(2) Non-déterminisme du
choix de fichier** : `parseDbgFile` prenait le premier enregistrement satisfaisant,
or `files` est une `unordered_map` — avec deux enregistrements partageant un
basename et aucune correspondance exacte (possible dès qu'un projet tire des
`EXTRA_ASM` de plusieurs répertoires), la table de lignes retenue pouvait différer
d'une exécution à l'autre sur la même machine. Deux passes explicites désormais :
correspondance exacte d'abord, basename ensuite, **id le plus petit** dans chaque
cas. Épinglé par `dbgfile_smoke` (ids listés à l'envers, plus une correspondance
exacte ajoutée après les basenames) et **vérifié par mutation** — inverser le
départage fait bien tomber le test.

### Fixed — chasse n°5 sur le débogage source : deux régressions composées par les correctifs précédents

Cette passe ne visait plus la fonctionnalité mais **l'interaction entre les
correctifs des passes 1 à 4** — ce qui reste quand les défauts simples sont morts.
**(1) ▶ de nouveau mort, dans un cas plus étroit** : le correctif n°1 conditionnait
son « step-then-run » à `isCpuBreakpointTripped()`, or le ré-armement de la passe
n°3 appelle `setCpuBreakpoint`, **qui remet le latch `breakpointTripped` à zéro**
(`M6502::setBreakpoint`). Reconstruire pendant que le CPU est garé sur le
breakpoint effaçait donc la preuve dont dépendait le contournement, et ▶ se
re-piégeait instantanément. La condition devient factuelle et non historique :
« CPU arrêté ET PC == adresse armée » — le latch ne dit pas ce que fera la
prochaine reprise. **(2) Revendication d'onglet trop large** : `dbgDocUid_` était
posé dans `applyResult`, qui traite aussi des résultats **qui ne sont pas des
builds** — `prepareTargetWithStarter` y passe le retour de `selectTargetExplicit`.
Avec une table encore vivante d'un build antérieur, cet appel offrait les
décorations à l'onglet actif quel qu'il soit. La revendication sort dans
`claimDbgDoc()`, appelée par les seuls sites de build (Verify, Run, et la fin de
build asynchrone WASM) — les seuls dont la table décrive vraiment l'onglet courant.

### Fixed — chasse n°4 sur le débogage source : la table survivait aux changements de machine

La table de lignes (`dbgInfo_`) n'était invalidée que par `build()` — or la machine
peut être reprogrammée **sans build** : le sélecteur Mode prépare un interpréteur
BASIC/LOGO en appelant `injectBasic`/`injectLogo` **directement** (ROM + hard
reset), et ce **parfois sur le même preset** — la cible Applesoft-GEN2 partage
celui du bench GEN2 asm, donc ni le wipe de `build()` ni un changement de preset ne
s'interposaient. Les décorations restaient vivantes sur un texte inchangé pendant
que la machine faisait tourner tout autre chose : armer un breakpoint piquait une
adresse dans l'interpréteur. Corrigé par des invalidations **aux points de
reprogrammation eux-mêmes** : `applyTargetPreset` quand il applique réellement
(`applyMachineConfig` — les Runs répétés sur le même preset gardent la table, et
l'ouverture d'un sketch *neutre* qui ne change pas la machine aussi), et en tête
des deux injecteurs. Effet de bord traité : le chemin Run de `build()` appelle
`onTargetSelected` APRÈS le link — le wipe aurait tué la table fraîchement parsée.
`adoptDbgInfo` est donc hissé à portée fonction et rendu **idempotent** (no-op si
la table est déjà adoptée, re-parse après un wipe), et les trois chemins Run
ré-adoptent après leur préparation machine, avant le ré-armement ; un `.dbg`
résiduel d'un build précédent est supprimé en début de branche asm par précaution.
La limite déjà documentée demeure : un File → Load hors Bench reprogramme la
machine sans que le Bench le sache — la table décrit « le dernier build ».

### Fixed — chasse n°3 sur le débogage source : le breakpoint ne survivait à aucun rebuild

Le plus grave des trois passes, trouvé en suivant le cycle de vie du breakpoint à
travers `build()` : le flux naturel de débogage — **Verify, armer un breakpoint,
Run** — le désarmait silencieusement et le programme filait sans s'arrêter. Deux
mécanismes s'additionnaient : le nettoyage en tête de `build()` (chasse n°1)
effaçait notre breakpoint sans le ré-armer, et de toute façon **tout chemin Run
repasse par un reset qui l'efface** — `loadBinary()` « resets + runs », le chemin
CodeTank fait un `hardReset(false)`, et `M6502::reset()` nettoie
`breakpointActive` (comportement voulu pour les changements de preset). Même un
simple re-Verify perdait le breakpoint. Correction : la ligne possédée est
mémorisée en tête de build (seulement si le breakpoint CPU est bien LE NÔTRE — un
breakpoint posé par la fenêtre Debug n'est jamais ni effacé ni écrasé), puis
ré-armée contre la table fraîche **après le dernier reset de chaque chemin** :
immédiatement pour Verify (aucun état machine touché), après `loadBinary` pour les
chemins asm et dual-bank (le CPU tourne déjà — un breakpoint dans les toutes
premières instructions peut manquer sa première passe, les boucles l'attrapent à
la suivante), et entre le `hardReset` et le `4000R` différé pour CodeTank — la
fenêtre idéale, le programme n'a pas démarré. La console trace le ré-armement
(« breakpoint re-armed at line N ($XXXX) ») ; une ligne qui ne produit plus de
code laisse le breakpoint retombé, visiblement (le marqueur suit
`breakpointLine()`). Non testable en ctest (exige `Pom1BenchHost` + MainWindow —
le trou UI connu) ; vérifié par lecture des quatre chemins et build vert.

### Fixed — chasse n°2 sur le débogage source : l'invariant « dirty » était le mauvais, et les macros

Trois défauts de plus, dont deux sur le même invariant. **(1) Faux négatif** : les
décorations exigeaient un buffer non-dirty, or `dirty` compare le buffer au fichier
SAUVÉ — un Verify sur un buffer édité-mais-non-sauvé produit une table de lignes
parfaitement valide pour ce texte exact, et le bouton breakpoint restait pourtant
gris. **(2) Faux positif, l'inverse** : `loadExample` et le chargement d'un starter
remplacent le texte EN PLACE en remettant `dirty=false` — les décorations du build
précédent auraient décoré le nouveau texte. Et le crochet `IsTextChanged()` ne
pouvait pas les voir : `TextEditor::Render` efface le flag EN ENTRÉE, donc un
`SetText` hors rendu est invisible au contrôle post-rendu. L'invariant devient
« le texte de CET onglet n'a pas changé depuis le build » : `dbgDocUid_` est posé au
retour du build et retiré sur toute édition (crochet) comme sur tout `SetText`
programmatique (explicitement, aux deux sites) ; le flag `dirty` ne joue plus aucun
rôle, et le suffixe « (line N) » du Step est gaté pareil. **(3) Macros** : mesuré
sur ld65 réel, chaque expansion émet des line records `type=2` pour les lignes du
CORPS de la `.macro`, spans sur les octets de l'expansion — selon l'ordre des
records le PC-follow pouvait sauter dans la définition, et un clic dans le corps
armait une expansion arbitraire. Le parseur ignore les records `type=2` : les
octets d'une expansion appartiennent à la ligne d'INVOCATION (le `type=1` — source
C — est conservé pour la phase C à venir). Vérifiés sains au passage :
`setCpuBreakpoint` prend bien `stateMutex` (pas de course UI/thread émulation),
`newDoc`/Save/Open ne peuvent pas produire de table périmée (uid neuf ou texte
inchangé), et un Ctrl+Z revenant au texte bâti reste invalidé — conservateur mais
jamais menteur. Miniature et fixture réel (macro `PAD` + table `.byte`) étendus
dans `dbgfile_smoke` / `bench_cc65_smoke`, le record `type=2` placé AVANT
l'invocation pour prouver que l'exclusion ne doit rien à l'ordre.

### Fixed — chasse aux bugs sur le débogage source du DevBench

Deux vrais bugs trouvés par relecture adversariale + expérimentation sur la vraie
sortie ld65, le jour même de la livraison. **(1) ▶ mort après un breakpoint** :
`M6502::run` teste le breakpoint en TÊTE de boucle, donc reprendre avec PC garé
sur l'adresse armée re-déclenchait avant d'exécuter une seule instruction — la
fenêtre Debug le savait (son Continue fait `stepCpu(); startCpu();`), le ▶ du
Bench non. `Pom1BenchHost::cpuRun` applique désormais la même recette quand le
trip est latché et PC == breakpoint. **(2) breakpoint sur une ligne de données** :
mesuré sur ld65 réel, les lignes `.byte`/`.asciiz` émettent bien des line records
(spans porteurs d'un attribut `type=` — les spans d'instructions n'en ont jamais),
donc un clic dessus armait une adresse jamais exécutée : un breakpoint qui ne
déclenche **jamais**, silencieusement. Le parseur exclut les spans typés des deux
tables — l'accrochage saute les lignes de données comme les commentaires. Angle
mort assumé et documenté : `.res` émet un span **sans** `type=`, indiscernable du
code. Plus une garde (span de taille 0 jamais breakpoint-able — jamais observé de
ld65, les lignes à étiquette seule n'émettent pas de span du tout). Vérifiés sains
au passage : indexation 1-based de `TextEditor::mBreakpoints`, resynchronisation
par frame du miroir `cpuRunning` quand le CPU se gare seul, fraîcheur du PC après
Step (`stepCpu` recopie le snapshot avant de retourner), gardes `#if` WASM autour
des éditions de `build()`. `dbgfile_smoke` (miniature) et `bench_cc65_smoke`
(fixture réel avec table `.byte`) épinglent les deux corrections.

### Added — débogage au niveau source dans le DevBench (asm, desktop)

L'écart le plus net entre ce que POM1 avait et ce qu'il pouvait être : le Bench
compilait avec la vraie chaîne cc65, exécutait sur le vrai 6502, puis laissait
l'utilisateur seul avec des adresses hexadécimales. Toutes les pièces existaient —
il manquait la correspondance ligne ↔ adresse et le câblage. Le chemin asm assemble
désormais avec **`ca65 -g`** et lie avec **`ld65 --dbgfile`** (simple et dual-bank),
et le nouveau module pur **`src/DbgFile.cpp`** parse le format v2 (records
file/seg/span/line/sym) en deux tables : adresse → ligne source et ligne → adresse,
avec accrochage vers l'avant (cliquer un commentaire arme la ligne de code
suivante). Dans le Bench : un bouton **point d'arrêt** (● rouge) arme/désarme
l'unique breakpoint CPU de la machine à la ligne du curseur (marqueur dans la
gouttière, statut « Breakpoint armed at line N ») ; **Step** annonce la ligne
atteinte et le curseur de l'éditeur **suit le PC** tant que le CPU est arrêté ; les
**étiquettes du programme** (records `sym type=lab`, y compris celles des modules
EXTRA_ASM) sont versées d'office dans le `SymbolTable` du désassembleur — plus de
`.lbl` VICE à charger à la main. Un buffer édité ou un autre onglet invalide la
table (les décorations disparaissent jusqu'au prochain Verify/Run) ; un rebuild
retire le breakpoint ligne qu'il rend caduc, sans toucher un breakpoint posé par la
fenêtre Debug. Le seam `IBenchHost` gagne quatre virtuals à défaut neutre — le
module portable reste ignorant de cc65. Épinglé par **`dbgfile_smoke`** (miniature
manuscrite : tables, accrochage, refus) et **`bench_cc65_smoke`** (cc65-gated :
parse la VRAIE sortie `ld65 --dbgfile` d'un fixture `ca65 -g` — et premier test de
`Pom1BenchCc65.cpp`, dont la moitié de l'assertion est qu'il linke sans UI, part A
sur les micro-parseurs Makefile/.sketch.json/cfg et les specs C embarquées).
Restent ouverts (TODO) : cibles C, WASM, points d'arrêt multiples.

### Changed — `pic/` sort du préchargement WASM : le premier pixel passe de 14 à ~7 Mo

`pic/` pesait **6,9 des 14,0 Mo gzip** que chaque visiteur téléchargeait — la moitié
du bundle, pour des photos de *Aide → Photos* que la plupart n'ouvrent jamais (dont
2,5 Mo de fichiers que **rien ne référençait** : `IMG_6708.png`,
`applesoft-iia-400x259.jpg`). Seuls `pic/icon.png` et le logo du magnéto (visibles
au boot) restent dans `POM1.data` ; tout le reste arrive **en HTTP à l'ouverture de
la fenêtre** : `ensurePicFetched()` (`src/MainWindow_Dialogs.cpp`) tri-état
Ready/Pending/Failed — Pending ne pose pas `loadTried`, donc le `ensure*Texture`
par-frame réessaie jusqu'à l'atterrissage du fichier en MEMFS (écrit par le
callback `emscripten_async_wget_data`, thread principal, URL relative
percent-encodée — `SWTPC PR-40 Printer.png` porte des espaces) ; Failed retombe
dans la branche « not found » existante ; sur desktop tout compile en « toujours
Ready », zéro changement. Les fenêtres affichent « Downloading… » pendant le vol.
`tools/assemble_wasm_site.sh` publie désormais `pic/` en entier à côté de la page —
un fichier jamais référencé n'est plus téléchargé par personne. Le préchargement
CMake devient une **allow-list assumée** (contrairement aux deny-lists de
`sketchs/`/`dev/`) : une photo oubliée de la liste marche quand même en HTTP, elle
n'est juste pas payée par tous les visiteurs.

### Added — essai à blanc hebdomadaire du packaging

`release.yml` ne tournait que sur tag (ou dispatch manuel), donc une casse de
packaging se découvrait **pendant** la publication — la cicatrice du dylib Homebrew
à chemin absolu embarqué dans le `.dmg`, mort au dyld sur toute machine Apple
Silicon. Un déclencheur `schedule:` (lundi 04:43 UTC) bâtit désormais les quatre
paquets chaque semaine sans rien publier : le pipeline était déjà structuré pour —
le job `publish` est gardé par `ref_type == 'tag'` et la version retombe sur le
fichier `VERSION` — il ne manquait que le déclencheur. Une image de runner mise à
jour, une baseline vcpkg, un hoquet winget ou une casse de notre code arrivent en
rouge un lundi matin au lieu du jour de la release.

### Added — smoke navigateur sur la page publiée

`Deploy Pages` téléversait sans jamais charger la page : une erreur JS, un échec
d'init WebGL2 ou un `POM1.data` tronqué se déployaient au vert — entre « le build
passe » et « le site marche », il n'y avait rien. `tools/wasm_smoke.mjs` (Playwright,
Chromium headless + SwiftShader) sert l'assemblage du site en HTTP local, charge
`POM1.html` et assère trois choses : le boot va au bout (`pom1FirstFrameReady`, le
rappel C++ posé après le premier `glfwSwapBuffers` — plus fort que
`onRuntimeInitialized`, l'écran Apple-1 est réellement peint, observé via
`#bootSplash.hidden`) ; rien n'a cassé en chemin (zéro exception non rattrapée, zéro
erreur console, zéro requête échouée, overlay `#err` resté caché) ; et le canvas
n'est pas noir (capture compositeur relue en 2D — le contexte WebGL sans
`preserveDrawingBuffer` relit noir entre deux frames). Un mode `--self-test` prouve
que le vérificateur sait ÉCHOUER (canvas noir, throw, overlay d'abort) et que le
runner a encore WebGL2 — même philosophie que `lock_order_smoke` : un contrôle qui
ne tire jamais se lit comme de la couverture sans en fournir. L'assemblage du site
sort de `pages.yml` vers `tools/assemble_wasm_site.sh`, partagé avec le job `wasm`
de `ci.yml` : le smoke tourne **à chaque push, avant le merge**, et une seconde fois
dans `pages.yml` en **porte du déploiement** — sur exactement les fichiers servis.

`runEmulationSlice` sérialisait **et** encodait en delta ~80 Ko quatre fois par
seconde en tenant `stateMutex` — un pic de latence périodique sur tout appel UI qui
voulait le verrou, de la même famille que le défaut `CassetteDevice` d'août : ni
`LockOrder.h` ni TSan ne le voient, seule la lecture du chemin d'appel le trouve.
La capture est maintenant en deux temps : la copie des pages (`saveSnapshotToBuffer`,
le seul pas qui ait besoin que la machine ne bouge pas) reste sous `stateMutex` ; le
verrou est relâché ; l'encodage delta + l'éviction (la moitié coûteuse, qui ne touche
que le tampon) tournent sous un nouveau **`rewindMutex`** de rang 25 — entre `State`
(30) et `Keyboard` (20), pris à l'intérieur de `stateMutex` par les chemins UI
(seek/resume/clear). Un **compteur de génération** bumpé par toute édition de la
timeline (enable/clear/troncature « resume here ») retire un blob copié avant
l'édition et encodé après : il ne sera jamais ajouté comme trame « future » derrière
la nouvelle tête. `lock_order_smoke` épingle les trois nestings réels et l'inversion
`rewind → state` (5 inversions attrapées). Non mesuré au chronomètre — la propriété
est structurelle : ce qui reste sous `stateMutex` est une copie mémoire, plus un diff.

### Changed — l'enregistrement du rewind est désactivé par défaut

La bande timeline de la barre d'outils armait l'enregistrement au premier rendu
(`rewindAutoStarted`), donc chaque utilisateur payait la capture — ~80 Ko sérialisés
quatre fois par seconde **sous `stateMutex`** (`TODO.md`, « Capture du rewind sous
`stateMutex` ») — pour une fonction que peu ouvrent. La bande affiche désormais
`timeline (off)` et CPU → State Rewind → « Enable rewind recording » l'active ; rien
d'autre ne change (`rewind_buffer_smoke` vert).

### Added — matrice headless des 13 presets (`headless_preset_matrix`)

> **Mesuré** : 13/13 presets bootent et se garent dans la boucle clavier du Woz Monitor
> (`PC=$FF29`) en **3,1 s** pour toute la matrice. `ctest` 96 → 97.

Le mode `--headless` existait, complet (presets, cartes, verbes différés), et **aucun
test ne l'appelait** : 46 des 109 TU — la moitié du code — n'entraient dans aucun
binaire de test, et le fan-out « ajouter une carte » (profil RAM, cavaliers, cascades
TMS9918→CodeTank / ACI→ACI étendue / microSD→IEC, évictions Parmigiani, reset et
enregistrement bus de chaque carte) ne cassait que chez un utilisateur. Il manquait une
seule brique : le mode headless n'avait d'issue qu'un signal ou un `--dump-*-frame`.

- **`--exit-after-cycles <N>`** (`CliDispatcher` + `runHeadless`) : après les verbes
  différés, exécute exactement N cycles émulés sur le thread appelant, journalise
  `headless run complete — N cycles, PC=$xxxx` et sort 0. Implique `--headless` ;
  avec `--paste-at-cycle` il devient le budget de rejeu. Documenté dans `doc/CLI.md`
  (`cli_flags_sync` + `doc_paths_sync` verts).
- **`tools/test_headless_presets.py`** : lit `--list-presets` (jamais de compte en
  dur), boote chaque preset avec 2 M cycles, exige sortie 0, aucune ligne `ERROR`, et
  **le CPU dans `$FF00-$FFFF`** — le Monitor a atteint son prompt, ce qui prouve que
  la ROM, la RAM et chaque carte branchée ont survécu au boot. Skip 77 sans binaire.

### Changed — `-Werror` sur le job macOS

L'arbre mesuré sous AppleClang 17 donnait **39 avertissements sur 10 sites**, tous
du code mort ou presque : deux constantes et un rectangle du magnétophone jamais
dessinés, les trois helpers CAM16-UCS de `HgrConvert` que le commentaire disait
utilisés par la marche de scoring (elle travaille en RGB linéaire), une capture de
lambda sur des `constexpr`, deux compteurs incrémentés et jamais lus dans les tests
(`cyclesSinceProgress`, `shown`), `totalNatural` dans la barre mémoire, et les 27
champs GL de `CrtEffectStack` que le stub Metal ne touche pas par conception — ce
dernier traité par un pragma **avant** l'include, pas par un `#if POM1_HAS_METAL`
dans l'en-tête : la définition est à portée de cible, et une classe dont la
disposition en dépend est exactement la divergence que le bloc `pom1_build_flags`
documente. `kAboutPhotoFile` redevient la source unique du chemin de la photo About
(quatre littéraux dupliqués le contournaient). `ci.yml` → `macos` passe
`-DPOM1_WERROR=ON` ; Windows reste à mesurer (`TODO.md`).

### Changed — le registre de panneaux devient la liste unique des 68 fenêtres

> **Vérifié par A/B à l'exécution**, la seule preuve disponible ici : POM1 lancé 20 s
> sous Xvfb sur le preset *Fantasy* (toutes cartes branchées), avant et après, à partir
> du même `ini/`. **49 fenêtres soumises et 58 états ouvert/fermé identiques**, et la
> **partition de dock identique** sur un second A/B parti d'un `ini/` vide (ce qui force
> `buildDefaultDockLayout()`). Plus une équivalence statique du dispatch contre `HEAD` :
> 51 triplets (flag, fonction, garde), aucun perdu, aucun apparu. `ctest` 95 → 96.

`MainWindow_ImGui` fait **17 135 lignes réparties sur 16 TU** — plus que le
`MainWindow.cpp` de POM2 dont il a été scindé par imitation. Les passes de scission
successives étaient du *code motion* pur, ce que `CLAUDE.md` assume explicitement :
le couplage n'a jamais bougé. Le vrai symptôme n'était pas la taille des fichiers mais
que les **68 fenêtres étaient récitées à la main dans quatre endroits** — la table de
persistance, 51 lignes `if (showX) renderX();`, les bascules éparpillées dans huit
menus, et les 26 lignes de `kDockLayout[]`. Quatre listes sur un même ensemble : c'est
ainsi qu'une fenêtre finit ouvrable mais non sauvegardée, ou sauvegardée et absente des
menus, chaque oubli étant silencieux.

**La table existait déjà** — `WindowDescriptor { key, title, show, kind, persistPresence }`,
68 lignes couvrant les 68 flags, zéro orphelin de part et d'autre — mais elle n'avait
qu'**un seul consommateur** : la persistance ini. Elle porte désormais aussi `render`
(pointeur sur méthode), `gate` (le flag *carte branchée* du `if (jukeBoxEnabled && …)`,
gardé distinct de `show` : fermer un panneau ne doit pas débrancher la carte, et
débrancher ne doit pas oublier que le panneau était ouvert), `desktopOnly` (l'ancien
`#if !POM1_IS_WASM` autour d'un site d'appel) et `dock` (la place d'usine).

Ce qui disparaît : les **51 lignes de dispatch** (une boucle), les **26 lignes de
`kDockLayout[]`** (le slot est dans la ligne ; il reste une table résiduelle de **deux**
entrées, pour les deux fenêtres dockées sans ligne de registre — « Apple 1 Screen »,
toujours dessinée sans flag, et « Memory Search », qui appartient à `MemoryViewer_ImGui`).
Les 17 fenêtres à `render == nullptr` gardent leur bloc dédié : elles ont besoin d'un
état d'entourage (géométrie tirée de `DisplaySize`, carte auto-branchée à l'ouverture,
branche `else` à la fermeture) qu'un appel de méthode nu ne porte pas.

L'ordre de soumission suit désormais l'ordre du registre. C'est sans effet : Dear ImGui
tire l'ordre de profondeur de sa liste de focus, pas de l'ordre des `Begin()`, et chaque
bloc dédié garde son `SetNextWindowSize`/`applyPendingLayout` juste avant son `Begin()`.
La seule règle d'ordre porteuse — `renderDockSpace()` avant le premier `Begin()` dockable —
est intacte.

### Added — un menu *Windows*, généré, et `window_registry_sync` pour le tenir

POM1 n'avait **pas de menu Fenêtres** : les bascules vivaient dans huit autres menus et
couvraient 47 des 68 fenêtres. Les 21 restantes n'avaient aucune entrée de menu — dont
**les neuf panneaux de cartes** (GEN2 HGR, TMS9918, GT-6144, IEC, Wi-Fi Modem, Terminal
Card, PR-40, A1-IO & RTC, Juke-Box) et la console de débogage : une fois le panneau
fermé, aucun menu ne permettait de le rouvrir. Le menu est une boucle sur le registre,
groupée par `kind`, avec le `gate` **montré et non imposé** (un panneau de carte
débranchée se bascule quand même — le flag est l'intention de l'utilisateur, et il est
mémorisé).

Les entrées contextuelles existantes sont **délibérément laissées en place**. Ce n'est
pas une liste parallèle à replier mécaniquement : *Help ▸ Tutorials* porte des libellés
éditoriaux (« Integer BASIC: write your first program », pas « Tutorial: Integer BASIC »)
et un regroupement thématique par séparateurs qu'aucun champ `kind` n'encode. Les
générer échangerait de la curation contre de l'uniformité. Ce qui change est leur statut :
elles deviennent un raccourci curaté, plus la seule porte d'entrée.

**`tools/check_window_registry.py`** (ctest `window_registry_sync`) est le premier
contrôle de quelque nature que ce soit à atteindre `MainWindow` — la plus grosse couche
de POM1 est exclue de tous les binaires de test, donc un script qui lit les sources n'est
pas un pis-aller, c'est le seul outil disponible. Il assère que chaque flag `show*` a une
ligne, que chaque ligne peut réellement dessiner (un `render` ou un bloc dédié nommé), et
que la ligne `if (showX) renderX();` **ne revient pas** — la cinquième liste qui
recommence. Vérifié falsifiable : 3 dérives semées, 3 détectées.


### Changed — un seul jeu de macros pour l'app, `pom1_core` et les 50 cibles de test

> **Vérifié par sonde.** Une macro témoin ajoutée temporairement à la cible
> `INTERFACE` puis relue dans `compile_commands.json` : **115 TU de l'app + 227 TU
> de tests** la reçoivent, 19 des 20 TU vendored ne la reçoivent pas — le vendored
> reste hors périmètre exactement comme avant. `ctest` → 95/95.

`NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_CRT_SECURE_NO_WARNINGS` et `/utf-8` étaient
posés par `target_compile_definitions(${PROJECT_NAME} …)`, donc sur la **cible POM1
seule** : `pom1_core` et les 50 cibles de test compilaient les mêmes `.cpp` sous un
autre jeu de macros. Le symptôme était déjà visible dans les sources — **six fichiers**
portaient un préambule `#ifndef NOMINMAX` écrit à la main (`RewindBuffer.cpp`,
`SocketHandle.h`, `AudioDevice.cpp`, `main_imgui.cpp`, `MainWindow_Dialogs.cpp`,
`NativeFileDialog.cpp`), six fichiers ayant payé la même leçon séparément. C'est la
forme d'un défaut de système de build, pas d'un défaut de source.

La divergence a cessé d'être théorique le 22 août 2026, quand `ctest` s'est mis à
tourner sous Windows : un jeu de macros qui diffère entre l'app et ses tests devient
une source de rouge en CI qui ne se reproduit pas sur Linux.

Une bibliothèque `INTERFACE` **`pom1_build_flags`** porte désormais ces quatre
réglages ; l'app, `basicc` et — par `link_libraries()` à portée répertoire —
`pom1_core` et les 50 cibles de test la lient. Le correctif ne peut pas pourrir :
une nouvelle cible reçoit les flags ou il lui manque une ligne visible.

Corollaire du même défaut : `GL_SILENCE_DEPRECATION` était recopié **50 fois** dans
`tests/CMakeLists.txt`, un bloc `if(APPLE)` de quatre lignes par cible. Il est énoncé
une fois, à portée répertoire (2 200 → **2 046 lignes**). La copie de l'app reste
par-cible et conditionnelle : elle n'y vaut que pour le chemin macOS-OpenGL hérité.

### Changed — `Memory.h` ne tire plus les onze en-têtes de cartes

> **Mesuré avant/après**, `touch` puis reconstruction complète sur 16 cœurs.
> `ctest` → 95/95, zéro avertissement.

`Memory` possède tous les périphériques, donc `Memory.h` est l'endroit naturel où un
en-tête de carte finit par atterrir — et onze l'avaient fait. Le fichier le plus
largement inclus du projet était devenu propriétaire transitif de toute la flotte :

| `touch src/…` | avant | après |
|---|---|---|
| `CassetteDevice.h` | 105 TU | **20 TU** |
| `D64Image.h` | 105 TU | **23 TU** |
| `MicroSD.h` | 105 TU | **24 TU** |
| `JukeBox.h` | **105 TU / 5 min 21 s CPU** | **57 TU / 20 s** |

Rien de tout cela n'était structurel. `std::unique_ptr<T>` n'exige `T` complet qu'à
l'instanciation du deleter, c'est-à-dire au destructeur — et `~Memory()` est
hors-ligne dans `Memory.cpp` depuis longtemps. Les getters *inline* d'une ligne ne
l'exigent pas davantage : lier une référence à une lvalue de type incomplet est
bien formé, ce que `TMS9918` et cinq autres cartes prouvaient déjà en étant
forward-déclarées **avec** leurs getters inline. Les onze includes étaient de la
pure accrétion.

`JukeBox` et `CodeTank` étaient les deux derniers verrous, pour une raison qui ne se
contourne pas par déclaration anticipée : ils publiaient des types **imbriqués**
(`JukeBox::Jumper`, `JukeBox::ChipMode`, `CodeTank::Jumper`) présents dans des
signatures de `Memory`. Ces trois enums vivent désormais à portée namespace dans un
nouveau **`src/CardTypes.h`** — un en-tête sans dépendances. Chaque carte conserve un
alias membre (`using Jumper = pom1::JukeBoxJumper;`), donc les **145 sites** qui
écrivent `JukeBox::Jumper` n'ont pas changé d'un caractère.

Rayon d'impact total : **7 TU** ont dû nommer un en-tête qu'ils utilisaient déjà
(4 dans `src/`, 3 tests) — c'est le bon sens de la dépendance. Quatre getters faisant
un accès membre (`jukeBox->getJumper()`) sont passés hors-ligne : eux exigent
réellement le type complet. `Peripheral.h`, qui arrivait par ricochet via `JukeBox.h`,
est maintenant nommé.

Reste ouvert (→ `TODO.md`) : `JukeBox.h` / `CodeTank.h` s'arrêtent à 57 TU et non ~20,
retenus par `EmulationSnapshot.h` qui a besoin de `JukeBox::Snapshot` — des structs
imbriqués par valeur, le même blocage un cran plus haut.

### Added — `crt_params_sync` : la pile CRT n'est plus le seul sous-système sans test

> **Vérifié par mutation.** Quatre dérives semées une par une — bouton ajouté à
> `CrtParams` seul, knob retiré du shader MSL, uniform GLSL déclaré mais plus
> alimenté, les deux structs MSL désaccordées — **quatre détectées**, arbre restauré
> vert. Un garde qui ne se déclenche jamais se lit comme de la couverture sans en
> fournir.

Douze flottants et un enum pilotent le post-traitement CRT, en **trois copies tenues
à la main** : `CrtParams.h`, le GLSL de `CrtEffectStack.cpp` (`uBrightness`…) et le MSL
de `CrtEffectStackMetal.mm` (`brightness`…). `CLAUDE.md` énonçait la règle noir sur
blanc — « un bouton ajouté à l'un doit l'être à l'autre ou macOS diverge
silencieusement » — et rien ne la vérifiait. Le mode de défaillance est par
construction invisible sur les machines CI Linux et Windows : seul un Mac exécute le
chemin MSL.

`tools/check_crt_params.py` (cinquième garde de la famille `version_sync` /
`imgui_pin_sync` / `doc_paths_sync` / `cli_flags_sync`) vérifie les 13 réglages dans
les deux sens, plus le cas « déclaré mais jamais téléversé » — un uniform présent que
personne n'alimente étant exactement la divergence silencieuse visée. Les noms ne se
correspondent pas un à un (`shadowMaskStrength` → `uShadowStrength` →
`shadowStrength`) : la table de correspondance du script est la documentation qui
manquait autant qu'elle est le contrôle.

### Fixed — deux valeurs restaurées d'un snapshot sans repasser par leur borne

> `ctest` → 95/95. Défauts trouvés par balayage systématique des sites de
> désérialisation, pas par un rapport d'utilisateur.

**`PR40Printer::deserialize`** était le dernier `static_cast<Enum>(r.readU8())` du
dépôt sans contrôle de plage. Les cinq autres sites valident tous
(`MicroSD` ×2, `IECCard` ×2, `JukeBox`) : un enum de classe dont les énumérateurs
couvrent 0..2 n'a pas de valeur 3, donc y couler un octet corrompu est un
comportement indéfini. Aligné sur la forme des autres.

**`MicroSD::deserialize`** restaurait `writeExpectedLen` en `u16` brut, alors que le
chemin vivant refuse tout ce qui dépasse `MAX_WRITE_SIZE` (32 Ko) avec
*« FILE TOO LARGE »*. Un snapshot corrompu pouvait donc reprendre un transfert avec
une cible de 64 Ko que la machine en marche n'aurait jamais acceptée — borné et sans
gravité, mais c'est un invariant de périphérique que le chargeur ne rétablissait pas.


### Changed — GitHub Pages sert un artefact bâti par la CI, plus une branche du dépôt

> **Vérifié sur le site en ligne.** Deux runs `Deploy Pages` verts, puis probe HTTP :
> `/pom1/` 200, `build-wasm/POM1.{html,js,wasm,data}` 200, `cc65/ca65.wasm` 200.

Le lien *Play in browser* du README était servi depuis la branche `main` elle-même,
ce qui obligeait à **committer** `POM1.data` / `POM1.wasm` / `POM1.js` — de pures
sorties de build, ~62 Mo par déploiement, que git conserve à jamais. 39 révisions du
seul `POM1.data` avaient porté `.git` à 813 Mo, et la courbe ne redescend pas.

*Settings → Pages → Source* est passé à **GitHub Actions** : `.github/workflows/pages.yml`
compile le bundle avec emsdk et le publie à la même URL, les trois sorties ne sont plus
versionnées (`.git` à 252 Mo), et `build-wasm/cc65/` reste committé — c'est une
dépendance de la DevBench en ligne, pas une sortie de build. Le workflow n'avait
jamais été exécuté : son premier run a été lu ligne à ligne avant de retirer quoi que
ce soit de git.

### Fixed — la passe avertissements cassait le WASM et Windows

> **Vérifié par exécution réelle.** Build WASM complet avec emsdk en local
> (`POM1.wasm` / `.js` / `.data` produits), `ctest` → 92/92, puis CI verte sur les
> trois plateformes de bureau et `Deploy Pages` vert.

`-Wall -Wextra` / `/W4` posés sur les sources de POM1 (août 2026) ont cassé les deux
cibles qu'aucun job par-push ne compile — chacune pour une raison que Linux ne peut
pas voir.

**`updateCpuExecution(float /*deltaTime*/)`** : le nom du paramètre avait été commenté
pour taire `-Wunused-parameter`, alors qu'il est lu dans la branche `#if POM1_IS_WASM`
deux lignes plus bas. Le natif ne compile pas cette branche, donc rien n'a bronché ;
emcc a rendu *« use of undeclared identifier 'deltaTime' »* et le job `Deploy Pages`
est mort à 29 %. Le nom revient, marqué `[[maybe_unused]]`.

**La propriété `COMPILE_OPTIONS` portant les drapeaux** était posée sur
`POM1_OWN_SOURCES`, qui contient `packaging/windows/POM1.rc`. `rc.exe` ne parle pas
`cl` : il a lu `/W4` comme une option inconnue et tué le build Windows sur
*« RC1106: invalid option: -4 »*. Les ressources (`.rc`, `.icns`) sont filtrées avant
la pose — ce ne sont pas des unités de traduction.

**`RewindBuffer.cpp` ne compilait plus sous MSVC hors cible principale.** `windows.h`
définit `min`/`max` en **macros** sans `NOMINMAX`, et ce TU appelle `std::min` deux
fois → `C2589`. La cible POM1 pose `NOMINMAX` globalement, mais pas les 50 cibles de
test : `POM1.exe` se construisait pendant que `test_rewind_buffer` échouait sur le
même fichier. Le garde va dans le TU qui tire l'en-tête, comme le fait déjà
`AudioDevice.cpp`. Au passage, `<algorithm>` manquait — `std::min` ne tenait que par
une inclusion transitive de libstdc++.


### Fixed — le thread audio n'alloue plus et n'attend plus le chargement d'une cassette

> **Vérifié par exécution réelle.** Build complet, 0 avertissement, `ctest` → 92/92.

Issu d'une revue d'architecture ciblée stabilité. Trois défauts qu'aucun job de
la CI ne pouvait voir, parce qu'aucun n'est une inversion d'ordre de verrou
(`LockOrder.h` y est aveugle) ni une course (TSan y est aveugle) : ce sont des
durées de détention et une allocation sur le thread temps-réel.

**`CassetteDevice::loadAudioStream` tenait `audioStreamMutex` pendant
`ma_decoder_init_file()`** — ouverture du fichier et détection du format — puis
pendant `ma_decoder_get_length_in_pcm_frames()`, qui peut parcourir un MP3
entier pour compter ses frames. Le callback audio réclame ce même verrou à
chaque période, ~2,7 ms à 128 frames / 48 kHz : insérer une cassette décrochait
le son, d'autant plus longtemps que le média est lent (la carte SD d'un Pi). Le
décodeur passe en `std::unique_ptr` — adresse stable obligatoire, la base
data-source de miniaudio gardant des pointeurs vers l'objet lui-même —
construit hors verrou puis échangé sous verrou court, l'ancien étant retiré une
fois le verrou relâché. `closeAudioStream` vole le pointeur sous verrou et fait
l'`uninit` dehors. Même discipline que le SID, qui la documentait déjà dans son
en-tête : c'était la référence à appliquer, pas à inventer.

**`AudioDevice::mixSources` appelait `tmpBuf.resize()` dans le callback.** Une
allocation sur le thread audio peut prendre un verrou d'allocateur et manquer
l'échéance du tampon. Le scratch est dimensionné une fois dans le constructeur
(`kMixScratchFrames`) et une demande plus grande est mixée par tranches au lieu
d'agrandir le tampon — les sources sont des flux à état, leur demander N frames
puis M revient exactement à leur en demander N+M.

**`~CassetteDevice()` était `= default`.** Une cassette encore montée à
l'extinction ne passait donc jamais par `ma_decoder_uninit` : les tampons du
backend et le descripteur de fichier fuyaient. Le job ASan nocturne tourne avec
`detect_leaks=1`.

### Added — un journal qui survit au processus

`Logger` n'avait que trois sinks : `StreamLogger` (cout/cerr), `RingBufferLogger`
(mémoire) et le `TeeLogger` qui les combine. **Aucun sink fichier.** Sur un
`.app` ou un `.exe` lancé au double-clic, stdout ne va nulle part — un rapport
de bug n'a donc rien à joindre, et c'est vrai d'un défaut ordinaire (« la carte
SID est muette »), pas seulement d'un plantage. La case TODO « filet de crash +
bundle de diagnostic » supposait un journal à empaqueter ; il n'en existait pas.

`FileLogger` devient le troisième enfant du Tee : une ligne horodatée à la
milliseconde par entrée, flush par entrée (les entrées qui comptent le plus sont
les dernières avant un gel ou un `kill`), rotation d'un cran à l'ouverture pour
garder la session précédente, et no-op silencieux si le fichier ne s'ouvre pas —
un sink capable de faire tomber l'application est pire que pas de sink.

Un défaut trouvé en le câblant : `initDefaultTeeLogger()` s'exécutait **avant**
`pom1_macos_provision_user_data_dir()`, qui fait le `chdir` vers
`~/Library/Application Support/POM1/`. Un lancement Finder aurait écrit son
journal là où le Finder se trouvait. Les deux sont réordonnés — le
provisionnement ne journalise rien, donc rien n'est perdu. Non installé sous
WASM, où le système de fichiers est virtuel et jeté au rechargement.

### Changed — le budget du rewind suit la RAM de la machine

`kDefaultMemoryBudgetBytes` était un `constexpr` de 128 Mo et `setMemoryBudget()`
n'était appelé par rien avec une valeur dérivée de la plateforme. La constante
précède la borne Raspberry Pi : sur un Pi 3 (1 Go), 128 Mo d'historique de scrub
disputent la machine à l'émulateur et au framebuffer. `defaultRewindBudgetBytes()`
prend ~1/16 de la RAM physique, clampé à [16 Mo, 128 Mo] — tout hôte ≥ 2 Go garde
exactement l'ancien comportement, et une sonde impossible (WASM, libc sans
`_SC_PHYS_PAGES`, échec) retombe sur le plafond historique plutôt que de réduire
en silence sur une machine qu'on n'a pas su mesurer.

### Changed — passe outillage : build des tests, avertissements, CI multiplateforme, sanitizers

> **Vérifié par exécution réelle.** Reconfiguration + build complet depuis zéro,
> puis `ctest` → **90/90**, 0 sauté. Build instrumenté `-fsanitize=address,undefined`
> reconstruit et rejoué séparément.

Issu d'un audit d'architecture transversal. Le cœur est sain — c'est
l'outillage autour qui coûtait cher tous les jours.

**Le build des tests recompilait le cœur 50 fois.** Aucun `add_library`
n'existait dans le projet : chaque cible de test listait les 24 sources de
`POM1_TEST_CORE_SOURCES` en dur, donc chacune les recompilait sous `-O3`. Un
build complet exécutait **1435 compilations, dont 1300 pour les tests et 1154
redondantes** (`TMS9918.cpp` compilé 53 fois). Elles passent par une
bibliothèque `OBJECT` `pom1_core` déclarée dans `tests/CMakeLists.txt` — donc
héritant du `-UNDEBUG` qui garde les `assert()` vivants — et
`POM1_TEST_CORE_SOURCES` s'étend désormais en `$<TARGET_OBJECTS:pom1_core>` :
**une seule édition, les 50 `add_executable` sont inchangés**. 1435 → **355**
compilations.

**81 000 lignes compilaient sans le moindre avertissement activé.** Ni `-Wall`,
ni `-Wextra`, ni `/W4` nulle part. Le coût d'entrée a été mesuré avant d'agir :
51 hits sur tout l'arbre, dont 24 venaient de `stb_image_write.h` (vendorisé).
Le reste, corrigé : trois locales mortes (`base` dans `parseAddr16`, le lambda
`rdword` du désassembleur, `barHeight`), deux helpers jamais appelés dans
`Drive1541`, trois tests tautologiques (`r.end <= 0xFFFF` sur un `uint16_t`),
un `kFile` inutilisé hors `_WIN32`, quinze `-Wmisleading-indentation` — dont
**un vrai défaut d'indentation dans `M6502::ADC`**, où le `if`/`else` du calcul
de l'overflow et le `setFlagCarry` qui suit étaient alignés de trois façons
différentes. Les drapeaux sont appliqués **par fichier source**, ImGui étant
compilé dans le même exécutable. `-DPOM1_WERROR=ON` en CI seulement.

**macOS et Windows n'étaient construits qu'au moment de publier.** `ci.yml`
n'avait qu'un job Linux ; les deux autres plateformes n'apparaissaient que dans
le workflow de release, déclenché sur tag — c'est-à-dire qu'une régression y
était découverte au pire moment. Ajout de deux jobs *build-only* (macOS Metal
**et** OpenGL, Windows/vcpkg static). Sans `-Werror` : l'arbre a été mesuré
propre sous GCC, pas sous AppleClang ni MSVC, et promettre un gate sur un jeu
d'avertissements jamais exécuté aurait livré une CI rouge dès le premier run.

**Sanitizers.** `-DPOM1_SANITIZE=address,undefined` (ou `=thread`) instrumente
toutes les cibles, force LTO à OFF, et alimente un job CI nocturne. La première
exécution locale a montré pourquoi il fallait le tester avant de le livrer :
`applesoft_gen2_smoke` et `applesoft_tms9918_smoke` dépassaient leur budget de
120 s sous ASan **sans rien trouver** — `tests/CMakeLists.txt` multiplie
maintenant chaque `TIMEOUT` déclaré par 5 sous sanitizer. Aucun rapport ASan ni
UBSan sur la suite.

**Un test validait un binaire qu'il n'avait pas construit.**
`tools/test_lib_micro.py` codait en dur `<repo>/build/POM1` et CMake ne lui
passait pas le binaire — alors que `test_gfx_regress.py` recevait déjà
`$<TARGET_FILE:pom1_imgui>` juste à côté. Depuis tout répertoire de build autre
que `build/`, il validait donc un binaire périmé, et **sautait silencieusement**
(code 77) quand `build/POM1` n'existait pas. Il reçoit `--pom1`, et l'absence du
binaire est une **erreur**, plus un skip : cc65 peut légitimement manquer sur
une machine, le binaire sous test non. Un dépassement de délai se distingue
maintenant d'une erreur de compilation dans la sortie (l'ancien
`except Exception` rendait les deux indiscernables).

### Added — l'ordre des mutex est vérifié, la doc aussi

**`src/LockOrder.h` + `lock_order_smoke`.** La règle
`stateMutex > keyboard.keyMutex > publisher.snapshotMutex` était répétée dans
huit fichiers et vérifiée dans aucun. Répéter un invariant montre qu'il compte ;
ça ne le maintient pas vrai. Chaque verrou porte un rang, un thread ne peut en
prendre qu'un strictement intérieur à ce qu'il détient déjà. `PriorityMutex`
porte le rang `State` ; `keyMutex` et `snapshotMutex` deviennent des
`pom1::RankedMutex<...>` (BasicLockable : les six sites de `lock_guard`
existants ne changent que d'argument de template). Compilé hors des binaires
livrés (`NDEBUG`), vivant dans les tests et en Debug, et de disposition mémoire
identique dans les deux cas. Le test **fork un enfant par inversion et récolte
`SIGABRT`** — un vérificateur qui ne se déclenche jamais se lit comme de la
couverture tout en n'en apportant aucune — plus un cas témoin, sans lequel un
vérificateur qui abortrait sur *tout* passerait chaque assertion.

**`tools/check_doc_paths.py` + `doc_paths_sync`.** Troisième gardien de la
famille `version_sync` / `imgui_pin_sync` : les 372 chemins de fichiers cités
entre backticks par les 26 000 lignes de markdown doivent exister. Résolution
selon les conventions réellement employées (racine, dossier du document, `src/`,
`dev/`, `dev/lib/`) ; `CHANGELOG.md` exclu en entier, parce qu'un changelog qui
nomme un fichier depuis renommé fait exactement son travail. Trois dérives
réelles corrigées au passage (`demos_menu/`, `tms9918m1/m2.asm`,
`gt6144_hello/`). Remplace une corvée manuelle — le git log en contient déjà une
passe de 20 corrections à la main.

### Changed — `pic/` divisé par deux (14 Mo → 7,0 Mo)

Sept PNG portaient un canal alpha entièrement opaque, soit un quart d'octets
constants ; POM1 charge de toute façon en RGBA forcé (`stbi_load(..., 4)`), donc
le retirer est invisible au bit près après chargement. La photo de la PR-40
était par ailleurs stockée en 2704×1568 pour une fenêtre qui la redimensionne à
la volée : ramenée à 1600 px de large (Lanczos), toujours au-dessus de toute
taille d'affichage réaliste. Bénéficie au préchargement WASM **et** aux trois
installeurs de bureau.


### Fixed — passe de cohérence doc ↔ code (2) : les guides 6502 et les cartouches CodeTank

> **Vérifié par une exécution réelle** (les deux passes précédentes annonçaient
> ne pas avoir compilé) : `cmake` + `make` complets puis `ctest` → **90/90
> passent, 0 échec**, les 8 « skipped » étant les cibles conditionnées à cc65,
> absent de l'environnement. `make -C dev/lib check` passe ses gates de dérive
> (police partagée, C64-font, catalogue de sprites, shims wozmon) et ne
> s'arrête qu'au pas `ca65`, même cause. Les 46 sidecars `.sketch.json` du
> DevBench ne référencent aucun fichier manquant.

Deuxième passe, sur le périmètre que la première n'avait pas couvert : les
chemins cités par **tous** les docs (pas seulement `CLAUDE.md`), les
`README.md` de `dev/lib/`, les guides `sketchs/doc/` et les claims sur les
workflows CI. Là encore, aucun changement de comportement — la seule retouche
de source est un commentaire.

**La réorganisation des cartouches de juillet 2026 n'avait pas été propagée.**
Les images `Codetank_GAME1-7` ont disparu au profit de CLASSICS / BASIC_LOGO /
ARCADE / DEMOS, mais cinq documents faisaient encore graver les anciennes — et
dans deux cas la banque du jumper avait changé aussi, ce qui rend l'instruction
activement fausse :

- `APPLE-1_LOGO-2.6-MANUAL.md` + `-MANUEL.md` (FR) demandaient de flasher
  `Codetank_GAME1.rom` **jumper Upper** pour lancer LOGO. La bonne combinaison
  est `Codetank_BASIC_LOGO.rom` **jumper Lower** — l'ancienne fait démarrer
  l'interpréteur Applesoft sur un vrai Apple-1.
- `sketchs/tms9918/game_rogue/README.md` : Rogue est dans la banque **haute**
  d'`Codetank_ARCADE.rom`, pas dans la banque basse de `GAME2` ; `--rom=2`
  n'est plus une valeur acceptée (`--rom=arcade`).
- `sketchs/tms9918/demo_nyan_cat/README.md` : Nyan est passé d'une banque haute
  dédiée au slot `$6000` du menu **bas** de `Codetank_DEMOS.rom`.
- `sketchs/tms9918/tool_logo/README.md` : cartouche + cible de build.
- `sketchs/doc/Programming_TMS9918.md` décrivait encore la disposition
  `--layout=dualslot8k` (sacrifiant le menu et Snake) comme la solution au
  dépassement de slot de Galaga. Le drapeau n'existe plus ; la disposition
  ARCADE actuelle donne 8 448 B à Galaga **et** garde le menu.
- `doc/BASIC_COMPILER.md` datait l'interpréteur Applesoft TMS9918 de
  `CODETANKDEV.rom` (cartouche générée, sans interpréteur).

**Exemples et projets fantômes.** Trois guides renvoyaient le lecteur vers
`sketchs/gen2/demo_mandelbrot/HGR_Mandelbrot.asm` et
`sketchs/gen2/demo_house/HGR_House.asm` — deux sketchs absents de l'arbre,
cités comme *les* modèles à copier pour le tracé de pixels et le dessin de
formes. Remplacés par `demo_sierpinski/HGR_Sierpinski.asm` (qui consomme bien
`hgr_tables.inc`), `game_maze3d/HGR_Maze3D.asm` et
`demo_bestiary/HGR_Bestiary.asm`. Même classe ailleurs :
`sketchs/gen2/demo_symbols/`, `sketchs/tms9918/nino-democ/` (source sortie de
l'arbre avec GAME5), `sketchs/gen2/demo_dbuf` (le vrai démonstrateur de
double-buffer est `demo_bounces`), `dev/codetank/game6_maze3d/`,
`dev/lib/hgr/` (les `hgr_*` vivent dans `dev/lib/gen2/`), `dev/assets/` et
`dev/apple1-videocard-lib` (le WASM précharge `dev/cc65` + `dev/lib`).
`dev/projects/` — arborescence dissoute — était encore cité par le préambule
de ce fichier et par deux guides cc65.

**Une contradiction dans les deux sens sur les cibles natives du Bench.**
`Pom1BenchTargets.cpp` affirmait que les cibles 12-13 (Applesoft compilé natif)
sont *« guarded out of the WASM target table (see the ctor) »* — le
constructeur ne filtre plus rien, `targetMap_` est l'identité. Symétriquement
`CLAUDE.md` affirmait que le web expose « every target … same matrix as
desktop » sans mentionner que ces deux-là sont bien listées mais refusées par
le dispatch mode 5, avec `nativeSiblingOf()` qui renvoie -1. Les deux côtés
disent maintenant la même chose.

**Divers.**

- `README.md` annonçait que la carte GEN2 *« auto-loads
  `software/Graphic HGR/GEN2.HGR.BIN` »*. Ce fichier n'existe pas et aucun
  chargement automatique n'existe dans le code : les 17 programmes du dossier
  se chargent par *File → Load Memory* (qui branche la carte au passage).
- `CLAUDE.md` : `release.yml` se déclenche aussi sur les tags numériques nus
  (`1.9.5`), que le workflow lui-même qualifie de convention courante, pas
  seulement sur `v*`.
- `dev/lib/tms9918c/README.md` : section « Modules `lib/` » alors que les
  modules sont à plat dans le répertoire.
- `dev/cc65/Makefile.common`, `dev/lib/gfx/Makefile` et
  `tools/build_shared_font.py` renvoyaient à `sketchs/doc/TODO6502.md` ; le
  fichier est `dev/TODO6502.md`.


### Fixed — passe de cohérence doc ↔ code : 20 dérives corrigées

Audit systématique de la documentation contre le code qu'elle décrit. Aucun
changement de comportement : les seules retouches de sources sont des
**commentaires**. Ce qui a été vérifié mécaniquement (et est désormais vert) :
tous les liens relatifs des 85 `.md`, tous les drapeaux de `CliDispatcher.cpp`
contre `doc/CLI.md`, tous les noms de tests `ctest` cités dans les docs, tous
les chemins de fichiers cités par `CLAUDE.md`, et la table `kMachinePresets[]`
contre le tableau des presets du `README.md`.

**Fonctionnalités livrées mais encore décrites comme à faire.** C'est la
catégorie la plus trompeuse — un lecteur y renonce à une feature qui existe.

- `TODO.md` gardait **Uncle Bernie's Improved ACI** en 🚫 *bloqué, en attente du
  binaire de Bernie*. La carte est livrée depuis août 2026 (`roms/XACI.rom`,
  page `$C500-$C5FF`, cascade ACI dans les deux sens, `extended_aci_smoke`).
  Entrée supprimée.
- `doc/GEN2_RELEASE_questions.md` listait trois chantiers GEN2 comme restant à
  faire, tous terminés : les rendus TEXT / LORES / MIXED
  (`GraphicsCard::renderInternalSegment` les dispatche), le flag HBLANK MSB de
  la phase 2 (`Gen2VideoScanner::hst0State()`), et le portage du test
  `horizontal_split` de POM2 (livré en `gen2_horizontal_split_smoke`). Le même
  fichier annonçait encore les attributs de caractères (inverse / clignotant)
  comme un « hook futur » alors que `resolveGlyph` décode les trois bandes, et
  la page PROM `$C5xx` comme « possible future » — c'est l'ACI étendue.

**Symboles et fichiers déplacés que la doc suivait encore à l'ancienne adresse.**

- `kP1Targets[]` vit dans `Pom1BenchTargets.cpp` depuis la découpe du god file
  Bench, pas dans `Pom1BenchHost.cpp` : corrigé dans `CLAUDE.md`,
  `doc/DEVBENCH.md`, `MachinePresets.cpp` et `MainWindow_Presets.cpp`. Au
  passage, `CLAUDE.md` affirmait que chaque cible pointe vers l'un des trois
  bancs — deux cibles n'en pointent aucun (`-1` « toute machine » pour le hex
  Wozmon, `kPresetMicroSD` pour l'interpréteur Applesoft Lite).
- `doc/DEVBENCH.md` faisait charger l'interpréteur Applesoft TMS9918 depuis
  `CODETANKDEV.rom` et LOGO TMS9918 depuis `Codetank_GAME3.rom` — une cartouche
  générée (jamais commitée) et une autre retirée en juillet 2026. Les deux
  bancs viennent de `Codetank_BASIC_LOGO.rom`. Même dérive dans deux
  commentaires de sources (`Pom1BenchHost.cpp`, `BasicTokeniserApplesoft.cpp`),
  qui décrivaient encore CODETANKDEV comme portant l'Applesoft en banc haut.
- `doc/CLI.md` donnait `Codetank_GAME1.rom` comme cartouche par défaut de
  `--codetank-rom` ; la sonde réelle est `Codetank_ARCADE.rom` puis le legacy
  `roms/codetank.rom`.

**Trous de documentation.**

- **Le `TelemetryPort` était absent de `CLAUDE.md`** — ni dans la carte mémoire,
  ni dans la liste des périphériques, alors qu'il enregistre `$C440-$C443` en
  **priorité 30**, soit au-dessus du handler large `$C200-$C7FF` de GEN2. C'est
  exactement le genre d'arbitrage de bus que ce fichier existe pour documenter.
  Ajouté aux deux endroits, avec le renvoi vers `doc/TELEMETRY_SIDE_CHANNEL.md`.
- Quatre drapeaux du parseur n'avaient aucune ligne dans `doc/CLI.md` :
  `--vram-noise` (cité seulement en prose ailleurs), `--tms-frameflag-hostile`,
  `--ram-poison <HH>` et `--ram-trap`. Les quatre forment le harnais
  « marche sur POM1, casse sur silicium » ; ils ont désormais leur ligne.

**Comptes et affirmations fausses.**

- `MachinePresets.h` : le commentaire d'`extendedAci` disait « off sur tous les
  presets historiques, on pour POM1 Fantasy ». La table dit l'inverse — elle est
  **on partout où l'ACI est branchée sauf** `#0`/`#4`/`#5`. Commentaire réécrit
  avec la liste exacte.
- `CLAUDE.md` : `MainWindow_ImGui` est réparti sur **14** TU, pas 12 (les 12
  suffixés + `MainWindow_ImGui.cpp` + `_Dock`), et `_Keyboard` est le seul à ne
  pas inclure `MainWindow_Internal.h`.
- `README.md` : la ligne CodeTank du tableau des cartes disait « 3 cartouches
  livrées » là où les deux autres mentions du même fichier disent 4 (CLASSICS /
  BASIC_LOGO / ARCADE / DEMOS). Et « Built with Dear ImGui & OpenGL » ignorait
  le backend Metal, défaut sur macOS depuis la couture `PomRenderer`.

**Chemins qui n'existent pas.**

- `doc/SKETCHS.md` documentait `sketchs/tms9918/_template_tms9918c/` — table des
  starters, section dédiée et ligne `make -C` — pour un dossier absent de
  l'arbre. Réécrit autour du vrai starter C TMS9918 (`tms9918_hello_c/`, sans
  Makefile, construit par le DevBench), avec la nuance que trois sketchs
  TMS9918 (`game_maze3d`, `tool_diapo`, `tool_tmsload`) ont bien le leur.
- `sketchs/doc/CC65.md` citait `demo_maze`, `lib_smoke` et `tms9918_logo` comme
  projets multi-objets à Makefile propre : aucun des trois n'existe. Remplacés
  par les vrais, et par le constat que `Makefile.common` n'a plus qu'un seul
  consommateur (`sketchs/apple1/_template/`).
- `dev/lib/tms9918c/README.md` annonçait des sorties dans
  `software/Apple-1_TMS_CC65/`, dossier parti avec les 41 fichiers retirés de
  `software/` le 2026-06-22 ; `make -C dev/codetank` compose en réalité les
  cartouches sous `roms/codetank/`.
- Huit liens relatifs cassés : `CHANGELOG.md` (`../doc/` depuis la racine),
  `doc/TELEMETRY_SIDE_CHANNEL.md` (chemins non préfixés `../`),
  `dev/codetank/README.md` (`../../../` = un cran au-dessus du dépôt),
  `dev/cc65/README.md` et `dev/lib/README.md` (`../projects/codetank/`, qui est
  `../codetank/`).


### Changed — passe « god files » : cinq fichiers monstres découpés, un couplage UI supprimé

Suite de l'audit architectural de juillet 2026 (`TODO.md` → *Refactors
architecturaux*). Deux natures de travail, à ne pas confondre :

**1. Un vrai découplage** — `kMachinePresets[]` sort vers `MachinePresets.{h,cpp}`,
un TU **sans UI**. La table est de la donnée pure (cartes, RAM, BASIC,
placements de fenêtres au premier lancement) que lisent la CLI, la table de
cibles du DevBench et le menu Hardware ; elle vivait dans
`MainWindow_Presets.cpp`, ce qui obligeait `CliDispatcher.cpp` à inclure
`MainWindow_ImGui.h` **pour deux accesseurs statiques** — et donc à traîner
ImGui + GLFW dans tout binaire liant le parseur de ligne de commande. C'était
la raison pour laquelle `parseCli()` est resté le 5ᵉ trou de tests de
juillet 2026 alors que les quatre autres étaient comblés.

- Nouveau test **`cli_dispatcher_smoke`** (90 tests au total). Son assertion la
  plus importante n'est pas dans le fichier : **c'est qu'il linke**. Recoupler
  le dispatcher à l'UI casse la compilation de cette cible.
- Il couvre au passage la sélection de preset par index et par nom, le rejet
  des index hors bornes (jamais de clamp silencieux), les bornes de
  `--audio-latency`, l'ordre des actions différées, et la règle « carte fille »
  (CodeTank ⇒ TMS9918, ACI étendue ⇒ ACI, IEC ⇒ microSD) sur toute la table.
- `MachineWindowPlacement` porte un POD `PresetVec2` au lieu d'`ImVec2` — c'est
  ce qui garde le TU sans UI ; l'interface convertit à un seul endroit
  (`detail::toImVec2`).
- **Piège** : `preset_ram_profiles_smoke` parse le fichier source **en texte**.
  Son argument `add_test` pointe désormais `MachinePresets.cpp` ; déplacer la
  table sans déplacer ce chemin fait échouer le test sur « table introuvable »
  (c'est comme ça que le déplacement a été rattrapé).

**2. Du code motion pur** — cinq god files scindés selon leurs axes réels,
sans changer une signature ni un site d'appel. Chaque découpage a été vérifié
en comparant le multi-ensemble des lignes de code non commentées avant/après :
identique à chaque fois, aux seuls préfixes `static` retirés et aux structures
déplacées vers un header près. 90/90 tests verts après chaque étape.

| fichier | avant | après | nouveaux TU |
|---|---:|---:|---|
| `Pom1BenchHost.cpp` | 3957 | 2229 | `_Lang` 571 · `Pom1BenchTargets` 456 · `Pom1BenchCc65` 789 |
| `MainWindow_Dialogs.cpp` | 3559 | 1807 | `MainWindow_Settings` 594 · `MainWindow_Tutorials` 1227 |
| `MainWindow_HardwareWindows.cpp` | 2530 | 1752 | `MainWindow_SiliconStrict` 818 |
| `MainWindow_Presets.cpp` | 2740 | 2327 | `MachinePresets` 407 |
| `EmulationController.cpp` | 2143 | 867 | `_State` 381 · `_Machine` 332 · `_Cards` 652 |

Les lignes de coupe sont choisies, pas arbitraires :

- **`EmulationController`** — 207 définitions de méthodes dans un seul fichier.
  Le `.cpp` garde le thread CPU, run/stop/reset, step + step-over, points
  d'arrêt / de surveillance / trace PC, injection clavier et la boucle de
  slice (les constantes de cadence restent là : la boucle est leur seul
  usager). `_State` = images mémoire, snapshots, rewind, rechargements de ROM.
  `_Machine` = les boutons de fidélité silicium et de diagnostic. `_Cards` =
  cassette/audio et les passthroughs par carte. L'ordre des mutex
  (`stateMutex > keyboard.keyMutex > publisher.snapshotMutex`) vaut pour les
  quatre.
- **DevBench** — `Pom1BenchTargets` prend la table `kP1Targets[]`, les sketches
  hello-world et les deux helpers de chemin : de la donnée, exactement comme
  `MachinePresets`. `Pom1BenchCc65` prend la couche **pure** (sondage
  Makefile/cfg linker, spec JSON de build C, cache d'archive `ar65`,
  fabrication des commandes cl65/ld65) — ni MainWindow, ni
  EmulationController, ni ImGui : à garder ainsi. `_Lang` prend l'**injection**
  d'interpréteur. La ligne de partage est *injection vs chaîne d'outils*, pas
  « ce qui touche aux langages » : `compileBasicNative` est un chemin BASIC lui
  aussi, mais il appelle ca65/ld65 et partage la plomberie cc65 de `build()`,
  donc il reste à côté de ce pipeline.
- **`MainWindow_SiliconStrict`** — la règle Parmigiani « une carte à la fois »
  **en tant que politique** (table `ConflictRule`, `gateStrictPlug`,
  `wouldCreateConflict`) plus son inspecteur. Assise au milieu de 52 blocs
  `ImGui::Begin`, elle avait l'air d'un détail de rendu ; elle n'en est pas un,
  et l'item ouvert « data-driver ces 52 blocs contre `Memory::cardSlots()` »
  commence par lui donner un fichier à elle.

Le plus gros fichier propre restant est `Memory.cpp` (2532 l.) : plus rien
au-dessus de 2 600 lignes, contre quatre fichiers > 2 500 et deux > 3 500 avant
la passe.

**Chemin WASM vérifié** : le build web est celui que personne ne compile en
local, et la découpe a bien failli y laisser un piège — `MainWindow_Settings.cpp`
hérite de la branche plein-écran Emscripten et donc de ses `#include`. Tous les
TU touchés ont été passés au `-fsyntax-only` sous `POM1_BUILD_FOR_EMSCRIPTEN=1`
avec des stubs Emscripten, et le contrôle a été prouvé mordant (retirer
l'include fait bien échouer la sonde).

### Added — la fenêtre Welcome explique l'ACI étendue

Une ligne dans le démarrage rapide (`C500R`) et une section dédiée. Elle dit
surtout ce que l'interface taisait : **`C500R` a l'air de ne rien faire**. Il
relocalise la ROM ACI dans la page de pile, la rustine et rend la main au
moniteur — c'est `RX RX` qui fait tourner la bande. Avec la marche à suivre
complète (Play d'abord), l'écriture (`<from>.<to>WX`), où brancher la carte, et
la rétro-compatibilité avec une ACI d'origine.

### Fixed — la CI Linux ne compilait plus : `<sstream>` perdu dans la découpe des god files

`MainWindow_Settings.cpp` construit la ligne d'état d'EhBASIC avec un
`std::stringstream`, mais l'`#include <sstream>` est resté dans
`MainWindow_Dialogs.cpp` — le TU d'origine, qui n'en utilise plus. Le fichier
scindé compilait quand même partout où l'en-tête arrivait transitivement ;
la libstdc++ du runner CI ne le fournit pas, donc `std::stringstream` y était
un type incomplet. Job `linux` rouge depuis `346e9e4` (la passe god files),
sur une erreur qu'aucune machine de dev ne voyait.

Un `-fsyntax-only` sur les 105 TU du projet confirme que c'était le seul
manque : la compilation s'arrêtait au premier fichier fautif, donc rien ne
disait que les TU suivants passaient.

### Fixed — la PROM de l'ACI étendue redevenait inscriptible dès qu'on branchait la carte GEN2

`$C500-$C5FF` est une PROM : `memWrite` la protège en écriture depuis qu'elle
existe. Sauf que la carte GEN2 HGR enregistre un handler `PeripheralBus` sur
**tout** `$C200-$C7FF` — ses soft switches sont des miroirs éparpillés
(`SEL = !A11 & A9 & A4`) — et retombe sur la RAM à plat pour tout ce qu'il ne
décode pas. Or `$C5xx` a `A9 = 0`, donc le décodeur GEN2 y est structurellement
aveugle… et le handler répond depuis `bus.tryWrite()` **en tête** de
`memWrite`, donc **avant** la protection ROM, qui n'était jamais atteinte.

Résultat : brancher la carte HGR rendait la PROM d'Uncle Bernie inscriptible.
Sur le profil POM1 Fantasy par défaut et sur tous les presets GEN2 — qui
portent justement la page étendue — un `STA $C5xx` égaré la corrompait en
silence. Précisément la page dont la note de conception dit qu'elle ne peut
être ni déplacée ni rognée, parce que le firmware s'y relocalise dans la page
de pile et rustine sa propre copie ; une fois corrompue, `C500R` déraille d'une
manière qui ressemble à un problème de bande.

Les fenêtres protégées vivent désormais dans `Memory::isRomWriteProtected()`,
que `memWrite` **et** le passe-plat GEN2 consultent — la règle est écrite une
fois, et CLAUDE.md la pose pour tout futur handler qui écrit `mem[]` lui-même.
Épinglé dans `extended_aci_smoke` partie A (vérifié rouge sans le correctif),
avec la contrepartie : la RAM de part et d'autre de la PROM (`$C440`, `$C640`)
doit rester inscriptible, sinon le correctif aurait gelé toute la fenêtre.

### Fixed — snapshots v6 : deux états vivants que la sauvegarde ne capturait pas

Chasse aux bugs sur le format de snapshot. Les deux défauts sont de la même
famille — un registre interne qui décide du comportement du matériel, présent
nulle part dans l'image mémoire, et absent de la section qui aurait dû le
porter. Le rewind rejoue exactement les mêmes blobs (`RewindBuffer` travaille
sur `saveSnapshotToBuffer`), donc chacun se manifestait aussi en scrubbant la
timeline, pas seulement sur un File → Load snapshot.

- **Les registres fantômes du PIA 6821 (`CRA`/`CRB` + `DDRA`/`DDRB`)
  n'étaient pas sauvegardés.** Ils ne sont pas reconstructibles depuis les
  64 Ko : `memWrite` sort **avant** le rangement dans `mem[]` quand le
  registre de direction est banké, donc DDRA/DDRB n'y sont jamais ; et si
  `mem[$D011]`/`mem[$D013]` reçoivent bien une copie des mots de contrôle,
  **toute lecture répond depuis le membre**. Restaurer gardait donc le banking
  de la machine *vivante* : un état pris pendant la sonde de Codebreaker
  (`CRB:=0` / lire `$D012` / attendre `$7F`) revenait avec `$D012` répondant
  le port d'affichage au lieu du registre de direction, et `$D013` relisant
  l'ancien CR. Quatre octets ajoutés à la section `MEM`.
- **Le compteur multi-secteurs de la CFFA1 (`sectorsRemaining_`) non plus.**
  Il n'est pas déductible des registres ATA — le registre de comptage est une
  copie que le transfert réécrit au fil des secteurs. Une restauration le
  laissait à la valeur de la carte de destination (0 sur une carte neuve),
  donc la frontière des 512 octets suivante le décrémentait sous zéro,
  retombait DRQ et **tronquait la lecture au secteur déjà en tampon** :
  ProDOS recevait une lecture courte puis `$FF` pour tout le reste. Un octet
  ajouté à la section `CFFA1`, plus `reset()` qui le remet à zéro (SRST
  laissait un transfert fantôme en compte).

Format **v6**. Les deux champs sont relus derrière `r.version() >= 6`, comme
le drapeau T2 du microSD l'a fait en v4 : un snapshot v1-v5 se recharge sans
rien perdre de ce qu'il portait — le PIA repart sur son état post-reset
(`$A7/$A7/$00/$7F`, ce qu'installe `resetMemory`, et surtout **pas** une
reconstruction depuis `mem[$D011]`/`mem[$D013]` qui valent 0 sur une machine
n'ayant pas exécuté le reset du moniteur, donc DDR bankés et ECHO qui pend),
et un transfert CFFA1 en vol est traité comme son dernier secteur, ce qu'il
faisait déjà.

Épinglé des deux côtés — chaque pin a été vérifié rouge sans le correctif :
**`pia_ddr_smoke`** gagne un aller-retour snapshot (§8), **`snapshot_smoke`**
une lecture CFFA1 de quatre secteurs interrompue en plein vol, comparée
octet à octet au même transfert mené d'une traite. Ce dernier fabrique sa
propre image (un octet distinct par secteur) au lieu d'emprunter
`cfcard/cfcard.po` : une plage de `$FF` dans l'image réelle se lit pareil que
le transfert continue ou qu'il avorte, et le test passait alors contre le bug
même qu'il devait attraper.

Au passage, l'écriture des CR (`$D011`/`$D013`) marque enfin la page `$D0`
sale. Le Memory Viewer dessine le snapshot **publié**, qui ne recopie que les
pages marquées — il affichait donc l'ancien mot de contrôle jusqu'à ce qu'une
autre écriture dans la page veuille bien la salir.

### Fixed — documentation : promesses que le dépôt ne tenait plus

Passe de vérification factuelle, pas de relecture au jugé.

- **Le bloc « Dev tools » du README annonçait sept programmes ; six n'étaient
  plus livrés.** `software/Apple-1 dev/` et `software/utils/` ont disparu le
  22 juin dans le commit `72b39a7` (« Update CMake configuration and BASIC
  sketches »), qui supprime 41 fichiers de `software/` sans que son message le
  mentionne. Bloc retiré — le septième, Party, est un démo déjà couvert.
- **`software/Apple-1_TMS_CC65/` est parti avec eux** mais restait cité par une
  infobulle qui envoyait l'utilisateur vers un dossier inexistant, par un
  commentaire de `Memory.cpp` et par CLAUDE.md. Infobulle et commentaire
  nettoyés ; la branche de code demeure (elle sert un dossier que l'utilisateur
  crée lui-même) et CLAUDE.md le consigne avec la date et le commit.
- **« 30th » → « 50th anniversary »** : le fichier livré est `50th.apl.txt`, et
  le reste du README fête les 50 ans d'Apple.
- **Le README ignorait trois capacités réelles** : le glisser-déposer et ses
  formats, la double nature de `.hex`, et un tableau « Cinq BASIC, un support »
  — Microsoft BASIC et EhBASIC n'y figuraient nulle part alors qu'ils sont
  accessibles depuis les Réglages.
- **`doc/README.md` se dit l'index de toute prose du dépôt** mais oubliait
  `HGR_AI_IMAGE_PROMPTS.md` et les deux `dev/*/README.md` neufs.
- **`doc/CLI.md`** documentait le routage par extension de `--load` sans
  mentionner l'Intel HEX ni le fait que sa détection est structurelle.

Vérifié au passage sans rien changer : les 20 programmes encore annoncés sont
tous livrés, la table des presets colle à `kMachinePresets` (13), aucun nom de
test cité dans CLAUDE.md n'est fantôme, aucun lien `.md` mort.

## [1.9.5] — 2026-08-09 — « Docked XACI »

### Fixed — le PIA 6821 : registres de direction ($D010-$D013), et Codebreaker croyait tourner sur un émulateur

Le Codebreaker d'Uncle Bernie affichait « HEY ! I WANT TO RUN ON A REAL
APPLE-1 ! » au lieu de jouer. Ce n'était pas une histoire de chronométrage : le
jeu **sonde le PIA**.

```
$0939: STA $D013   ; CRB := $00 → bit 2 à 0 : $D012 adresse le registre de DIRECTION
$093C: LDA $D012   ; lit DDRB
$0941: STX $D013   ; CRB := $A7 → retour au registre de données
$0944: CMP #$7F    ; DDRB doit valoir $7F, ce que programme le moniteur Woz
$0946: BNE ...     ; sinon → la pub
```

Chaque port du PIA cache **deux** registres derrière une adresse, choisis par le
**bit 2 du registre de contrôle** : à 0 le registre de **direction**, à 1 le
registre de données. POM1 n'en modélisait rien — `$D013` tombait en RAM
ordinaire et `$D012` relisait le dernier caractère écrit. Les quatre registres
sont désormais émulés (`$D010` KBD/DDRA · `$D011` CRA · `$D012` DSP/DDRB ·
`$D013` CRB), décodage A0-A1 compris.

**Le point délicat, trouvé en cassant neuf tests d'abord :** POM1 amorce le PIA
dans son état **après reset** (`CRA=CRB=$A7`, `DDRB=$7F`) et non dans l'état
silicium à la mise sous tension (tout à zéro). C'est porteur : POM1 saute
directement dans les programmes (`--run`, DevBench Run, `jumpTo`) sans exécuter
le reset du moniteur à `$FF00`, et un CRB à zéro laisse les registres de
direction en façade — l'ECHO du moniteur écrit alors ses caractères dans DDRB
puis **se bloque indéfiniment** sur son propre `BIT $D012 / BMI` dès qu'un
octet à bit 7 y atterrit. C'est ce qui vidait la trame GEN2 de référence.

Les lectures de `$D011` gardent leur sémantique historique (bit 7 = touche
prête, rien d'autre) : tous les programmes la testent par BIT/BPL, et relire les
bits de contrôle changerait chaque lecture du corpus sans aucun appelant qui le
demande.

Verrouillé par **`pia_ddr_smoke`** (le mécanisme isolé, plus les invariants
d'affichage et de clavier) et par **`extended_aci_smoke` partie C** : la vraie
bande, un niveau choisi, et l'exigence que le jeu file au tableau des scores
sans passer par la pub.

### Fixed — le deck cassette envoyait vers des commandes qui ne lisent pas la bande

Trouvé en testant le glisser-déposer : après avoir déposé `codebrk.aiff` (format
étendu d'Uncle Bernie), le deck affichait `Type C500R then RX RXR` et
`ARMED - waiting for C100R`. Deux instructions fausses.

- **`RX RXR` n'existe pas.** L'étiquette de cassette ajoutait un `R` à la valeur
  lue dans `tapeinfo.txt`. Correct pour une plage de chargement (`E000.EFFF` →
  `E000.EFFFR`, le `R` du moniteur Woz), absurde pour une entrée qui est déjà
  une commande complète. Le défaut était présent **à deux endroits** du widget.
- **La bannière ARMED codait `C100R` en dur** — précisément l'entrée qui ne lit
  *pas* une bande étendue, celle-ci s'amorçant par `C500R` puis `RX RX`. Le deck
  envoyait donc l'opérateur vers la seule commande incapable de la démarrer.

La règle vit désormais dans `CassetteDevice` (`tapeLabelCommand` /
`tapeArmingCommand`), à côté du champ qu'elle interprète : une entrée de
`tapeinfo.txt` est soit une **plage** — jamais d'espace, reçoit le `R` du
moniteur, s'arme sur `C100R` — soit une **commande complète**, reproduite
verbatim. Widget et tests s'accordent par construction.

Le chemin fonctionnel, lui, n'était pas en cause : le déclencheur de lecture est
la première lecture de `$C081` et n'inspecte aucune adresse d'entrée, si bien
que `extended_aci_smoke` chargeait déjà cette bande par `RX RX`. Ce test pinne
maintenant aussi les deux chaînes affichées, contre les frappes qu'il envoie
réellement.

### Added — EhBASIC 2.22 sur Apple-1 (portage POM1)

*Derived from EhBASIC.*

Enhanced 6502 BASIC de Lee Davison — flottants, chaînes, `IF..THEN..ELSE`,
`DO..UNTIL`, littéraux hexa et binaires — chargé en RAM à `$5000-$7FFF`. Cold
start `5000R`, warm `5003R`, programmes en `$0300-$4FFF` (~19 Ko libres).
Settings → Memory Settings → ROM Loading.

**Aucun portage Apple-1 n'existait** : l'amont ne fournit que `min_mon.asm`, un
stub pour le simulateur Kowalski avec un ACIA simulé à `$F000`, une entrée par
le vecteur RESET et une invite `[C]old/[W]arm ?` — rien de tout cela ne convient
à une machine dont le vecteur RESET appartient au Woz Monitor. `dev/ehbasic/src/`
le remplace : I/O sur le vrai PIA 6821 (`KBDCR`/`KBD` en entrée, `DSP` en sortie
avec la boucle d'attente ECHO de Woz) et deux `JMP` épinglés par le linker à
`$5000`/`$5003`, dans la convention de tous les autres interpréteurs de POM1.
`basic.asm` — l'interpréteur lui-même — n'est pas touché.

- **En RAM, pas en ROM** : même modèle que l'Applesoft Lite du microSD. Donc
  ≥ 32 Ko requis, et sous la règle Parmigiani toute carte décodant `$5000-$7FFF`
  l'ombrage — POM1 débranche exactement les trois concernées (microSD, CodeTank,
  Juke-Box) et le dit dans la barre d'état. CFFA1 et A1-IO/RTC sont laissées en
  place : elles ne chevauchent pas.
- **Pourquoi `$5000` et pas `$5800`** : la fenêtre `$5800-$7FFF` que supposent
  les discussions Apple-1 est **183 octets trop petite** pour un 2.22 complet.
- **La vérification, c'est l'exécution** : aucun binaire publié auquel se
  comparer, donc `ehbasic_smoke` démarre l'interpréteur sur le 6502 émulé,
  vérifie `PRINT 1/4` = `.25` et `PRINT SQR(2)` = `1.41421`, puis saisit une
  boucle `FOR` et la lance. Le test exige aussi la présence de
  `DERIVED FROM EHBASIC` dans l'image — **c'est une condition de licence**, pas
  une coquetterie : EhBASIC l'impose dans tout binaire distribué, et
  `dev/ehbasic/README.md` en est la moitié lisible par un humain.

### Added — glisser-déposer, Microsoft BASIC, fréquence CPU mesurée

Trois manques relevés en comparant POM1 à HoneyCrisp (l'émulateur Apple-1 de
Landon J. Smith).

- **Glisser-déposer de fichiers** (`glfwSetDropCallback` → `MainWindow_ImGui::
  queueDroppedFiles` / `processDroppedFiles`). La callback GLFW se déclenche
  dans `glfwPollEvents`, hors de la frame ImGui : les chemins sont donc mis en
  file et traités en tête de `render()`, avant le premier `Begin()` — un
  chargement peut lever des drapeaux de fenêtre que la frame doit honorer. Le
  routage par extension **rejoue exactement l'action du menu Fichier
  correspondante** : un `.txt/.hex/.apl/.mon/.tur` passe par `performMemoryLoad`,
  donc un dépôt depuis `software/Graphic HGR/` branche toujours la GEN2, évicte
  les cartes de stockage qui l'ombrageraient, charge les symboles et enregistre
  les régions de la Memory Map. Aussi : `.bin` (avec la même invite d'adresse),
  `.snap`, les bandes `.aci/.aiff/.wav/.mp3/.ogg/.flac`, et `.d64` (montage 1541
  + cascade IEC/microSD). Un dépôt multiple ne charge que le premier fichier et
  le dit. Desktop uniquement : sous Emscripten le navigateur fournit un objet
  File, pas un chemin. `.po` reste hors périmètre — l'image CFFA1 est ouverte une
  fois à la construction, sans API de remontage.
- **Microsoft BASIC 6502 à `$E000`** (`roms/msbasic.rom`, test `msbasic_smoke`) —
  8 Ko, lignée OSI, **virgule flottante**, portée sur le PIA de l'Apple-1. C'est
  la raison d'être de cette ROM : l'Integer BASIC de Woz n'a aucun flottant.
  Cold start `E000R`, warm `E003R` ; chargement par Settings → Memory Settings →
  ROM Loading. Le mutex `$E000` n'a demandé aucune machinerie : les deux
  interpréteurs partagent la fenêtre, en charger un évince l'autre — un seul
  support d'EPROM BASIC, comme sur la vraie machine. `unloadBasic` nettoie
  désormais **tout** `$E000-$FEFF` : MS BASIC fait presque le double d'Integer
  BASIC, et l'ancien effacement `$E000-$EFFF` laissait 3 Ko de l'interpréteur
  précédent en place lors d'un changement.
  **La ROM est reconstructible depuis les sources publiques** :
  `dev/msbasic/build_msbasic.sh` la rebâtit à partir de commits épinglés de
  mist64/msbasic + de l'overlay coopzone-dc, applique les quatre branches de
  dispatch que l'auteur de l'overlay n'a jamais publiées (sans le patch de
  `header.s`, `E000R` tombe au milieu d'une table de vecteurs au lieu de démarrer
  BASIC) et **vérifie le sha256 — qui est celui de la ROM publiée, au bit près**.
  Provenance, patchs et position sur la licence : `dev/msbasic/README.md`.
- **Fréquence CPU mesurée** (`EmulationController::getMeasuredCpuHz`, test
  `measured_cpu_rate_smoke`). La barre d'état affichait `executionSpeed × 60`,
  c'est-à-dire la *cible* du régulateur : une machine tournant à moitié vitesse
  se lisait exactement comme une machine à l'heure, et « Max » n'affichait aucun
  chiffre. Le débit réel est désormais mesuré sur une fenêtre glissante de 0,5 s.
  En Max il est affiché seul (la cible y est un ~60 MHz volontairement hors
  d'atteinte) ; en x1/x2 la cible reste l'étiquette honnête tant qu'elle est
  tenue, et le réel n'apparaît qu'en dessous de 95 % — barre calme au cas normal,
  parlante quand un programme verbeux fait décrocher la machine. Le temps est
  comptabilisé **avant** le retour anticipé « budget non plein », sinon la mesure
  ne couvrirait que les rafales et annoncerait des dizaines de MHz à x1.

### Fixed — chargeur hex : Intel HEX reconnu, `.TUR` sans marqueur `T`

Deux trous du même genre que l'ancien `.apl` qui écrivait son propre texte ASCII
en RAM : un fichier lisible par POM1 mais lu par le **mauvais** parseur, sans
erreur ni avertissement. Repérés en relisant le changelog de HoneyCrisp v1.3.6
(l'émulateur Apple-1 en ECMAScript de Landon J. Smith), qui traite les deux.

- **Intel HEX** (`src/IntelHexFile.h`, test `intel_hex_smoke`). `.hex` désigne
  deux dialectes sans rapport : le dump WOZMON (`AAAA: HH HH … / AAAAR`) et
  l'Intel HEX (`:10030000A9018510…0D`). Le second n'a pas d'adresse `AAAA:`, donc
  l'ancien parseur écrivait **chaque chiffre** — compteur d'octets, adresse de
  chargement, type d'enregistrement et checksum compris — comme *donnée*, à
  l'adresse courante quelle qu'elle soit. La détection est **structurelle, jamais
  par extension** (`looksLikeIntelHex` valide ensemble le compteur, le type et le
  checksum du premier enregistrement) : un Intel HEX nommé `.txt` se charge
  correctement, un dump WOZMON nommé `.hex` garde son comportement au bit près —
  aucun des ~100 dumps livrés ne commence même une ligne par `:`. Une fois le
  premier enregistrement validé, POM1 **s'engage** : checksum faux, type inconnu
  ou enregistrement au-delà de `$FFFF` (les types 02/04 sont un héritage x86)
  font échouer le chargement **bruyamment**, au lieu de repasser au parseur
  WOZMON qui recommencerait à écrire des en-têtes en mémoire. Un enregistrement
  de départ 03/05 joue le rôle du `AAAAR` final ; un EOF (type 01) manquant ne
  déclenche qu'un avertissement.
- **`.TUR` sans marqueur `T`** (`Memory::loadHexDump`, test
  `hex_dump_turbotype_smoke` §4). Le parseur ligne-structuré n'était sélectionné
  que par une ligne `T` isolée — or le marqueur est optionnel dans la nature : le
  même transfert circule aussi en blocs « adresse + `:données` » sans marqueur du
  tout. Ces fichiers-là retombaient sur le parseur legacy joint-lignes, donc sur
  l'échec exact que la branche ligne-structurée existe pour éviter (les 4
  derniers chiffres de chaque ligne pris pour l'adresse de la suivante — les 60+
  zones fantômes du 15 Puzzle). L'extension `.tur` force désormais le bon
  parseur ; le reste continue d'exiger son `T`.

### Added — CI « Borne Raspberry Pi » : binaires taillés pour un cœur (PGO + LTO)

Recette portée de NeoST (`.github/workflows/pi-borne.yml` +
`packaging/raspberry/build_in_bookworm_pi.sh`), adaptée à POM1.

- **Pourquoi un workflow séparé de `release.yml`** : la release publie une
  AppImage aarch64 **générique**, qui doit tourner du Pi 3 au Pi 5. La borne, elle,
  ne tourne que sur SON Pi — on peut donc compiler `-mcpu=cortex-a72` (Pi 4 /
  Pi 400) et laisser GCC utiliser le vrai modèle de coût du cœur. Déclenché à la
  main (`workflow_dispatch`, choix du cœur a72/a76/a53) ou par un push sur la
  branche `borne-raspberry` ; casser ce workflow n'empêche pas de publier.
- **Le PGO déménage du Pi vers le runner.** `build_native_pi.sh --pgo` faisait
  payer deux compilations complètes + le parcours d'entraînement **au Pi
  lui-même** — plusieurs heures sur un Pi 400, avec l'OOM-killer au lien LTO.
  Le même travail sur un runner ARM64 est gratuit. Sur NeoST, d'où vient la
  recette : PGO seul −20 %, PGO+LTO −34 % de temps CPU à code identique.
- **Build dans un conteneur `debian:bookworm`, pas sur le runner** : Raspberry Pi
  OS *est* bookworm (glibc 2.36), alors que `ubuntu-24.04-arm` estamperait
  `GLIBC_2.39` et le binaire ne démarrerait sur aucun Pi. Le script vérifie ce
  plancher, l'architecture AArch64, et qu'aucun **libGL de bureau** n'a été lié
  (sur un Pi c'est le rastériseur logiciel : en dépendre serait une régression
  silencieuse à 2 images/s plutôt qu'une erreur de lien). Le conteneur tourne
  nativement — pas de QEMU.
- **Deux paquets issus du MÊME build**, sans recompilation : une AppImage
  `POM1-<ver>-pi400-aarch64.AppImage` (Pi OS avec bureau) et un tar.gz
  `pom1-pi400-aarch64.tar.gz` pour la borne **sans bureau** — une AppImage v2
  réclame libfuse2, absent de Pi OS Lite, et la borne devrait l'extraire à chaque
  démarrage pour rien. L'arborescence du tar.gz est celle du dépôt
  (`build/POM1` + `roms/`, `software/`…) parce que c'est exactement ce qu'attend
  `pom1-session.sh` ; les scripts de borne voyagent dedans, donc l'arbre déballé
  s'installe lui-même. Le tag machine dans le nom évite qu'un paquet homonyme
  écrase l'AppImage générique de release dans le dossier d'artefacts aplati.
- **Le tar.gz est dérivé de l'AppDir, pas du binaire nu** — et c'est la CI qui
  l'a imposé : la première version expédiait `build/POM1` tel quel et le paquet
  mourait au lancement sur `libglfw.so.3: cannot open shared object file`. Pi OS
  Lite n'installe pas `libglfw3`, et un paquet dont tout l'intérêt est de ne rien
  compiler sur la borne ne peut pas exiger un `apt install` pour démarrer.
  linuxdeploy avait déjà rassemblé les bibliothèques et réécrit le RUNPATH en
  `$ORIGIN/../lib` pour l'AppImage : on réutilise le même AppDir (binaire dans
  `build/`, bibliothèques dans `lib/`, `$ORIGIN/..` retombant sur la racine
  déballée). Le script **échoue** si ce RUNPATH a disparu, et la CI lance le
  tar.gz sur un runner où `libglfw3` n'est pas installée — ce qui rend
  l'autoportance vérifiée plutôt que supposée.
- **Garde-fous du PGO conservés** : les deux passes partagent le même répertoire
  de build (GCC nomme les `.gcda` d'après le chemin absolu de l'objet, et
  `-Wno-missing-profile` rendrait l'échec totalement muet), et le build **échoue**
  si aucun profil n'a été collecté pour `M6502`, `Memory`, `GraphicsCard` et
  `TMS9918` — un parcours d'entraînement muet donnerait un placebo.
- **Piège d'expression GitHub refermé** : `inputs.pgo == false` seul aurait
  désactivé le PGO en silence sur le déclencheur `push` (l'entrée n'existe pas,
  et la comparaison lâche convertit `null == false` en `0 == 0`, donc vrai). La
  comparaison est gardée sous `github.event_name == 'workflow_dispatch'`.
- Les deux paquets sont **réellement lancés** sur le runner ARM64
  (`--list-presets`, le tar.gz depuis sa racine déballée) : c'est le seul contrôle
  qui prouve que `-mcpu` n'a pas produit d'instruction absente du cœur.

### Added — Uncle Bernie's Extended ACI : la 2ᵉ page PROM `$C500` de son ACI Gen-2

Code fourni par Uncle Bernie (`xaci.zip`, Applefritter, août 2026) et installé
tel quel : `roms/XACI.rom`, 256 octets, plus le repli compilé `kExtendedAciRom`
dans `Memory.cpp` — un build depuis les sources sans le fichier ROM démarre
quand même.

- **Ce n'est pas une carte de plus sur le bus, c'est une PROM de plus sur l'ACI.**
  Aucun MMIO nouveau : la carte reste le flip-flop `$C000` et le comparateur
  `$C081` de Woz. Ce qui change tient dans une page de ROM à `$C500-$C5FF`,
  à côté du firmware `$C100` laissé **intact**. D'où la règle fille, calquée sur
  CodeTank/TMS9918 : brancher la page étendue branche l'ACI en cascade,
  débrancher l'ACI l'emporte avec elle (`Memory::setExtendedACIEnabled`).
- **Ce que fait `C500R`** — et pourquoi la page est intouchable : elle recopie la
  ROM ACI `$C100-$C1FF` dans la **page de pile** `$0100-$01FF`, patche les
  branchements `R`/`W` de la copie vers un `RTS` (c'est la page étendue, pas le
  firmware de Woz, qui décide de ce qu'est une lecture/écriture), puis réoriente
  les deux `JSR $C1F1` vers `$C5F1`. Ce dernier patch porte deux choses à la
  fois : il installe l'accumulateur de **checksum façon Apple-II**, et il met à
  la retraite la queue `$01F1-$01FF` de la copie — soit exactement les octets
  dont le code relogé a besoin comme pile (SP démarre à `$FF`). Retirer l'un ou
  l'autre et le firmware écrase sa propre pile.
- **Format étendu** : en-têtes de 8 octets portant les adresses from/to, adresses
  égales = autostart. La bande porte donc son adresse de chargement — `C500R`
  puis `RX RX` charge *et* démarre, `<from>.<to>WX` enregistre. Rétrocompatible :
  un ACI d'origine ou un Apple-II relit la même bande en soustrayant 8 à chaque
  `<from>` et en sautant le bloc d'autostart.
- **Décodage AIFF ajouté** (`CassetteDevice::loadAiffTape`). Non négociable :
  miniaudio ne décode pas l'AIFF, et l'AIFF est ce qu'émet **ACIace**, le
  synthétiseur de bandes d'Uncle Bernie — sans lui aucune bande au format étendu
  n'était chargeable. Lecteur IFF big-endian écrit à la main (COMM/SSND, taux
  d'échantillonnage en flottant étendu 80 bits, PCM 8/16/24/32 + AIFF-C
  `sowt`/`fl32`). Deux pièges traités : le PCM AIFF est **signé à toutes les
  largeurs**, y compris 8 bits (celui du WAV est non signé), et `.aiff` prend le
  chemin *pulse* inconditionnellement comme `.aci` — c'est de la **donnée**
  cassette, jamais de la musique de platine, et le mode flux ne porterait aucune
  transition pour `$C081`.
- **Exposition** : entrée « └ Uncle Bernie's Extended ACI ($C500) » sous l'ACI
  dans le menu Hardware (avec la séquence de touches en infobulle), `--enable
  xaci` (alias `extended-aci`, `aci2`), région dans les deux barres de carte
  mémoire, ombrage ROM dans l'inspecteur, champ `extendedAci` dans
  `MachineConfig`. **Branchée d'office partout où l'ACI l'est** — presets 2, 6, 11,
  12, et tout branchement manuel de l'ACI (menu Hardware, pastille de la
  barre d'outils, `--enable aci`, le branchement de secours de l'éditeur SFX)
  l'amène avec lui. **Deux exceptions assumées** : les presets **4** (« ACI &
  BASIC cassette », octobre 1976) et **5** (« SWTPC GT-6144 », 1976) gardent un
  ACI de Woz d'origine — ce sont les machines historiquement fidèles, et une
  PROM de 2026 n'y a pas sa place. Décocher la page seule redonne partout
  ailleurs une carte d'origine. Le **bench CC65 (preset 0) suit son modèle** et
  reste donc lui aussi en ACI d'origine : `preset_ram_profiles_smoke` exige que
  chaque banc reproduise exactement la machine pour laquelle il compile, et il a
  attrapé la divergence dès le premier passage. `Memory` reste de la mécanique
  pure : `setACIEnabled(true)` n'auto-branche **pas** la page, sans quoi le
  branchement différé de l'ACI viendrait défaire un `--disable xaci` explicite.
- **Bande de démo livrée** : `cassettes/codebrk.aiff` — le Codebreaker d'Uncle
  Bernie, tel qu'il le distribue.
- **Test `extended_aci_smoke`** : partie A, la carte (mapping `$C500`,
  protection en écriture, cascade ACI dans les deux sens, RAM ordinaire quand la
  page est absente) ; partie B, le firmware de bout en bout — `codebrk.aiff`
  joué à travers la vraie extraction de pulses, `C500R` puis `RX RX` tapés au
  clavier émulé, et la bannière du jeu attendue sur `$D012`. Rien n'est injecté :
  les octets ne peuvent apparaître que par décodage AIFF → passages par zéro →
  `$C081` → firmware étendu relogé en page 1. C'est aussi le **seul** garde-fou
  du lecteur AIFF.

### Added — borne Raspberry Pi : les optimisations de NeoST portées dans POM1

Reprise de la campagne « borne » de NeoST (`packaging/raspberry/`,
`docs/PERFORMANCE.md`), poste par poste, adaptée à POM1.

- **Le préambule GLSL n'est plus figé.** `OpenGLShader.cpp` déduit le dialecte de
  `GL_SHADING_LANGUAGE_VERSION` puis essaie **150 → 140 → 130** en compilant
  réellement chaque candidat (`#version 300 es` sur un contexte GLES) ; le
  backend ImGui reçoit le même traitement (`PomRenderer_GL.cpp`,
  `imguiGlslVersion`). Motif : le V3D des Raspberry Pi (Mesa) plafonne à GLSL
  **1.40** et rejetait les shaders CRT — « GLSL 1.50 is not supported. Supported
  versions are: 1.10, 1.20, 1.30, 1.40, 1.00 ES, 3.00 ES » — alors que leur corps
  n'utilise que des constructions **1.30** (`in`/`out`, `texture()`, `fwidth()`).
  La cascade est un filet et pas une coquetterie : un pilote peut annoncer une
  version et la refuser dans *ce* contexte, seule la compilation tranche. Les
  échecs intermédiaires sont muets et `errorOut` est vidé en cas de succès, sinon
  le panneau CRT afficherait « shader indisponible » avec une pile prête.
  Diagnostic systématique : `[CRT] GLSL 140 (driver: 1.40)`.
- **Création de contexte en cascade** (`main_imgui.cpp`) : 3.2 core → 3.2 → 3.1 →
  3.0. Le repli sous 3.2 était refusé tant que `#version 150` était codé en dur à
  deux endroits ; ce n'est plus le cas. Le Pi (GL 3.1 max en desktop) ouvre donc
  une fenêtre sans la rustine `MESA_GL_VERSION_OVERRIDE=3.3`. Validé sous Mesa
  llvmpipe avec versions forcées : 1.40 → 140 (cas Pi), 1.30 → 130, image rendue
  à travers la pile CRT dans les deux cas ; le palier natif `-DPOM1_GLES=ON`
  compile et tourne de bout en bout (`[CRT] GLSL 300 es`).
- **`--audio-latency MS`** (borné [20, 250], défaut inchangé ≈17 ms) : miniaudio
  garde 3 périodes et les allonge. Le défaut est calibré pour un bureau ; sur un
  Pi chargé la miss se produit dans l'ordonnanceur et se traduit par un
  grésillement continu que rien côté émulateur ne corrige.
- **`--fullscreen`** : plein écran d'emblée, **ré-imposé après chaque changement
  de profil** (chaque `loadPresetLayout` restaure sa propre géométrie). C'est ce
  qui permet à la borne de tourner sur un **X nu**, sans le moindre gestionnaire
  de fenêtres — une recopie de trame en moins. Non persisté dans
  `ini/preset_NN.size` : un drapeau de ligne de commande ne doit pas réécrire une
  disposition enregistrée.
- **`packaging/raspberrypi/` refait** sur le modèle NeoST :
  `build_native_pi.sh` (palier GLES par défaut, `-mcpu` déduit de
  `/proc/device-tree/model` et non `-mcpu=native`, `-j` calé sur la RAM, `--pgo`
  en deux passes, `-DPOM1_LTO=OFF` sous 2 Go), `pgo_train.sh` (13 charges
  headless : Woz Monitor, Integer BASIC, GEN2 HGR, TMS9918, microSD, CFFA1),
  `install_kiosk.sh` (X nu sur le VT 1 par une unité systemd modèle, purge des
  serveurs de son → miniaudio parle à ALSA en direct avec détection HDMI par
  l'ELD, gouverneur `performance`, `irqaffinity=0`, Bluetooth/Wi-Fi/swap coupés,
  boot silencieux, `--uninstall` qui rend `config.txt`/`cmdline.txt`), plus
  `pom1-kiosk.sh` / `pom1-session.sh` / `pom1-kiosk@.service`.
  ⚠ Deux pièges refermés dans les scripts plutôt que dans un ticket : miniaudio
  demande `SCHED_FIFO` pour son thread ALSA et **échoue silencieusement** sans
  `LimitRTPRIO=` (thread audio préemptible → underruns), et les deux passes du
  PGO doivent **partager le même répertoire de build** (GCC nomme les `.gcda`
  d'après le chemin absolu de l'objet ; `-Wno-missing-profile`, indispensable
  pour les objets d'interface non entraînés, rend l'échec totalement muet) —
  d'où le contrôle qui **échoue** si `M6502`, `Memory` ou `GraphicsCard` n'ont
  pas de profil.
  ⚠ Non porté : le mode enceinte Bluetooth de NeoST (PipeWire + A2DP), qui
  réintroduirait le serveur de son qu'on vient de retirer — marche à suivre dans
  le README du dossier. Scripts **non encore rejoués sur un Pi réel**.
- **`option(POM1_LTO)`** (ON par défaut) : échappatoire pour les machines dont le
  lien LTO se fait tuer par l'OOM-killer (`cc1plus: fatal error: Killed`).

### Added — interface zoom (the whole UI, docking included)

- **Settings ▸ UI Theme ▸ Interface zoom** (also in Display Settings): a 75 –
  250 % slider plus Zoom in / Zoom out / Reset that scales the **entire**
  interface — Dear ImGui widget geometry (padding, rounding, scrollbars, item
  spacing) **and** fonts **and** POM1's own toolbar / status bands, so the
  dockspace and the two bars that frame it keep their proportions. Technique
  lifted from POM2's `Pom2Theme`: `applyUiTheme()` now rebuilds the style from a
  **default-constructed** `ImGuiStyle` on every call, because
  `ImGuiStyle::ScaleAllSizes()` is *cumulative* — re-applying a zoom on a live
  style compounds the padding. Fonts scale through `style.FontScaleMain` (user
  zoom) × `style.FontScaleDpi` (monitor scale), the 1.92 dynamic-font path, so
  nothing is re-rasterised and no backend texture is rebuilt mid-session.
- The band constants POM1 owns (`kToolbarBandHeight`, `kStatusBarBandHeight`, …)
  are authored at 100 % and multiplied at the point of use by the new
  `detail::uiPx()`; `ScaleAllSizes` knows nothing about them, and left unscaled
  they would let the dockspace overlap a toolbar whose contents had grown.
- Changing the zoom also rescales floating window rects once
  (`ScaleWindowsInViewport`, applied at the top of the next `render()` like
  ImGui's own DPI path) so a panel doesn't clip its now-larger contents. Docked
  panels keep their share of the dockspace.
- **Correctif (août 2026) — boîtes figées autour d'un glyphe qui, lui, zoome.**
  Reste du même piège que `kToolbarBandHeight` : une taille en pixels passée à
  `ImGui::Button` est authored à 100 %, alors que la police à l'intérieur suit le
  zoom. Trois symptômes, une cause. **(a)** Les boutons `x1`/`x2` de la barre
  d'outils gardaient une hauteur de 24 px pendant que leurs voisins grandissaient
  — d'où des boutons qui « perdaient leur forme » ; ils prennent maintenant
  `btnSize` (et une largeur plancher égale, donc le rapport tient). **(b)** La
  pastille de teinte du moniteur, carrée par construction, restait 22×22 →
  `uiPx()`. **(c)** Dans le lecteur de cassette, les six icônes d'en-tête étaient
  **coupées** dès ~150 % : boîte fixe de 38×38 pour un glyphe dessiné à
  `1,45 × police zoomée`. Le module étant réutilisable tel quel (POM2), il lit le
  zoom dans `ImGuiStyle` (`FontScaleMain × FontScaleDpi`, ce que publie
  `applyUiTheme()`) plutôt que d'inclure un en-tête de POM1 — même produit,
  aucune dépendance ajoutée — avec un plancher `police × 1,45 + padding` qui fait
  **grandir la boîte** au lieu de rogner l'icône. Les paires VOL+/VOL− dérivent
  désormais de cette taille déjà zoomée, et la contrainte de taille minimale de
  la fenêtre suit, sinon le plancher ne contenait plus sa propre rangée de
  boutons. Vérifié par capture à 100 / 200 / 250 %. Même traitement pour les
  largeurs figées des boutons de dialogue (`Save`/`Cancel`/`Load Tape`…, 17
  sites) et les champs d'adresse hexa de Save Memory.
  ⚠ Reste ouvert, autre cause : au-delà de ~200 % la **rangée d'outils dépasse
  la largeur de la fenêtre** et sa fin devient inatteignable (il n'y a ni
  défilement ni repli). Ce n'est plus un problème de rapport mais de place.
- **HiDPI is now one factor of that product** instead of a separate font-only
  slider: `io.FontGlobalScale` is gone (obsolete since ImGui 1.92), the monitor
  content scale is polled each frame (`syncUiDpiScale`) so moving the window to
  another monitor re-scales live, and "Auto (follow monitor DPI)" simply decides
  whether that factor counts. Persisted as `ui_scale` in `ini/ui.settings`; a
  pre-existing manual `hidpi_scale` migrates into it.

### Changed — Raspberry Pi defaults to POM1's in-process file browser

- `NativeFileDialog` now has a per-platform **compiled default**
  (`defaultEnabled()`): native pickers on desktop Linux / macOS / Windows,
  **off on a Raspberry Pi** — the kiosk session (`packaging/raspberrypi`) runs a
  bare matchbox WM with no GTK/KDE desktop behind it, so a forked
  zenity/kdialog pays a multi-second cold start on an SD card, can land behind
  the fullscreen window, and may not be installed at all. Detected both from the
  native GLES tier (`-DPOM1_GLES=ON`) and at runtime from
  `/proc/device-tree/model`, because the Pi installer builds with a plain
  `cmake`.
- The Settings toggle is now **persisted** (`native_dialogs` in
  `ini/ui.settings`) instead of being session-only, so a Pi user who wants the
  native picker gets it back on every launch — and vice versa on the desktop.

### Changed — architecture: one card registry, and the core is UI-free again

- **`Memory::cardSlots()` replaces four hand-synced lists.** Adding a card used
  to mean editing, in lockstep and in one file, the FLAGS bitmap pack, the FLAGS
  unpack, the per-card section write order and the read-dispatch vector — and
  `snapshot_smoke` carried a *fifth* copy of the card list, so the test meant to
  catch a forgotten card would itself have been the thing forgotten. All five
  are now one ordered table. The collapse is safe because the four orders
  already agreed once the flag-only rows (SID Special Edition, cassette audio,
  silicon-strict, GEN2 attach) are interleaved at their historical positions, so
  the on-disk format is unchanged.
- The 8-byte section-name truncation — a latent collision trap that already bit
  `A1-IO/RTC` once — is now a **compile-time** `static_assert` instead of a
  single runtime test. And because a save and a load written by the same table
  always agree with each other, no round-trip assertion could catch a reordered
  row; `snapshot_smoke` therefore gained an explicit **section-order pin** that
  names the on-disk sequence.
- **Snapshot I/O moved out of `Memory.cpp`** into `MemorySnapshot.cpp` (2569 →
  2157 lines). Pure translation-unit split: still `Memory` member functions, no
  friendship, no API change. The two ROM-reload magic numbers (`D8 58` at
  `$FF00`, `A9 00` at `$8000`) are now behind named predicates
  (`wozMonitorPresent()` / `sdCardOsPresent()`).
- **`EmulationController` no longer reaches ImGui.** It held a `Screen_ImGui*`
  and included `TMS9918.h` for a POD counter block, so `imgui.h` landed in every
  consumer of the "core" controller's header. It now depends on the
  `DisplayDevice` abstraction that was already injected via
  `Memory::setDisplayDevice` (which gained `clear()` / `resetDisplay()`), and on
  a split-out `pom1::Tms9918DropDiagnostics`. Chasing the last edge also made
  **`TMS9918.h` itself ImGui-free**: its colour constants were `ImU32` (a plain
  `unsigned int`) and the palette used `IM_COL32`, so a chip emulation was
  pulling in a UI toolkit for a colour literal. Verified with `-MM`: zero ImGui
  headers reachable from `EmulationController.h`.

### Changed — UI: photo windows are table-driven (and three leaks are gone)

- Eight Help → Photos windows differing only in title, file and size floor each
  carried an `ensure<X>Texture()` + `render<X>PhotoWindow()` pair plus four
  members: **−351 lines and −32 members**, replaced by one `PhotoWindowDef`
  table and one generic renderer. Window titles are unchanged byte-for-byte, so
  saved per-preset layouts still bind.
- Collapsing the teardown into a loop fixed a real bug: the Copson, Happy-Woz
  and P-LAB-TMS9918 textures were **never destroyed** by `releaseGLResources()`
  — three of eleven hand-written `drop()` calls had simply been forgotten. That
  is precisely the failure mode the table shape prevents.

### Added — tests for four modules that had none (74 → 78)

- **`disassembler_smoke`** — for every non-control-flow opcode, executes it and
  compares the CPU's real PC advance against the disassembler's `instrLen`. Two
  independent implementations encode that length, and a disagreement makes the
  Debug Console render a byte stream shifted from the one being executed. This
  pins the undocumented-multi-byte-opcode invariant (CLAUDE.md › M6502) that
  nothing covered: `symbols_smoke` calls the disassembler 16 times but never
  looks at `instrLen`.
- **`a1io_rtc_smoke`** — injected fixed clock via `setOverrideTime()` (stable in
  any time zone), analog/digital input channels, 65C22 VIA register file, reset.
- **`terminal_card_smoke`** and **`wifi_modem_smoke`** — both "desktop-only"
  cards tested headlessly, because the parts that hold logic need no socket.
  Writing them surfaced two things worth knowing: `TerminalCard::reset()`
  **binds localhost:6502**, so the test deliberately never calls it (it would
  fight a running POM1 or a parallel ctest job); and the 65C51 data register is
  paced at the emulated baud rate by `advanceCycles()`, so a drain loop must
  tick the clock between reads.
- `CliDispatcher` remains untested: `parseCli()` is pure, but its TU includes
  `MainWindow_ImGui.h` for two static preset accessors, so linking a unit test
  pulls in the whole UI. Decoupling it is now its own TODO item — it also
  unblocks the external `presets.json` work.

### Fixed — Windows: one self-contained `POM1.exe`, no DLL (issue #34)

- **POM1 no longer ships any DLL next to the executable, and no longer needs the
  Visual C++ Redistributable.** Since the 1.9.2 ZIP started deploying the VC++
  runtime app-local (commit `3d4ec54`), POM1 failed to start on some machines
  with `GLFW error 65542` — *"WGL: The driver does not appear to support
  OpenGL"* — on a fully up-to-date RTX 4070 Ti Super, while 1.9.0 worked. That
  message names the wrong culprit: GLFW emits it from `choosePixelFormat()`
  whenever the enumeration yields no non-generic pixel format, which is what a
  **failed ICD load** looks like from the outside. The real cause was DLL search
  order — Windows resolves from the application directory *first*, so the
  vendor's OpenGL ICD (`nvoglv64.dll`, which uses the STL) picked up our
  `msvcp140.dll` 14.44 while `msvcp140_1.dll` still came from System32 at the
  installed redist's version; the `msvcp140*` family is versioned in lockstep,
  so the mix stopped the ICD from loading.
- Fixed by linking the **CRT statically** (`POM1_WIN_STATIC_RUNTIME`, ON by
  default → `CMAKE_MSVC_RUNTIME_LIBRARY` set before the first target is created,
  so libresidfp and every test executable inherit `/MT` too) and **GLFW
  statically** (vcpkg `x64-windows-static`, whose triplet already carries
  `VCPKG_CRT_LINKAGE=static`). "No redist" *and* "no app-local DLL" together
  leave no other option: `vcruntime140`/`msvcp140` are not in-box on Win10/11,
  and no runtime trick recovers the in-between case. `/MT` absorbs the UCRT as
  well, so the `api-ms-win-crt-*` imports disappear. The ZIP gets *smaller*.
- The packaging script and the release workflow now assert the **opposite** of
  what they asserted through 1.9.4: zero DLL in the package root, plus a
  `dumpbin /dependents` check that the import table names no CRT and no GLFW.
  The bug itself is not reproducible in CI (no NVIDIA GPU on the runners), so
  the guarantee is structural.
- Accepted cost, recorded deliberately: `/MT` freezes the CRT, so a UCRT or
  vcruntime CVE needs a POM1 rebuild instead of an automatic redist update.
  POM1's network surface is small but non-zero (TerminalCard `:6502`, WiFiModem
  TCP). The other classic static-CRT hazards do not apply — POM1 loads no
  plugin, so there is no second heap and no CRT object crossing a module
  boundary.
- **The GLFW version is now pinned, in `vcpkg.json` at the repo root.** Both the
  release job and `setup_pom1.bat` used to run `vcpkg install
  glfw3:x64-windows-static`, which resolves against whatever baseline the runner
  image's vcpkg checkout happens to sit at — so a runner refresh could swap the
  statically-linked GLFW without a single line of POM1 changing. The manifest's
  `builtin-baseline` (vcpkg release `2026.06.24`, commit `cd61e1e2`) plus an
  explicit `overrides` entry now fix it at **glfw3 3.4#1**, the same discipline
  the bionic AppImage image applies with `GLFW_VER` + SHA256, and without adding
  a second dependency mechanism (no `FetchContent`). Both call sites lose the
  package argument — vcpkg *rejects* one in manifest mode — and the CMake
  toolchain picks the manifest up on its own at configure time.

### Added — actionable diagnostic when POM1 cannot open a window

- **`glfwInit()` and `glfwCreateWindow()` failures now explain themselves**
  instead of returning `-1` in silence. This is what made issue #34 last a
  month: the user could see the GLFW one-liner, but it blamed the graphics
  driver, so he reinstalled NVIDIA drivers that were never the problem. POM1
  now latches the last GLFW error code + text and prints a report naming the
  real causes in order of likelihood — app-local DLL shadowing the ICD's CRT
  first on Windows, then missing/absent vendor driver or an RDP/VM session,
  then a pre-3.2 GPU — together with the facts already in hand (requested
  context, GLFW build string). On Windows the same report also goes to a
  `MessageBoxW`, for the user who launched from Explorer and watched the
  console flash away.
- One **free** retry was added on the desktop-GL path: if the core-profile
  request is refused, POM1 retries once without pinning the core profile, since
  some virtualised and older ICDs refuse core outright yet hand out a 3.2+
  compatibility context where `#version 150` is still valid. Deliberately the
  only fallback — going below 3.2 would mean maintaining GLSL 130 variants of
  both hardcoded `#version 150` sites, and the real sub-3.2 path already exists
  as the `-DPOM1_GLES=ON` tier. In the failure mode of #34 no fallback could
  have helped anyway: the ICD never loaded, so every context request failed
  identically.

### Added — OpenGL ES 3.0 tier + Raspberry Pi package

- **`cmake -DPOM1_GLES=ON` builds against OpenGL ES 3.0 / GLSL ES 300** instead
  of desktop GL 3.2 core. This is what unblocks the **Raspberry Pi 4/5**: Mesa's
  V3D driver caps *desktop* OpenGL at 3.1, so POM1's default core-profile
  context request failed outright and no window ever opened. The `POM1_GL_ES`
  macro (`src/POM1Build.h`) now actually drives the three GL translation units —
  header selection, `#version 300 es` prologue, direct entry points instead of
  `glfwGetProcAddress`, `GLFW_OPENGL_ES_API` + `GLFW_EGL_CONTEXT_API` — where
  they previously keyed on `__EMSCRIPTEN__`, i.e. "are we a browser" rather than
  "do we speak GLES". Same sources for both tiers; only the link line
  (`GLESv2`/`EGL` vs `libGL`) and the context creation differ.
- **A Raspberry Pi AppImage is now built and published per release**
  (`POM1-<version>-aarch64.AppImage`, `raspberry` job). Native arm64 runner,
  compiled inside a `debian:bookworm` container so the glibc floor matches
  Raspberry Pi OS (2.36) rather than the runner's 2.39 — an AppImage never
  bundles glibc, so the build image *is* the floor. `build_appimage.sh` derives
  the architecture from `uname -m`, and CI compiles the GLES tier on every push
  so the tag-only release job is no longer the first thing to try it.

### Added — CRT effects on macOS (Metal)

- **The universal CRT effect stack now runs on the macOS Metal backend.**
  It was OpenGL-only, so on macOS every slider in "CRT Effects (sliders)..."
  did nothing and the Settings panel said as much. `CrtEffectStackMetal`
  (MTLRenderPipelineState + two private RGBA8 render targets ping-ponged for
  phosphor persistence) mirrors the GL stack effect for effect — same order,
  same constants, same analytic anti-aliasing — and `Pom1CrtEffects` picks
  between the two from the live renderer.

### Fixed

- **WASM asset staging is a deny-list, not an allow-list of extensions.** The
  desktop packagers ship the whole `sketchs/` tree, so any extension missing
  from the old allow-list vanished from the *web build only* — silently, in the
  one build nobody runs locally (that is how `sketchs/logo/*.logo` went missing
  from `POM1.data`). Now everything ships except heavy media and build residue.
  Bundle: 1147 preloaded files, up from 1117, for +275 KB.

## [1.9.4] — 2026-07-25

### Changed — boot straight into POM1 Fantasy by default

- **First launch now boots directly into the POM1 Fantasy profile** instead of
  showing the profile chooser. `ini/startup` gained a third state so all paths
  stay reachable: no file → POM1 Fantasy (new default); `auto=1,preset=N` →
  that preset (the chooser's "always start with this profile" box); `chooser=1`
  → show the chooser at startup (Settings → "Show profile chooser at startup").
  The chooser is still openable mid-session (Settings → "Profile chooser
  now..."), and CLI `--preset` overrides everything. Fixed the game/demo
  hex-dump loader in the same cycle (see below), so the Fantasy machine now
  runs every bundled program.

### Added — adaptive UI refresh (perf, old machines)

- **Adaptive UI refresh** (Display Settings → Performance, on by default,
  desktop only) — when nothing on screen is changing (no input for 2 s, no
  pending Apple-1 output, no open card-framebuffer window with the CPU
  running, CRT shader off, no status message), the ImGui re-render drops to a
  ~5 Hz floor instead of 60: an idle Wozmon renders ×10 fewer frames
  (measured 660 vs 1500 over 30 s). The emulation thread is untouched; GLFW
  events stay polled every ~10 ms so any input restores full rate within one
  tick, and a window-refresh callback repaints immediately on expose. The
  5 Hz floor guarantees a missed animation degrades to 5 fps rather than
  freezing. Persisted as `idle_throttle` in `ini/ui.settings`. This is P2-D
  of [`doc/PERF_VIEILLES_MACHINES_FR.md`](doc/PERF_VIEILLES_MACHINES_FR.md).
- **Card texture uploads are dirty-gated** — the TMS9918 window used to
  re-upload its full 288×216 framebuffer to the GPU every frame even when
  the picture was static (the chip re-rasterises every line regardless);
  it now memcmps against the last uploaded copy (~µs, early-out) and skips
  the upload when identical, and the GT-6144 does the same (GEN2 was
  already gated by `GraphicsCard::render()`'s diff). The same
  changed-recently signal replaces the throttle's coarse "card window open
  + CPU running" condition, so a game sitting on a static title screen now
  idles too; a resuming animation is picked up by the ~5 Hz floor within
  one tick.

### Added — universal shader CRT effects (opt-in)

- **CRT Effects window (Settings → "CRT Effects (sliders)...")** — a universal
  GPU post-process (ported from POM2's effect stack) that applies a composable
  CRT look to **every emulated framebuffer at once**: the Apple-1 text screen
  AND the GEN2 HGR / TMS9918 / GT-6144 card windows. Knobs: brightness /
  contrast / saturation / hue, spatial sharpness, phosphor persistence
  (temporal afterglow) + phosphor gamma, smooth anti-aliased scanlines, barrel
  curvature, procedural shadow mask (triad / aperture grille / dot, Lottes
  luminance-preserving triplet), center-lighting vignette and post-glass
  luminance gain. **On by default** (turn off with the master button in the
  window — an explicit off is remembered); all values persist to
  `ini/ui.settings` (`crt_*` keys). New sources: `CrtEffectStack` (GLSL pass, per-framebuffer
  ping-pong FBO rendered at on-screen resolution for analytic AA),
  `OpenGLShader` (portable GLSL 1.50 / ES 3.00 compile helper), `CrtParams`,
  `Pom1CrtEffects` (per-slot manager; single `apply()` call site returns the
  ImTextureID to draw). When active, the shader replaces the legacy ImGui
  scanline/phosphor overlays (no double-dipping); the monitor tint
  (Green/Amber/Mono) still applies on top. **OpenGL backends only** (Linux /
  Windows / WASM / macOS-GL); on the macOS Metal backend the stack stays inert
  and the raw framebuffer is presented unchanged.

### Fixed — hex-dump loader, card eviction, concurrency

- **Bundled demos loaded into zero page and never started** — most single-line
  Apple-1 demos (`mandelbrot-65`, `2048`, `cat`, `cellular`, `50th`, …) group
  data with a `:` every 8th byte, and `loadHexDump`'s colon-address guard had
  been loosened from ≥3 to ≥1 digit, so a trailing data byte (`5E`) was read as
  address `$005E`: the program scattered across hundreds of zones and the first
  `JMP` derailed. The ≥3-digit guard is restored (real dumps address with 4
  digits; page-zero targets use the unambiguous `040:` form) and pinned by a
  new `hex_dump_inline_colon` case. Every bundled game **and** demo now prints
  its title/first frame headlessly.
- **Card-eviction desyncs + concurrency hardening** (seven findings from a
  bug-hunt sweep of the load path, DevBench host and rewind layer): microSD
  eviction now drops the UI-side IEC flags too (phantom drive window,
  mis-cascading re-toggle); CodeTank eviction disarms a pending cold-boot
  `4000R` autorun so it can't fire against an unmapped bus; the DevBench build
  log snapshots the project context by value (use-after-scope);
  `RewindBuffer::evictToBudget` re-anchors a single over-budget segment instead
  of overshooting the budget; `getRewindStatus` clamps `currentPos` against a
  torn lock-free read; the emulation-thread wait uses the predicate overload
  (lost-wakeup window); `PeripheralBus::registerHandle` refuses a 33rd entry in
  release builds (`1u<<32` UB with the assert compiled out).

### Changed — text games fill the screen; CRT curvature on by default

- **Sokoban** renders only the level's bounding box instead of all 12 playfield
  rows — Microban #1 goes from 16 lines (9 of them blank) to ~7.
- **Chess** sits flush at the top of the 24-line display (the spare rows move
  below the board) and its anti-scroll clear drops from 24 to 6 CRs, so a
  redraw no longer scrolls a full screen height on the slow Apple-1 video.
- **Connect 4** draws its column separators with `!` instead of `|` — the
  Signetics 2513 char ROM only covers `$20-$5F`, so `|` rendered as `\`.
- **CRT barrel curvature defaults to 0.025** — a light bow that reads as glass
  without warping the 40-column text at the edges. Because a *saved* value
  always won over the compiled default, every install that had ever run POM1
  (and, on the web, every returning visitor through the IDBFS-persisted copy)
  kept its old CRT block forever. `ini/ui.settings` now carries a
  `settings_version`; a file without one predates 1.9.4, so its CRT block is
  dropped once and the current defaults apply. Later edits are written back
  with `settings_version=1` and never migrated again.
- **Profile chooser: the "always start with this profile" checkbox is gone** —
  redundant now that the startup preference lives in Settings and POM1 boots
  into Fantasy by default.

### Changed — 6502 software & repo hygiene

- **`hgr_blit2` dispatches the blit mode once per call, not per byte** — the
  mode decision is hoisted out of the inner loop into specialised unrolled row
  loops (one per width × mode): a 4-byte STORE row drops from ~250 to ~101
  cycles, a 28×32 tile from ~8000 to ~3300. ZP shrinks (`bl_a`/`bl_i` collapse
  into the walking pointer `bl_sp`), `bl_src` survives the call, and mode 3 =
  PALFLIP is added. Rogue + RogueX2 rebuild byte-identical.
- **GEN2 Snake clears a cell-aligned band behind GAME OVER** — the old 168×20
  px panel left half-cut blocks glued to the letters.
- `game_rogue_x2` declares its cross-sketch include dir (`incDirs`) so the
  DevBench resolves the x1 asset pack; documented in `doc/SKETCHS.md`.
- Bundled artefacts refreshed (`HGR_BBFontShow`, `HGR_Life`, `GEN2Bounces`),
  `Chess.bin.hi/.lo` dropped (the shipped form is `Chess.txt`), and the English
  **Intruder** build added to the games catalogue.
- Repo hygiene: `*.rawbin` build residue untracked, the stray
  `scratch_hgr/priestess_hgr.png` dropped, `screenshots/` ignored.

## [1.9.3] — 2026-07-22

### Fixed — pre-release review pass (2026-07-22)

- **GEN2 HGR: stale/black card window after snapshot load or paused rewind
  scrub** — the frame-atomic latch (the only thing the renderer reads) was
  never re-seeded on restore, so the display kept the pre-restore frame until
  the CPU completed a full frame (never, while paused). `readSnapshotSections`
  now re-seeds both latches from the restored RAM, and UI hex-editor pokes to
  `$2000-$5FFF` re-seed too so paused edits are visible immediately.
- **TMS9918 Paint/Sprite editors: "auto-plug" never actually plugged the
  card** — the menu/render paths set the UI flags but never called
  `setTMS9918Enabled(true)` (the deferred-plug countdown was never armed), so
  `$CC00/$CC01` stayed unmapped and the Graphic Card window stayed black while
  the editor canvas worked via `editorPokeVram`. All four sites now plug
  directly, mirroring the VDP Inspector.
- **HGR Sprite editor: undo replayed strokes in forward order** — a drag
  touching the same byte twice was only partially undone; the replay now walks
  the ops in reverse like the TMS editor.
- **HGR Sprite editor: Save PNG heap over-read on ×2 sprites larger than the
  page** — the crop now clamps to the rendered 280×192 page instead of reading
  up to 560×384 out of bounds.
- **TMS9918 Paint editor: "Clear page" wiped the sprite pattern bank** — the
  Graphics II clear ran to `$3FFF` instead of stopping at the colour table's
  end (`$37FF`), erasing every sprite shared with the TMS Sprite editor.
- **macOS Metal: per-frame autorelease leak** — `nextDrawable`/`commandBuffer`
  return autoreleased objects and the GLFW main loop never drains a pool, so
  each frame leaked a reference pinning a `CAMetalDrawable` (risking drawable
  starvation). `beginFrame` now wraps the acquisition in an
  `@autoreleasepool`.

### Added — previously unlogged user-visible work this cycle

- **Beeper SFX editor** (50-cue bank) and **SID Tracker editor** (built-in tune
  bank + live SID preview) — two new sound tools.
- **Buzzard Bait ported to the GEN2 HGR card**
  (`software/Graphic HGR/BuzzardBait.txt`), following the Apple II HGR port
  recipe (soft-switch remap + `$FCA8` WAIT + keyboard shim).
- **Fixed IJKL controls in every game** — the QWERTY/AZERTY layout selector is
  removed; all Apple-1 games now use the same IJKL movement keys.
- **Interactive Apple-1 keyboard photo gains functional CLEAR/RESET keycaps.**

### Fixed — previously unlogged first-launch / platform fixes this cycle

- **Linux AppImage now runs on glibc ≥ 2.27 distros** (e.g. Mint 19.x) — the
  release AppImage is built inside an Ubuntu 18.04 container.
- **Linux AppImage now integrates with AppImageLauncher again** — the packager
  was fetching the *new* `AppImage/appimagetool`, whose static-pie (ELF
  `ET_DYN`) runtime AppImageLauncher rejects with "AppImages of type -1 are
  currently not supported" (direct `./POM1…AppImage` launch was unaffected).
  Reverted to AppImageKit's `appimagetool`, which emits a classic `ET_EXEC`
  runtime + gzip squashfs; the packaged POM1 binary is byte-for-byte the same
  (glibc 2.27 floor unchanged).
- **Windows: VC++ runtime bundled app-local** so POM1 launches on a bare
  Windows without the redistributable installed.
- **macOS: ad-hoc codesigning of POM1.app** fixes Gatekeeper refusing the
  unsigned bundle.
- **WASM: window layouts survive page reloads** — `ini/` is an IDBFS mount
  flushed on `pagehide`, plus a debounced layout autosave (~2 s) on desktop so
  crashes lose seconds, not the session.
- **Undocumented multi-byte 6502 opcodes** now advance PC by their real NMOS
  operand length (dispatch + disassembler), fixing instruction-stream desync.
- **Release CI repaired (all three OS jobs were broken, run 29819799811)** —
  Linux: the bionic apt rewrite to `old-releases.ubuntu.com` now only happens
  when `archive.ubuntu.com` stops serving bionic (the mirrors flipped back);
  macOS: `macos-13` was retired by GitHub and queued forever → `macos-15-intel`,
  plus `timeout-minutes: 90` on every OS job; Windows: MSVC can't build the six
  POSIX-only (`mkdtemp`/`unistd.h`) native-BASIC tests → skipped on WIN32, and
  `GL_CLAMP_TO_EDGE` gets a fallback define (Windows' `<GL/gl.h>` is GL 1.1).

### Fixed — WAIT_VBLANK_SAFE coverage completed + hostile-F burn-gate pass (Claudio's 8-July silicon report)

- **Three shipped TMS9918 programs still carried an unbounded frame-flag
  poll** after the 9-July `WAIT_VBLANK_SAFE` hardening pass — the exact class
  that black-screened TMS_Rogue on Claudio's Replica-1: **TMS LOGO V2.6**
  (`erase_turtle`, on *every* visible REPL command — a hostile-F chip froze
  the interpreter on the first `FD`), **Nyan** (manual `BIT/BPL` spin in
  `main_loop`) and **demo_split**. All three now use the bounded macro.
  Negative proof: the pre-fix `Codetank_BASIC_LOGO.rom` under
  `--tms-frameflag-hostile` freezes at the first turtle command; the rebuilt
  one draws the full test scene.
- **The LOGO 16 KB bank was full to 2 bytes** — the +18 B macro delta is paid
  for by converting 27 `JSR x / RTS` pairs to `JMP x` tail-calls
  (cycle-identical, −27 B → 8 B free).
- **The Claudio burn gate now runs a `--tms-frameflag-hostile` boot pass per
  scenario** (`tools/verify_codetank_roms.py`) — the one real-silicon
  condition it never covered. 14/14 scenarios clean.
- New **`Codetank_GALAGADIAG.rom`** bisection probe (ROGUEDIAG pattern):
  Galaga's in-game player-ship sprite is missing on real silicon while POM1
  renders it under every modelled condition (strict + vram-noise +
  dram-refresh + ram-poison + hostile-F). The probe replays Galaga's exact
  video path in 4 key-advanced steps (init / static SAT / gated rebuild /
  ungated rebuild) so the first failing step pins the divergence.

### Added — TMS Chess file letters, Mandelbrot zones + colour cycle (Claudio's 8-July wishlist)

- **TMS_Chess**: file letters a-h now render as 5x6 grey micro-glyphs in the
  bottom-left cell of every rank-1 square (the full-height board has no room
  for a coordinate strip; piece bases never reach that cell's cols 0-5, and
  `cellcol_for` forces the glyph cell's fg to grey so it reads on empty and
  occupied squares alike).
- **TMS_Mandel**: after the render completes the escape bands **colour-cycle
  forever** (the whole Mode-2 colour table remapped through a boot-built
  256-byte permutation table, ~0.5 s per pass — in-set black stays fixed so
  only the bands crawl); any non-ESC key then renders the **next zone of the
  set** (full view -> seahorse valley -> elephant valley -> period-3 bulb ->
  wrap, Q8.8-precision-capped at 3x zoom). Viewport constants became
  per-zone variables; ZP being full, the new state lives in the free `$0F00`
  page. Standalone `TMS_Mandel.txt` / `TMS_Logo_16k.txt` / `TMS_Split.txt`
  regenerated from the fixed sources.

### Added — Rogue ported to the GEN2 HGR card (`sketchs/gen2/game_rogue/`)

- **`HGR_Rogue`** — full port of the TMS9918 roguelike to Uncle Bernie's GEN2
  HGR card (280×192 bitmap, no name table / hardware sprites). The game logic
  (dungeon gen, shadowcasting FOV, monster AI, combat, inventory, buffs, pits,
  depth-13 boss) is carried over verbatim from `TMS_Rogue.asm`; the video
  layer is rewritten: 14×16 px tiles (2 HGR bytes × 16 rows), soft OR-blitted
  entities with an inverted-box hurt flash, byte-aligned `bbfont_ascii5f`
  text behind a TMS-compat layer (`WRT_DATA_*` → `hgr_emit_a` emulating the
  VDP auto-increment cursor — ~5000 logic lines assemble untouched), and a
  dirty-tracked repaint (`vis | prev_vis`) so a turn only redraws the cells
  that changed. Single-region image at `$6000` (chess model, preset 11,
  `6000R`); map + pools move from the Parmigiani `$E000` high bank to
  `$0280-$046F`. Assets (8 tiles + 14 sprites + 28×32 boss) are generated
  from the TMS art by the new `tools/build_rogue_hgr_assets.py`. Artefacts:
  `software/Graphic HGR/HGR_Rogue.{bin,txt}`.

### Added — HGR_RogueX2: the ×2 colour variant (`sketchs/gen2/game_rogue_x2/`)

- Alternative build of the Rogue port with the whole playfield at **×2 in
  colour** — 28×32 px cells, coloured tiles/sprites, 56×64 px demon — behind
  an **8×5-cell dead-zone camera** (the 16×10 map at ×2 exceeds HGR's width;
  the window scrolls only near its edges so the delta renderer keeps
  working; a camera move forces the full repaint). Colours are baked into a
  second generated asset pack (`rogue_assets_hgr_x2.inc`, ×2 section of
  `tools/build_rogue_hgr_assets.py`): parity masks + palette bit applied at
  generation over the doubled pixels — pure NTSC colours at zero runtime
  cost (hero white, undead green, ghost violet, skeleton blue, death/boss
  orange, stairs green, door orange, pit violet). The 4 bottom HUD rows are
  byte-identical to the ×1 build (×1 icons kept for HUD + inventory modal);
  the empty-bag modal and the GAME OVER screen hash-match the ×1 build.
  Second pass: the camera is **centre-locked** (the hero pins viewport cell
  (3,2), the world scrolls, off-map border rendered black once per move);
  **hits flash by palette-bit flip** — new generic `hgr_blit2` mode 3
  (PALFLIP, EOR #$80 + OR, pinned by t15): orange demon flashes green, blue
  hero flashes violet; `hgr_text8` gained optional artifact-colour
  attributes (`ht_cm_*`/`ht_cbit`, white pass-through, glyph bit 7 now
  always stripped — ×1 games re-pinned hash-identical) used for a
  colour-coded HUD (DEPTH orange / ATK-DEF blue / HP green / XP violet,
  white restored for prompts+modals) and a **×2 orange "ROGUE" title
  banner** (`putc8_x2` doubling the HGR-order bbfont via `dblnib`) over a
  green title body; sprite palette enriched (hero blue as on TMS, zombie
  green, skeleton white, sword blue). Third pass: **true double
  buffering** — turns render to the hidden HGR page and flip; the dirty
  snapshots are kept per page and swapped at flip (delta formula
  untouched), the HUD repaints once per page (`hud_again`), the off-map
  border blanks once per page (`vp_force` countdown), modals/prompts draw
  on the visible page. The lib blit + text engines gained an `EOR`-based
  page selector (`bl_page`/`ht_page`, $00/$60 — `$2x EOR $60 = $4x`; ×1
  games re-pinned hash-identical with selectors at 0).
  Artefacts: `software/Graphic HGR/HGR_RogueX2.{bin,txt}`.

### Changed — GEN2 HGR ports: repaint elision (2026-07-13)

- **`HGR_Rogue`** — three classes of useless rewrites removed, pixel-parity
  pinned (the 5 deterministic reference frames hash-identical, boss-fight
  death screen bit-identical to the pre-optimisation build):
  (1) the tile dirty set becomes `(vis XOR prev) | ent_prev | ent_now` — FOV
  ring delta + entity cells from a FOV-gated pre-scan — instead of the whole
  lit union (~12-18 cells per move instead of ~35; `force_dirty_all` now
  forces via `ent_prev`, the XOR formula's force channel); (2) `update_hud`
  caches the 11 displayed values and skips its ~130-glyph + 5-icon repaint
  when clean (`hud_msg` prompt flag + `hud_force` set by `clear_name_table`
  keep it correct; it owns the rows-22/23 wipe now); (3) the dagger-flight
  frames use the dirty repaint (~2 cells) instead of `redraw_game`'s forced
  full pass — flight pacing restored to ~TMS speed (48-lap delay). Typical
  quiet turn: ~170k cycles of repaint down to ~35k.
- **`HGR_Maze3D`** — `vdp_display_off/on` are real again: "off" flips the
  display to HGR page 2 (zeroed at boot), "on" flips back — every full
  redraw (3D frame, map, combat, title) happens off-screen, restoring the
  TMS blank-during-redraw UX for two soft-switch reads. Combat rounds no
  longer rebuild the whole screen: `run_combat`'s attack and failed-flee
  paths repaint only the two HP fields in place (`combat_update_hp`, 4
  digit cells) and loop for the next key — the full `draw_combat_screen`
  (clear + ×4 portrait + labels, ~140k cycles) fires only on combat entry
  and next-foe transitions where the name/portrait genuinely change.
  Reference hashes unchanged.

### Changed — dev/ clean-up sweep (2026-07-12)

- **Wozmon-shim duplication is now drift-gated instead of merged**:
  `tools/check_wozmon_shims.py` (wired into `make -C dev/lib check`) asserts
  the shared routines/equates of `apple1c/apple1io_asm.s` and
  `tms9918c/apple1_asm.s` stay instruction-identical — the historical hazard
  was the diverging Wozmon entry ($FF1F vs $FF1A, unified June 2026). A
  physical merge was declined on purpose: the DevBench copies build sources
  by basename (relative `.include` breaks in-Bench builds) and the tms9918c
  path is pinned by the bench spec, `Pom1BenchHost.cpp` and
  `build_codetank_rom.py`. `tms9918c/apple1.c`'s stale header comment
  (pre-unification) rewritten to document the decision; TODO6502's
  "single-source the shim" item resolved accordingly.
- **`dev/cc65/README.md`** documents the previously unlisted
  `apple1_tmsutil.cfg` ($0300 microSD utilities — `tool_tmsload`/`tool_diapo`)
  and modernises the "GEN2 HGR asm > 4 KB" advice (single-region $6000 chess
  model OR dual-bank split). **`dev/lib/tms9918/README.md`** documents the
  generated `c64font_tms.inc`.
- Local build artefacts purged from `dev/` (`__pycache__`, stray `.o`) — they
  are gitignored but were bloating the **WASM MEMFS bundle** (`POM1.data`
  shipped `.pyc` + ~20 `.o`); a packaging-side exclusion filter is now a
  TODO.md item. Audit false-positives cleared: `tms9918_text/console`,
  `sprite_triangle/helpers`, `repeat.asm`, `delay.asm`, `print_num.asm` all
  have consumers or are deliberate documented lib surface — no source
  deletions were warranted.

### Added — dev/lib/gen2 byte-aligned HGR building blocks + micro-tests

- The private text/blit copies the GEN2 ports grew are factored into shared
  lib modules: **`hgr_text8.asm`** (`hgr_putc8`/`hgr_puts8` — 8×8 glyphs,
  VDP-style cursor wrap, caller font in either bit order, preserves A/X/Y),
  **`hgr_blit2.asm`** (`hgr_blit2`/`hgr_blit4` — 2-/4-byte-wide rect blits,
  OR / inverted-FLASH / STORE, X untouched), and **`rev7.inc`** (the shared
  TMS→HGR bit-order table, split out of `hgr_sprite16.asm`). `HGR_Rogue`
  (emitter + blit engine) and `HGR_Maze3D` (`write_char`) now consume the
  modules — migrations verified **pixel-identical** by frame-hash on
  deterministic paths (Rogue title/boss/help, Maze3D title/combat).
  Remaining migration candidates (`GEN2_Chess` `putc_hgr`, `game_sokoban`
  `draw_tile`, `demo_bestiary`'s doubler) are tracked in `dev/TODO6502.md`.
- New micro-tests **t14** (`hgr_sprite16`: bit-stream repack x1/x2 + WHITE/
  GREEN artifact-colour attributes against simulator-derived framebuffer
  bytes) and **t15** (`hgr_text8` both bit orders + cursor wrap + A/X/Y
  preservation; `hgr_blit2/4` STORE/FLASH) — `tools/test_lib_micro.py` now
  15/15, gen2 added to its include path.
- Doc refresh: `dev/README.md` (dead `projects/` row → `codetank/`, `bench/`
  row added), `dev/TODO6502.md` (2026-07-12 sweep: shipped 4-cart CodeTank
  item removed, GEN2-port modules noted, convergence items added).

### Added — Maze3D ported to the GEN2 HGR card (`sketchs/gen2/game_maze3d/`)

- **`HGR_Maze3D`** — full port of the Wizardry-style 3D line maze to the GEN2
  HGR card. The game logic (DFS maze, pseudo-3D wireframe renderer, map view,
  HUD, narrator, turn-based combat) is carried over verbatim from
  `TMS_Maze3D.asm`; only the Graphics II bitmap *primitives* were swapped for
  HGR twins with identical contracts (`calc_pix_addr`/`plot_set`/`hline`/
  `vline`/`write_char`/`x2_tile`/`draw_sprite16_x1/x2/x4`/clears — colour-table
  routines become stubs). Pixel mapping: each 8-px TMS byte column lands on one
  7-px HGR byte column via a 256-entry `rev7_tab` (bit-order flip, rightmost
  pixel dropped; per-pixel plots clamp x%8==7 onto bit 6 so wall edges
  survive). Monster patterns link straight from the TMS sprite libs — no asset
  conversion step. Single-region image at `$6000` (preset 11, `6000R`), state
  kept at the CodeTank build's `$0E00` segments. The 1-px wireframe edges pick
  up NTSC artifact colour (green/violet) as a free depth cue, and the monsters
  keep their TMS archetype tints via artifact colour in the blitter
  (pixel-parity mask + palette bit): goblin green, orc orange, dark mage violet
  — title mascot, corridor clusters and the ×4 combat portrait included; the
  ×2 "MAZE 3D" title renders orange (the HGR red). The sprite pipeline is promoted to a
  shared lib module — **`dev/lib/gen2/hgr_sprite16.asm`** (`hgr_spr16_x1/_x2/
  _x4` + `hgr_spr16_color_a`, `HSPR_*` colour codes): TMS-format 16×16
  patterns, each output row built as a pixel bit-stream and repacked
  7 px/HGR-byte — lossless, after the first byte-column mapping (one dropped
  column per source byte) visibly skewed the ×2 goblin. Artefacts:
  `software/Graphic HGR/HGR_Maze3D.{bin,txt}`.

### Changed — CodeTank ROM library: four release cartridges (Claudio burn plan)

- **The GAME1-7 line-up is reorganised into 4 named cartridges** so Claudio's
  four EPROM burns cover the whole library (silicon-validated titles ride the
  same chips as the remediated/new banks he must test):
  **`Codetank_CLASSICS.rom`** (Tetris ✅ | Chess 🆕),
  **`Codetank_BASIC_LOGO.rom`** (LOGO V2.6 ✅ | Applesoft TMS 🆕),
  **`Codetank_ARCADE.rom`** (menu Galaga 🔴/Sokoban ✅/Snake ✅ | Rogue 🔴),
  **`Codetank_DEMOS.rom`** (menu Life ✅/Mandel 🔴/Plasma ✅/Vague 🆕/Nyan ✅ |
  Animals 🆕). The DEMOS lower bank packs all five small demos behind a new
  5-entry menu (`dev/projects/codetank/demos_menu/`, slots $4200/$4A00/$5200/
  $5A00/$6000 pinned by new `*_demos_bank.cfg`s); Nyan is slot-linked at
  $6000, Animals keeps its full-bank C build. TMS9918_Hello and TMS_Split
  leave the cartridge line-up (DevBench sketches only); the GAME5/6 packer
  machinery left `build_codetank_rom.py`. `TMS_Mandel.asm` (remediated build)
  was restored from git history — its source had left the tree while its bank
  still shipped. ARCADE is the default probe rom (`Memory.cpp`, presets).
  `tools/verify_codetank_roms.py` scenarios rewritten for the 4 carts —
  **Chess, Applesoft, Vague-from-menu and Nyan-from-menu are now covered by
  the Claudio gate** (Chess/GAME7 previously had no scenario at all).
- **`CODETANKDEV.rom` is now a pure two-slot flash cartridge — generated,
  never committed** (untracked + .gitignore). Both 16 kB banks are blank $FF
  flash slots; `flashCodeTankDevRom` composes the file from scratch when
  absent (desktop and WASM MEMFS alike, no toolchain needed — packagers call
  `--rom dev` unconditionally now). The Applesoft TMS bank it used to carry
  ships stabilised in `Codetank_BASIC_LOGO.rom`, and the DevBench injection
  paths for **both** TMS interpreters (Applesoft upper / LOGO lower) load
  that cartridge (`Pom1BenchHost.cpp`; tests updated:
  `applesoft_tms9918_smoke`, `applesoft_gen2_smoke`, `basic_compiler_smoke`,
  `bench_logo_inject_smoke`, `codetank_smoke`).
- **DevBench flash-bank picker**: CodeTank asm/C targets get an "Upper"
  toggle in the bench toolbar (new `IBenchHost::flashBankApplies`/
  `flashUpperBank` seam, default hidden) — Run flashes the chosen 16 kB half
  of CODETANKDEV and boots the matching board jumper, preserving the other
  bank's program across flashes.

### Fixed — boot profile chooser (bug-hunt pass)

- **The chooser's LOGO buttons were dead**: they passed machine indices 9/10
  into `targetFor()` after the `kP1Machines[]` compaction moved LOGO to rows
  7/8, so both buttons resolved to target -1. Machine rows consumed by name
  are now the `kP1Machine*` constants (`Pom1BenchHost.h`) with a size
  static_assert on the array.
- **Beeper SFX from the chooser vs `--disable aci`**: the Beeper branch now
  re-asserts `aciEnabled`/`pendingAciEnable` after `applyBootConfig` (like
  the SID branch) — the persistent CLI override used to clear the pending
  plug and force the editor's same-frame emergency plug (the documented
  silent-card-on-boot condition).
- **Tools → SID Tracker left a phantom Juke-Box checkmark**: the handler now
  clears `jukeBoxEnabled` like the Hardware-menu A1-SID item —
  `Memory::setSIDEnabled` already evicted the card on the bus ($CA00 sits in
  the SID window), so the stale UI flag even produced a bogus conflict row in
  the Silicon Strict Inspector.

### Added — Terminal Card: `Ctrl-K` injection hand-over

- **`Ctrl-K` (or `ESC K`) suspends/resumes keyboard injection** on the Terminal
  Card. While suspended, incoming TCP **data** bytes are dropped so the local POM1
  keyboard drives the Apple 1 — useful once a script has bootstrapped a program and
  you want to play — **without dropping the session**. It's a symmetric toggle: a
  second `Ctrl-K` re-attaches. Control commands (incl. `Ctrl-K` itself) still bite
  while suspended, and `Ctrl-K` is an escape hatch like `Ctrl-T` (works in 8-bit raw
  mode too). New `injectionSuspended` state (next to `eightBitMode`), surfaced in the
  Terminal Card hardware window + the client welcome banner. Pinned by
  `terminal_card_injection_smoke`. *(The TODO sketched re-attach via `Ctrl-T`, but
  that byte already toggles 8-bit mode — a symmetric `Ctrl-K` avoids overloading it.)*

### Added — Bench BASIC: Verify loads (ready to LIST) + cold/warm start toggle

- **Verify now LOADS the tokenised program into the live interpreter, ready to
  `LIST`** (instead of a host-side compile-check that touched no machine state).
  `Pom1BenchHost::injectBasic(run=false)` cold-starts the interpreter, pokes the
  image + pointers, and enters at the *prompt* rather than running: Integer BASIC
  via its warm start `$E2B3`, Applesoft by rewriting the launcher's trailing
  `JMP NEWSTT` → `JMP <warm>` (`$6003`/`$9803`/`$E003`/`$4003`) after `JSR SETPTRS`
  installs the pointers. So the program is present + `LIST`able and nothing runs;
  Run (`run=true`) is unchanged (loads + launches). All five BASIC targets.
- **Cold/warm start toggle** — a "Warm" checkbox in the Bench toolbar (shown only
  for BASIC targets, default **cold**). Warm re-enters the resident interpreter
  through its warm entry (`E2B3R`/`6003R`) and skips the hard-reset + ROM reload,
  so a program already typed at the REPL survives a Verify/Run; cold (`E000R`/
  `6000R`) is the classic clean boot. Warm is honoured only when that interpreter
  is actually resident (a preset switch or any non-BASIC build clears the residency
  flag), so it never warm-enters unmapped RAM. New portable seam
  `IBenchHost::warmStartApplies/warmStart/setWarmStart`.

### Added — native BASIC compiler: `ATN` + `RND`

- **`ATN(x)` and `RND(x)` now compile natively** (`BasicCompilerApplesoft` +
  `dev/lib/basicrt/basicrt_float.s`). `fp_atn` is a two-stage range reduction
  (`atan(1/x)` reciprocal fold + a `pi/6` offset fold to `|t| ≤ tan(π/12)`) then a
  4-term odd Taylor, built entirely on the existing `fp_*` core; `fp_rand` advances
  an xorshift32 state and forms the mantissa of a `[1,2)` single, minus 1 → `[0,1)`
  (its argument is evaluated then discarded — Applesoft's `RND(0)`/`RND(<0)` are not
  modelled). Both are feature-gated (`-D FP_ATN` / `-D FP_RAND`) so a program links
  them only when used, and either one auto-selects the float phase. The shared
  `LDF`/`CPF` float-constant macros moved to file scope so a routine can use them
  without `FP_SIN`. Pinned: `basic_float_runtime` (5000-point ATN grid vs `atanf`,
  RND range + non-degeneracy), `basic_native_codegen` (emits `jsr fp_atn`/`fp_rand`),
  `basic_native_run` (end-to-end arctan-curve + RND-scatter native programs). Doc:
  [`BASIC_COMPILER.md`](doc/BASIC_COMPILER.md).

### Fixed — documented `AND`/`OR` semantics divergence (not a tokeniser bug)

- **Investigated a reported "`IF (X AND 7)=0` freezes the interpreter" bug: it is a
  misdiagnosis.** The tokeniser emits bytes byte-identical to the interpreter ROM's
  own CRUNCH (verified end-to-end on the real ROM; new byte-exact regression in
  `basic_compiler_tokenize`). The real cause is inherent Applesoft semantics: `AND`/
  `OR` are **logical** (nonzero→1) in the interpreter but **bitwise** in the native
  compiler, so a bit-mask idiom loops forever under Inject yet works under Compile —
  a genuine per-mode divergence surfaced by the new Inject/Compile toggle. Documented
  in [`BASIC_COMPILER.md`](doc/BASIC_COMPILER.md) (no code change — both modes are
  correct, just different).

### Added — DevBench BASIC: explicit "Inject | Compile" mode toggle

- **The *New* dialog now exposes inject-vs-native-compile as a segmented Mode
  toggle** on the Applesoft BASIC row (`bench/CodeBench.cpp` `drawNewDialog`),
  instead of two look-alike "(native compile)" pseudo-machines in the Target combo.
  **Inject (interpreter)** = the ahead-of-time tokeniser (`BasicTokeniserApplesoft`,
  runs on the resident ROM); **Compile (native)** = the standalone 6502 codegen
  (`BasicCompilerApplesoft`, no interpreter, ~20× faster, `$0300`). The toggle
  shows only for the two machines with a native compiler (Applesoft GEN2 / TMS9918)
  and is **desktop-only** — WASM (no cc65) collapses to Inject; microSD / CFFA1 /
  Integer BASIC stay inject-only. The status-bar *Mode* switcher gained a matching
  indented "Compile (native)" row so native stays reachable in place.
- **New host seam `IBenchHost::nativeSiblingOf(target)`** (default −1, overridden in
  `Pom1BenchHost`: inject 9 → native 12, inject 11 → native 13, −1 on WASM) — the
  portable `bench/` module surfaces the toggle without knowing about card-specific
  compilers. The two `kP1Machines` "native compile" rows are gone; the native
  targets (12/13) and their `mode 5` dispatch are unchanged, so all
  `basic_native_*` pins keep passing. Doc: [`DEVBENCH.md`](doc/DEVBENCH.md).

### Added — HiDPI UI font scaling (Linux / Windows)

- **The UI font auto-scales to the monitor's DPI on startup** (`main_imgui.cpp`,
  `glfwGetWindowContentScale`, GLFW 3.3+): on Linux/X11 and Windows — where GLFW
  does not scale the framebuffer — a high-DPI monitor no longer renders the whole
  UI tiny. macOS (Retina handled by `io.DisplayFramebufferScale`) and WASM (the
  browser owns devicePixelRatio) are deliberately left untouched. **Display
  Settings** gains an *Auto (follow monitor DPI)* toggle + a manual **UI font
  scale** slider (0.75–3.0×) driving `io.FontGlobalScale`, replacing the manual
  poke it documented.

### Added — DevBench editor polish + GEN2 default = OpenEmulator composite

- **Markdown syntax highlighting in the editor's Edit mode** (`bench/BenchLang.cpp`
  `langMarkdown()`, routed via `langDef("markdown")` — was plain text): a light
  regex "pencil" for `#` headings, `**bold**`/`__bold__`, `*italic*`/`_italic_`,
  `` `code` `` and `[text](url)` links. Preview still renders the real formatting.
- **Unsaved-close guard** (`CodeBench`): closing a dirty tab (X, context Close, or
  Close others/all when any tab is dirty) now pauses on a **Discard / Cancel** modal
  (Discard in red, `Esc` = Cancel) instead of silently throwing away edits.
- **GEN2 HGR now renders with the OpenEmulator composite NTSC decode by default**
  (`MainWindow_ImGui.h` `gen2RenderMode = 1`) — the more faithful decode is what you
  see on first boot. The MAME artifact LUT stays a click away in the GEN2 menu, and
  remains the `GraphicsCard` standalone default so `gfx_regress_gen2` keeps its
  reference image.

### Fixed — stray Xlib clipboard error no longer crashes the app (Linux/X11)

- A raw Xlib protocol error (classically a clipboard `SelectionRequest` racing a
  requestor whose window has already vanished → `X_ChangeProperty` `BadWindow`)
  bypassed GLFW's error callback and hit Xlib's **default handler, which calls
  `exit()`** — taking the whole emulator down mid-session. POM1 now installs a
  non-fatal Xlib error handler at startup (`src/X11ErrorGuard.{h,cpp}`,
  `pom1InstallX11ErrorGuard()` right after `glfwInit`) that logs the decoded error
  and returns instead of exiting. All Xlib inclusion is isolated in one TU so its
  macro soup never leaks; it is a no-op on macOS/Windows/WASM and on Linux builds
  where `find_package(X11)` fails (headless/Wayland-only).

### Added — LOGO listing injection in the DevBench (4th language)

- **The Bench now writes + runs LOGO turtle programs**, not just the interpreter's
  asm build. LOGO is the 4th *New*-dialog language (after asm / C / BASIC) with two
  interpreter targets calqued on the Applesoft GEN2/TMS path: **LOGO TMS9918**
  (`Codetank_GAME3.rom` lower bank, `$4000`, cold `4000R`, 8 KB Parmigiani dual-bank)
  and **LOGO GEN2 HGR** (`roms/logo-gen2.rom`, `$6000`, cold `6000R`, preset 2). Both
  cold-start the resident **APPLE-1 LOGO V2.6** interpreter and draw the turtle live.
- **`LogoProgramLoader` (`src/LogoProgramLoader.{h,cpp}`, pure C++/WASM-safe)** parses
  a listing into the interpreter's on-chip layout and `Pom1BenchHost::injectLogo`
  (mode 6) pokes it. Unlike Applesoft there is **no tokenised image**: a LOGO
  procedure is stored as **raw ASCII source** in `proc_table` (244-byte slots — name,
  params, `body_len`, CR-separated body), so the loader emits `proc_table` writes +
  `n_procs`, cold-starts to the `?` prompt, `writeMemoryBatch`-pokes the table while
  the CPU is parked, queues **only one entry line** and resumes the REPL. Feeding a
  single line dodges the `REPEAT` break-poll type-ahead drop — procedure bodies never
  travel the keyboard. **Verify** parses host-side without disturbing the machine.
- Syntax colouring `langLogo()` (`bench/BenchLang.cpp`), `.logo` extension routing,
  starters, hints/tooltips, and `bench_logo_inject_smoke` — pokes a nested-`REPEAT`
  rosette procedure, calls it by name and asserts the turtle lights both framebuffers
  (TMS VRAM pattern table + GEN2 HGR page-1 RAM), pinning `proc_table` `$E431`/`$B431`
  + `n_procs` `$0260`/`$02E3` against an interpreter relink. Frozen `roms/logo-gen2.rom`
  (byte-identical to a fresh `CODETANK_BUILD`+`LOGO_GEN2` build of `GEN2Logo.bin`).
- **10 machine-neutral `.logo` sketches** in `sketchs/logo/` (rosette, polygons,
  stars, recursive flower/tree, random rays/meadow — canonical LOGO-manual §11
  tutorials), the counterpart of `sketchs/basic_applesoft/`; each runs unchanged on
  both the TMS9918 and GEN2 turtle and was verified drawing through the real
  interpreter. Dialect: turns are `TR`/`TL` (not `RT`/`LT`), nested `REPEAT` one deep.
- **Interactive LOGO REPL** after Run: because the interpreter stays resident at its
  prompt, the Bench now shows a one-line input below the console (new portable seam
  `IBenchHost::replActive()`/`replSend()`, gated by `Pom1BenchHost::logoReplActive_`).
  Typed lines are fed to the live REPL over the keyboard FIFO **one at a time** (so a
  paste can't trip the `REPEAT` break-poll), with Up/Down command history, and echoed
  into the console as a record of what was sent. The turtle draws on the card window;
  the interpreter's own text (prompt, `OK`, `PRINT`, errors) stays on the Apple-1
  screen window. Drive the turtle, call or (re)define procedures without a re-cold-
  start. Flag cleared whenever a non-LOGO target reprograms the machine.

### Added — GEN2 video journal survives save-state / rewind (snapshot v5)

- **The GEN2 soft-switch video journal now enters the serialized snapshot.** The
  per-frame `(cycle, kind, value)` journal that drives the beam-raced renderer
  (mid-line PAGE2/TEXT/HIRES flips — DROL-class double-buffering, horizontal
  splits) was published into the live in-memory `EmulationSnapshot` but *dropped*
  from the save-state / rewind blob: the load path called
  `resetGen2VideoEventJournal()` and rebuilt from the bare end-of-frame latch, so
  a beam-split scene restored mid-frame lost its per-line flips until the program
  toggled a switch again. The `GEN2VID` section now serializes the **published
  journal + that frame's start state**; on load they are restored (after the
  recording half is cleared and rebased to the restored latch). Event `emuCycle`s
  are absolute and the renderer maps them modulo the frame, so they stay valid
  against the restored cycle counter. This is the first slice of the TODO's
  "journal enters the snapshot" (TMS9918 adoption + shared `BeamClock` replay
  still open).
- **Snapshot format bumped v4 → v5.** The new journal fields are gated on
  `r.version() >= 5`; pre-v5 snapshots read no journal (the section's length
  prefix realigns) and fall back to the prior end-of-frame-latch behaviour, so
  old save-states still load. A forged event count (> `kGen2MaxEventsPerFrame`)
  is rejected before allocation.
- Pinned by a new round-trip case in `snapshot_smoke` (journal two soft-switch
  flips, publish across a frame boundary, save/load, assert every event + the
  frame-start state survives).

### Added — HGR Sprite editor: monochrome ×2 colour + side-by-side B&W / colour view

- **Reliable single-colour ×2 sprites** (`hgrsprite::magnifyColor2x`, pure +
  pinned in `hgr_sprite_blit_smoke`). On real HGR a ×2-magnified sprite lights two
  adjacent dots per source pixel, which NTSC reads as solid **white**. To get
  colour, each source pixel is doubled into a 2-aligned **colour clock**: only one
  dot of the pair is lit (even → Violet/Blue, odd → Green/Orange) plus the byte's
  **palette high bit** (MSB) selecting the group — so the whole ×2 sprite reads as
  one chosen artifact colour instead of white. The editor is monochrome by design:
  the shape is drawn in black & white and the sprite takes a **single** colour.
  Pinned by a new smoke case that stamps the doubled bytes and reads every hue
  back through the decoder.
- **Sprite editor rework**: the pencil/fill now author a pure **B&W shape**; the
  palette became **"Sprite colour (×2)"** — one colour for the whole sprite,
  **active only in ×2** (two ×1 dots are white, so colour needs the doubled
  clock). `buildSpriteBytes` produces the mono ×1 bytes or the single-colour
  doubled ×2 bytes, used by Stamp and the previews.
- **Dual display**: the editor now shows the **black-&-white shape canvas** and a
  read-only **NTSC colour view** of the same sprite side by side (decoded through
  the real GEN2 pipeline), so the colour clocks and hue bascules are legible while
  editing. The live-page preview also composites the sprite's real bytes and
  decodes through NTSC (was a swatch overlay).

### Added — GEN2 HGR: OpenEmulator composite NTSC decode on the CPU (Phase 4)

- **`GraphicsCard::RenderMode::CompositeOECpu`** — a second HIRES colour
  pipeline beside the default MAME artifact-colour LUT (which stays the
  fast-path v1). Instead of the 128-entry table it builds the 14.318 MHz
  composite signal from the *same* doubled-word HGR bitstream
  (`buildHgrWordRow`), then runs OpenEmulator's NTSC demodulator on the CPU:
  a symmetric **17-tap FIR** (luma ≈ 2.0 MHz, chroma ≈ 0.6 MHz), synchronous
  sin/cos demodulation against the 4-phase colour subcarrier, and the
  OpenEmulator YUV→RGB matrix. The 560 demodulated sub-pixels are pair-averaged
  down to the shared **280×192** RGBA buffer (no 560-wide framebuffer, no DHGR —
  GEN2 has neither), so the texture seam and every downstream path (monitor
  tint, phosphor persistence, beam-race splits, fast-path diff) are unchanged
  and the toggle is free. Port of POM2's `Apple2Display::renderCompositeOeCpu()`;
  pure CPU (no GLSL) so it works identically on WASM and desktop.
- **Verified against the original OpenEmulator source** (not just POM2's copy):
  the YUV→RGB coefficients (`1.139883 / -0.394642 / -0.580622 / 2.032062`) are
  verbatim from libemulation `OpenGLCanvas.cpp`, and the kernels are the exact
  result of its `chebyshevWindow(17, 50) × lanczosWindow(17, …)` recipe with
  luma normalised to sum 1 and chroma normalised **×2** (the demod gain).
- **Grafts at `rasterizeHgrLine`**: the composite branch replaces only the
  "560 sub-pixels" stage; `forEachBeamSegment` / `renderInternalSegment` /
  TEXT / LORES / the legacy fast path are reused as-is (composite affects the
  HGR artifact-colour decode only). GEN2 has no DHGR, so the per-frame
  `signalPhaseOffset` is always 0 (phase = sample index mod 4).
- **UI**: a "NTSC render" combo in the GEN2 window's right-click popup —
  "NTSC MAME (actuel)" vs "Composite OpenEmulator CPU", applied each frame via
  `setRenderMode`. Pinned by **`gen2_composite_smoke`** (achromatic-bright
  white, chromatic `$55/$2A` violet, black-on-empty, output distinct from the
  LUT, repaint on mode toggle). The golden-image `gfx_regress_gen2_testcard`
  stays byte-identical (default LUT path untouched).

### Fixed — 6502 software: Galaga title/help SAT rebuilt during active display

- `draw_title_sprites` / `draw_help_sprites`
  (`sketchs/tms9918/game_galaga/TMS_Galaga.asm`) rebuilt the Sprite Attribute
  Table (`$1B00`, ~20 bytes) during **active display with no `WAIT_VBLANK`
  gate** — and `title_wait_key` re-invoked `draw_title_sprites` on every
  keyboard poll — so the raster scanned the SAT mid-rewrite, tearing/garbling
  the title and help aliens on real TMS9918A silicon (matching Claudio
  Parmigiani's "Galaga still broken" report on his Replica-1). The in-game
  `render_sprites` path was already correctly VBlank-gated; the pad18 timing
  fix alone did not cover this. Both routines now open with `WAIT_VBLANK` +
  the pad18 cross-boundary cushion, mirroring `render_sprites`. Costs +16 B
  and still fits the `$4100-$61FF` CodeTank bank (170 B headroom); reship by
  rebuilding `Codetank_GAME1.rom` (needs the external `tetris_codetank.bin`
  lower-bank drop-in).

### Added — Renderer abstraction, macOS Metal backend & OS-native file dialogs

- **`PomRenderer` graphics-backend seam** (`src/PomRenderer.h` + `_GL.cpp` /
  `_Metal.mm` / `_Internal.h`): a single opaque renderer interface
  (`createTexture` / `updateTexture` / `destroyTexture` / `beginFrame` / `clear`
  / `renderDrawData` / `present` / `readBackbufferRGBA` / ImGui backend init).
  Every texture site in the codebase — Screen_ImGui glyph atlas + native-res
  framebuffer, GEN2 HGR / TMS9918 / GT-6144 framebuffers, the 10 Help→Photos
  textures incl. the interactive keyboard, and the HGR/TMS Paint editor canvases
  — now routes through it; no `gl*` calls survive outside `PomRenderer_GL.cpp`.
  Selected at configure time via the `POM1_RENDERER` cache option. Delivered as
  Phase 1 (abstraction) → Phase 3 (HGR/TMS editors adopt the seam). This also
  fulfils the **Shared video texture layer** item.
- **macOS Metal backend** (`PomRenderer_Metal.mm`): `CAMetalLayer` on GLFW's
  NSWindow, BGRA8 drawables (`framebufferOnly = NO` so the screenshot blit reads
  them), `@autoreleasepool` per frame. **Default renderer on macOS-non-WASM**;
  OpenGL stays the default on Linux/Windows/WASM and remains available on macOS
  via `-DPOM1_RENDERER=opengl`. The upstream `imgui_impl_metal` inline sampler is
  patched linear→nearest at configure time so pixel-art framebuffers/glyphs/
  canvases stay crisp. Fulfils the **ImGui Metal backend on macOS** item.
- **OS-native file dialogs** (`src/NativeFileDialog.{h,cpp}` + `_Mac.mm`): Load/
  Save Memory, Tape, and Snapshot (plus the cassette deck buttons and the HGR/
  TMS Paint editors + DevBench) open the platform's localised file picker —
  Win32 `GetOpenFileNameW`/`GetSaveFileNameW`, Cocoa `NSOpen/NSSavePanel`, Linux
  probing `$PATH` for `zenity` then `kdialog`. WASM and Linux-without-either fall
  back to the existing in-app ImGui browser; the portable editor seams keep a
  `pickFilePath(...)` that defaults to `false` so the modules stay standalone.
  Fulfils the **Native file dialog** item.

### Added — TMS9918 sub-scanline beam/CPU synchronisation

- **renderUpToBeam + write catch-up**: `renderActiveLine` is split into
  `renderLineToTemp` + `commitActiveSegment`, and the live raster commits only
  the horizontal slice the beam has crossed. The `Memory` MMIO **write** hook
  calls `TMS9918::renderBeamCatchUp(inFlight)` before each register/VRAM
  mutation, with the in-flight offset = `cpu->getCurrentInstructionCycles()`
  (sub-instruction accuracy, the same idiom as the GEN2 video-event journal). A
  mid-scanline `R7`/`R5`/`R6`/`R4`/VRAM change now splits the line at the exact
  pixel (horizontal rainbow, mid-line table swap); a static frame still commits
  each line in one slice, so the golden image is byte-preserved. Pinned by
  `tms9918_per_scanline` Phase G.
- **5S/collision read-time sync**: `syncSpriteScanToBeam(inFlight)` advances the
  per-line sprite scan to the beam's scanline before a `$CC01` read returns, so
  the 5S overflow / collision / index reflect the beam at the **read cycle** —
  the 5S raster-split poll loop is now cycle-precise. Pinned by Phase H.
- **Seamless mode/blank split**: the display mode (R0 M3, R1 M1/M2) and the blank
  bit (R1.6) are latched at the **start** of each render line (`lineLatchR0/R1`)
  while table bases + R7 stay live, so a mid-line mode/blank write defers to the
  next line (Grauw/ARTRAG "seamless" splits). Pinned by Phase I.
- **Shared `BeamClock` seam** (`src/BeamClock.h`, `pom1::beamPosAt`): factors the
  cycle→(line,x) raster mapping the catch-up paths compute inline — the shared
  "BeamClock + renderUntil(beam)" foundation for the TMS9918 (now) and the GEN2
  beam engine (to adopt). Pinned by `beam_clock_smoke`. (The decoupled
  journal/replay renderer + GEN2 adoption remain open in `TODO.md`.)

### Fixed — TMS9918 status-read semantics + active-display access floor

- **Status read clears F + C only, not 5S** (`TMS9918::readControl`, `~0xE0` →
  `~0xA0`): per BiFi/Sean Young §2.2 a `$CC01` read latch-clears only the frame
  flag (bit 7) and collision (bit 5); the 5th-sprite flag (bit 6) and its SAT
  index are re-derived by the sprite scan each frame (per-frame reset at the top
  of the active scan), **not** cleared on read. The old mask wiped 5S, so a
  second in-frame status read wrongly reported "no overflow". Pinned by
  `tms9918_sprite_status` T9. Docs §13/§18/§24/§26 + `TMS9918-SPRITE_INIT` /
  `-SPRITE_BEST_PRACTICES` made consistent; §30 Bug N°4 (overscan collision)
  reclassified 🔑 OPEN (Test E, silicon-unconfirmed) and the contradictory
  inline "overscan [-32,288)" comments corrected to visible-only [0,256).
- **Active-display CPU-access floor = 9c = ⌈8 µs⌉** (`kMinActiveDrainCycles`
  16c → 9c, Gfx12-scoped): the TI datasheet's ~8 µs between data-port writes in
  Graphics I/II is `⌈8 µs × 1.022727 MHz⌉` = 9 cycles, so an 8c gap drops and 9c
  lands. The retired flat 16c floor (≈2× spec) wrongly dropped the sprites-OFF
  `TMS_Plasma` timings that run clean on Parmigiani's Replica-1.
  `tms9918_silicon_strict_runtime` re-anchored.

### Fixed — headless keystroke injection (`--paste-at-cycle`) + TMS9918 silicon A/B validation

- **`--paste-at-cycle` never reached the CPU.** `queueKeystrokes` → `queueKey`
  enqueued the key, but the headless deterministic run path (`runCyclesSync`)
  pauses the async emulation thread — the only thing that drained the keyboard
  queue into Memory (`$D010`). So cycle-scheduled keys sat in the queue forever;
  the CPU never saw them, and the earlier "noise ON vs OFF → identical hash"
  check was trivially true (both runs stalled on the title screen). Fixed with
  `EmulationController::deliverQueuedKeys()` (drains the queue into Memory under
  `stateMutex`), called after each injection in `runCyclesWithTimedPastes`. Inject
  one key per `--paste-at-cycle` for programs that read several prompts in turn
  (each read needs its own `$D010` strobe at its own cycle).
- **A/B validation of the TMS9918 silicon-fidelity fixes.** With the tool
  actually working, Snake (`--run 7600`, keys `1`+`1`), Sokoban (`--run 6200`,
  key `1`) and Galaga (`--run 4100`, keys `1`+space) now drive headless past their
  keyboard-gated title screens into the sprite-bearing playfield. On the gameplay
  frame the render is **pixel-identical with VRAM power-on noise OFF vs ON**
  (`--vram-noise`) — the defensive SAT fill neutralises the ghost sprites the raw
  DRAM noise would otherwise surface. Galaga is additionally **poison-invariant**
  across `--ram-poison 00/FF/AA`, confirming the `anim_tick` (`$3F`) uninitialised-
  RAM read is fixed. This is the empirical close-out the fidelity work was after.

### Added — 6502 software: standalone `$0280` TMS9918 programs rebuilt (pad18)

- The shipped standalone `software/Graphic TMS9918/*.txt` (Woz-hex, load/run at
  `$0280`) had gone stale + orphaned: they carried the pre-pad18 timing and their
  per-program build recipes were deleted in the 686fe03 refactor. Re-established a
  committable, reproducible driver — `sketchs/tms9918/build_standalone_txt.py`
  (goes through `dev/cc65/emit_woz.py`, auto-links `tms9918_pad.asm`) — plus the
  restored `$0280` linker cfgs next to each source. Rebuilt 8 programs against the
  current pad18 libs: Snake, Sokoban, Mandel, Plasma, Life, Vague, Split,
  Logo_16k. Snake + Sokoban verified byte-behavior-identical to their CodeTank ROM
  builds (same gameplay-frame hash); the demos render correctly. The driver is
  idempotent (re-run → no diff). Five artefacts have no in-tree source any more
  (Maze3D, OrbitalPool, SilBench, Stars, Nyan_Fantasy — sources removed in
  686fe03, never migrated to `sketchs/`) and are left untouched; Galaga stays
  CodeTank-only (`$4100`, outgrew the `$0280` window).

### Fixed — 6502 software: defensive SAT fill in Snake + Sokoban

- **`TMS_Snake.asm` / `TMS_Sokoban.asm`** wrote only `SAT[0].Y=$D0` and omitted
  the `SAT[1..127]=$D1` fill — the confirmed cause-#1 of ghost sprites on real
  TMS9918A (renders fine on POM1's bistable power-on VRAM, breaks on silicon).
  Added the inline `$D0 + 127×$D1` fill (gold standard, `TMS9918-SPRITE_INIT.md`
  §4.2), and rebuilt `roms/codetank/Codetank_GAME1.rom`: the Tetris lower bank
  (by **Nippur72 / Antonino Porcino**) is preserved byte-identical, Snake is
  2476/2560 B and Sokoban 5077/5120 B, and the title screens are byte-identical
  old/new under `--vram-noise` (the fill is defensive — neutral outside
  gameplay). **CodeTank cartridge audit**: all 9 games now carry the fill —
  Galaga/Life/Snake/Sokoban inline, Rogue/Mandel/Plasma/Logo/Nyan via the lib
  `disable_sprites` helpers (`tms9918m1.asm` Mode I @ `$1B00` / `tms9918m2.asm`
  Mode II @ `$3B00`). `SPRITE_INIT` §11 corrected: failures **diverge by
  default** (POM1 hides the SAT-init class unless VRAM noise is armed), not
  "shared silicon↔POM1".

### Fixed — 6502 software: GEN2 LOGO `BIRDFLY` sprite flicker

- **GEN2 HGR LOGO (`sketchs/tms9918/tool_logo/TMS_Logo_16k.asm`, build
  `software/Graphic HGR/GEN2Logo.txt`)**: the bird sprite in the `DEMO`
  slideshow's final `BIRDFLY` scene flickered. The cap-only turtle commands
  (`SETH` / `TR` / `TL`) ran the full XOR `erase_turtle` → `draw_turtle` cycle
  even for a `SETSHAPE` **emote**, which is non-directional — turning changes
  none of its pixels (`gen2_draw_emote` ignores the heading, and `tx/ty` are
  unchanged). On a single live HGR page the transient "erased" window between
  the erase and the redraw is what the async beam-race renderer caught as a
  blink (each `BFR`/`BFL` flap does a `TR 12` between the two `FD 3` steps).
  A new `turn_erase` / `turn_draw` seam skips the erase+redraw when
  `sprite_mode ≠ 0` (emote → visual no-op, leave it drawn) and tail-calls the
  real routines when `sprite_mode = 0` (the triangle turtle genuinely rotates),
  removing ⅓ of the per-flap flicker windows (the two pure turns) while leaving
  the real motion (`FD`, `SETSHAPE`) untouched. The "emote + invisible + turn"
  state is unreachable (no `HIDETURTLE`; every `turtle_visible = 0` is either in
  triangle mode or immediately redrawn), so the change is behaviourally
  transparent apart from the fix. The **TMS9918 / CodeTank GAME3 LOGO** build is
  **byte-for-byte unchanged**: it has no flicker to fix (HW sprites + VBlank
  sync), so `turn_erase`/`turn_draw` alias straight through to the originals and
  emit no extra code (verified by identical linked-binary SHA).

### Added — HGR Paint Editor window

- **`HGR Paint Editor`**: an Apple II hi-res paint window for the GEN2 card.
  Draws **live** into the HGR framebuffer (`$2000` page 1 / `$4000` page 2) so
  strokes appear on the GEN2 screen in real time, and renders its canvas through
  the GEN2 NTSC artifact-colour pipeline so it is pixel-identical to the
  emulator's output. Tools: pencil, eraser, line, rectangle, ellipse, flood fill,
  eyedropper, rectangular select/clipboard, palette-shift, plus page select,
  brush size, zoom, grid, seam overlay, minimap, NTSC/mono toggle, undo/redo,
  clear, and load/save of 8 KB `.HGR` images + PNG export. **Faithful HGR colour
  model**: the six artifact colours obey column parity and the per-byte shared
  high bit. Pinned by `hgr_paint_plot_smoke`. Independent reimplementation
  inspired by fadden's HGRTool (concept only, Apache-2.0).
  - **Portable module** (`hgrpaint/`, at the repo root alongside `bench/`): the
    editor + pure model now depend only on ImGui/GL and a small
    `hgrpaint::IHgrPaintHost` seam (poke / render-page / file I/O), mirroring
    `bench/IBenchHost`. POM1 supplies `src/Pom1HgrPaintHost` (GraphicsCard +
    `EmulationController` + stb_image_write); **POM2 can reuse `hgrpaint/`
    verbatim** with its own host. The pure model carries its own Apple II row
    interleave + geometry constants (cross-checked against `GraphicsCard` in the
    test) so it pulls in no emulator headers.
  - **Fill rewritten to flood by *perceived* artifact colour** (renders the page
    through the host NTSC pipeline) instead of raw pixel on/off. An HGR colour
    field is bit-dithered (solid violet is the byte pattern `$55`, odd columns
    *off*), so the old raw-bit flood leaked through every chromatic region via
    those off sub-pixels — filling the background flooded ~the whole canvas.
    Recolour now clears the region first so an old colour's bits can't OR with
    the new ones into white (`$2A | $55 = $7F`). Pinned by new
    `hgr_paint_plot_smoke` cases (no-leak + clean recolour).
  - **UI redesigned MacPaint-style**: a left vertical palette of **FontAwesome
    icon tool buttons** (`IconsFontAwesome6`, same dependency as `bench/`), the
    **colour palette along the bottom**, the **navigator thumbnail in the left
    panel** (below the edit buttons, no longer overlaying the canvas), and a slim
    top strip (page · file · help). The drawing canvas is now an `InvisibleButton`
    (not an `Image`) so **dragging on it paints instead of moving the window** —
    the window only moves from its title bar (was unusable: any stroke dragged
    the whole window).
  - **Ergonomics**: right-drag quick-erase (no tool switch), middle-drag pan,
    `Shift` constrains Line to 0/45/90° and Rect/Ellipse to a square, zoom-to-fit
    on first open, and a `(?)` controls cheat-sheet.
  - **File picker** (portable `std::filesystem`): Load / Save / Save PNG / Import
    now open a modal browser instead of needing a typed path — it lists every file
    with its byte size and highlights the relevant ones (8 KB raw HGR pages for
    Load — they have no standard extension, e.g. `sdcard/NONO/HGR/PIC#062000` —
    or images for Import).
  - **Import PNG/JPG → HGR (ii-pix-grade)**, entirely in `hgrpaint/`
    (`Cam16.{h,cpp}`, `HgrConvert.{h,cpp}`, `HgrImageDecode.cpp`): decode (stb) →
    fit/letterbox resample → **analysis-by-synthesis** dithering. For each byte it
    tries all 256 (7 pixels + palette) patterns, renders each through the module's
    own copy of the NTSC pipeline (**byte-identical to GraphicsCard**, pinned in
    the test), scores it in **CAM16-UCS** perceptual space (chroma-weighted so flat
    greys dither clean black/white instead of magenta confetti), and keeps the
    best with Floyd-Steinberg error diffusion. Beats Buckshot/bmp2dhr by dithering
    against the *true* artifact colours incl. the sliding-window coupling. ~30 ms
    per image. Pinned by `hgr_convert_smoke` (CAM16 sanity, decode == GraphicsCard,
    black→empty, in-gamut reproduction, ramp tone conservation).
  - **Interactive import preview**: picking an image opens a modal with the
    **source and the HGR result side by side**, live-reconverting as you drag
    **Colour noise** (the CAM16 chroma weight — left = clean black/white greys,
    right = vivid colour), **Brightness**, **Contrast** and **Gamma**, plus
    **Diffusion (grain)** — the Floyd-Steinberg strength (1 = full dithering,
    lower = smoother/flatter) — plus **Serpentine** dithering (alternates the FS
    scan direction per row to kill diagonal smear) / Dither / Stretch toggles and
    a Reset. Apply commits it as one undoable stroke; Cancel discards.

### Added — native compiler: `SIN`/`SQR`/`INT`, peephole optimizer, `3DHat.apf` runs native

- **Transcendentals in the float runtime** (`dev/lib/basicrt/basicrt_float.s`):
  `fp_int` (truncate toward zero), `fp_sqrt` (Newton–Raphson, 5 iterations) and
  `fp_sin` (2π range reduction → fold to [-π/2,π/2] → 4-term Taylor). Each is
  **feature-gated** (`-D FP_INT`/`FP_SQRT`/`FP_SIN`) so it links only when the
  program calls it. The compiler compiles `SIN`/`SQR`/`INT` to these, and
  auto-precision forces the float phase when `SIN`/`SQR` appear. Logical `AND`/`OR`
  on floats fixed (had fallen through to the comparison path). Pinned by
  `basic_float_runtime` (now 5736 cases vs host `sinf`/`sqrtf`).
- **`3DHat.apf` compiles and runs native on GEN2 and TMS9918** — the MTU/Micro
  May-1981 hidden-line 3-D HAT (HGR2, nested `FOR`, `IF/GOTO`, `GOSUB/RETURN`,
  `INT`/`SQR`/`SIN`, decimal literals, `HCOLOR=0` column erase) draws the sombrero
  with proper hidden-line removal, standalone, **no ROM**. This meets the project
  goal. Pinned by `basic_native_run` (native-only 3DHat case).
- **`HCOLOR=0` erase** in the GEN2 runtime: `rt_plot` is pen-aware (`AND ~mask` to
  clear vs `OR mask` to set), so the hidden-line "plot point then erase column
  below" trick works; `rt_hgr` seeds a non-zero default pen.
- **Peephole optimizer** (`Codegen::optimizePeephole`): fuses the codegen's
  "define a temp, then copy it elsewhere" chains by retargeting the store straight
  into the destination (temp vanishes; self-copies dropped). Intra-block liveness;
  runtime `jsr fp_*`/`rt_*` transparent. Trims ~640 B — enough that the full 3DHat
  fits GEN2's `$0300–$1FFF` window (`basicc_native.cfg` moves BSS to `$0200` to give
  code the whole window up to the framebuffer at `$2000`).
- **Benchmarks** (`basic_native_bench`): a size+speed-by-program-type table —
  int-arith **21×**, int-raster **14×**, lines **2.4×**, float-arith **2.0×**,
  transcend **1.4×** vs the interpreter; binary 354 B–7157 B with dead-stripping
  (4 runtime routines for integer programs, 13 for 3DHat).

### Added — native compiler: auto-precision, dead-stripping (minimal size), clear diagnostics

- **Auto precision** (`FpMode::Auto`, the `basicc --native` default): the compiler
  picks the **smallest sufficient** numeric type — 16-bit integer unless a line
  needs a fraction (a decimal literal or a `/`), then binary32. A program that
  uses no floats **never links the float runtime**. `--int`/`--float` force a tier.
- **Minimal code size (dead-stripping):** the compiler emits only the runtime
  symbols it uses, and the runtime (`basicrt_*.s`) gates each routine on a `-D
  RT_xxx` flag the build derives from those imports — unused routines and the
  560-byte hi-res pixel tables never reach the binary. Measured GEN2 sizes:
  `X=5+2:X=X+1` **1695 → 89 B**, `PRINT`+`FOR` **1746 → 265 B**, `HGR:HPLOT` 1686
  → 1165 B. Pinned by `basic_native_run` (size assertion) + `basic_native_codegen`.
- **Clear, line-precise diagnostics** for authoring new programs: every error
  names the exact Applesoft line (`line 20: FOR expects a variable`), `GOTO`/
  `GOSUB`/`THEN` targets are validated at compile time (`GOTO 99: no such line
  number`), float literals are rejected in the integer phase, and `NEXT` without
  `FOR` is caught.

### Added — native compiler Phase 2b: float codegen (compile + run a float program, no ROM)

- **The native compiler now emits floating-point code** (`basicnative::compile(…,
  floatMode=true)`, `basicc --native --float`, `basicc_native.sh --float`):
  binary32 variables/temps, `+ - * /` and comparisons via the `fp_*` runtime,
  float `FOR/NEXT`/`IF`, and `HPLOT`/`HCOLOR` coords converted with `fp_toint16`.
  A float program (parabola) compiles to a standalone binary that runs **with no
  interpreter and no ROM float**, drawing the same picture.
- **`basic_native_run`** now pins **both** phases. Measured (native vs same source
  on the interpreter, identical output): integer compute loop **~22×** (16.8M vs
  368M), integer line-draw **~4.5×**, **float parabola ~2.0×** (2.8M vs 5.6M). The
  ~2× float ceiling is honest — binary32 work isn't cheaper than the ROM's float,
  so the gain there is only from removing interpreter overhead; control/integer
  code wins an order of magnitude more. `basic_native_codegen` adds float pins.
- **Remaining for native `3DHat.apf`:** `SIN/COS/SQR/INT` on the proven `fp_*`
  core + `FOR`-index type inference. See [`doc/BASIC_COMPILER.md`](doc/BASIC_COMPILER.md).

### Added — native compiler Phase 2a: standalone binary32 software-float runtime

- **`dev/lib/basicrt/basicrt_float.s`** — the autonomous floating-point core (no
  Applesoft ROM) for the native compiler's FP phase. 4-byte IEEE-754 single
  storage; `fp_fromint16/fp_toint16/fp_add/fp_sub/fp_mul/fp_div/fp_cmp` over the
  zero-page slots `FA`/`FB`, computing on an unpacked `{sign, E, 24-bit SG}` form.
  Pinned by **`basic_float_runtime`** (cc65-gated), which assembles the runtime and
  checks every op against the host IEEE `float` over a value grid **and 4000
  randomised pairs spanning 2^±20** — all exact within float tolerance. Phase 2b
  (compiler type-system integration + `SIN/SQR` → compile `3DHat.apf` to native)
  is the documented next step. See [`doc/BASIC_COMPILER.md`](doc/BASIC_COMPILER.md).

### Added — native BASIC compiler: standalone 6502 machine code (~20× faster, no interpreter)

- **`src/BasicCompilerApplesoft.{h,cpp}` + `dev/lib/basicrt/` runtime + `basicc
  --native` + `tools/basicc_native.sh`.** A **real** native-code compiler (not a
  tokenizer): recursive-descent / precedence-climbing parser → standalone ca65
  assembly with native control flow (`GOTO`→`JMP`, `GOSUB`/`RETURN`→`JSR`/`RTS`,
  `FOR`/`NEXT`/`IF` as native branches, line numbers → labels), 16-bit variables
  at fixed addresses (no name lookup), and **constant-multiply strength reduction**
  (`X*3` → shifts+adds, not a 16-iteration multiply). The output binary runs with
  **no Applesoft interpreter** — only the graphics card — via a tiny per-card
  runtime (`rt_*`) wrapping the project's graphics asm (GEN2 `plot_pixel`/
  `clear_hgr`; TMS `plot_set`/`line_xy`) + shared 16-bit math. Loads + runs at
  `$0300`; both GEN2 and TMS9918.
- **Measured (POM1 core, identical output):** ~**4.5×** on pixel-plot-bound code,
  **~20×** on arithmetic/control-bound code (e.g. 19.2 M vs 368 M cycles). Pinned
  by `basic_native_run` (cc65-gated: builds native + interpreter, asserts same
  framebuffer **and** native faster) and `basic_native_codegen` (pure asm-text
  unit pin: strength reduction, FOR/NEXT, GOTO/GOSUB, graphics ABI).
- Integer phase (16-bit signed: `+ - * /`, comparisons, `AND/OR/NOT`, `ABS`,
  `FOR/NEXT`, `IF/THEN`, `GOTO`, `GOSUB/RETURN`, `PRINT`, `HGR/HCOLOR/HPLOT`).
  **Phase-1 polish:** variables/temporaries moved to **zero page** (~20→25× on the
  arith benchmark), **full 16-bit X** (GEN2 hi-res 0..279, verified exact), `PRINT`
  of strings + signed integers via the WOZ terminal, and a clean TMS link. A
  standalone floating-point runtime (so `3DHat.apf` compiles to native code with
  no ROM) is the documented next phase. See [`doc/BASIC_COMPILER.md`](doc/BASIC_COMPILER.md).

### Added — Applesoft "BASIC compiler": compile an `.apf` to a 6502 image (no injection)

- **`src/BasicTokeniserApplesoft.{h,cpp}` + `basicc` tool + `doc/BASIC_COMPILER.md`.**
  Compiles an Applesoft Lite listing (GEN2 or TMS9918 dialect) **ahead of time**
  into a 6502 memory image — a tokenized program at `$0801` (Applesoft's own
  on-disk layout, byte-for-byte what `PARSE` builds) plus a 14-byte launcher at
  `$0280` (`install VARTAB; JSR SETPTRS; JMP NEWSTT`). The resident interpreter
  ROM supplies every runtime (FP, `SIN/SQR/INT`, `FOR/GOSUB`, `HGR/HPLOT`), so
  the program **loads and runs directly** instead of having its listing typed in
  one keystroke at a time. Pure C++ (no GL/ImGui) → links into the bench
  (desktop + WASM), the CLI and the tests; wired into the app `SOURCES` and a
  standalone `basicc` host tool (`--target {gen2|tms}` → Wozmon-hex image).
- **Pinned by `basic_compiler_smoke`** (ctest): the floating-point
  `sketchs/basic_applesoft/3DHat.apf` 3-D hat compiles and **executes on both the
  GEN2 HGR card and the TMS9918 card**, drawing into each framebuffer — verified
  injection-free (cold-start the ROM, poke the image, jump to the launcher). The
  test re-pins the two interpreter entry points (`SETPTRS`/`NEWSTT`), so a ROM
  rebuild that shifts them fails loudly. A second pure unit test
  (`basic_compiler_tokenize`) pins the exact tokenized bytes (links, `REM`/`DATA`/
  string/`?`→`PRINT`, ascending-line sort, launcher stub) against the Applesoft
  on-disk layout, independent of any ROM.

### Added — packaging: release builds bundle the cc65 toolchain (asm + C)

- **Every release package now ships cc65 next to POM1** so the DevBench
  (`Pom1BenchHost`) compiles **both** of its native languages out of the box,
  with no system cc65 on PATH — asm (`ca65`/`ld65`) **and** C (`cl65`/`cc65`)
  plus the `share/cc65` runtime (resolved via `CC65_HOME`, see `ensureCc65Home`).
  The exe-relative probe (`<exe>/cc65/bin`, macOS `Resources/cc65/bin`, AppImage
  `share/POM1/cc65/bin`) already existed; this wires the *producers*.
- **`tools/verify_cc65_bundle.sh`** (new) — asserts a staged tree carries
  ca65+ld65+cl65+cc65 + `share/cc65/{include,lib,target}`. The three packagers
  (`packaging/linux/build_appimage.sh`, `package_macos_release.sh`,
  `package_windows_release.bat`) call it through a **`POM1_REQUIRE_CC65=1`**
  strict gate so a missing/partial bundle is a hard failure, not a silent
  Woz-hex-only package.
- **`packaging/windows/fetch_cc65.ps1`** (new) — pure-PowerShell fetch of the
  official cc65 Windows snapshot, re-homed to the relocatable `cc65/bin` +
  `cc65/share/cc65` layout (`POM1_CC65_WIN_URL` overrides the URL). The Windows
  packager auto-runs it when no bundle is staged.
- **`.github/workflows/release.yml`** (new) — one native job per OS (cc65 can't
  be cross-built): Linux AppImage, macOS `.dmg`, Windows ZIP, each built with
  cc65 bundled + verified, uploaded as an artifact and attached to the GitHub
  Release on a `v*` tag. `package_macos_release.sh` / `package_windows_release.bat`
  now honor `POM1_VERSION` (tag → artifact name) with the shipped default kept.

### Added — emulator (`src/`): BASIC language axis in the Bench

- **Bench "BASIC" language — Run by injection** (`Pom1BenchHost.cpp`,
  `Pom1BenchHost.h`) — a third Bench language beside asm and C that compiles
  nothing: it cold-starts the in-ROM interpreter through the WOZ Monitor and
  TYPES the listing at the prompt over the Apple-1 keyboard FIFO (`$D010`), then
  `RUN`. New source mode 4 + `injectBasic()`; `build()` dispatches mode 4 before
  the cc65 split, so the path is byte-identical on desktop and the web (WASM)
  build — **no compiler in the loop, so both BASICs run in-browser**. The
  keyboard FIFO self-paces on the program's reads (same pipeline as Ctrl-V
  paste, 4096-char budget).
- **Two BASIC targets (machine + interpreter)** — **Integer BASIC** (Apple-1
  CC65 DevBench machine, preset 0, ROM at `$E000`, cold start `E000R`) and
  **Applesoft Lite** (P-LAB **microSD + Applesoft Lite** machine, preset 8, ROM at
  `$6000`, cold start `6000R`). The microSD preset owns the `$6000` Applesoft ROM +
  the `$8000` SD-OS, but is 8 KB with silicon/OOR-strict armed, so `$6000-$7FFF`
  (inside the `$1000..$7FFF` out-of-range window) reads back `$FF` and a bare
  `6000R` jumped into `$FF` garbage and fell back to the WOZ Monitor (which then
  parsed each program line as a hex address). `injectBasic` therefore **relaxes
  the microSD preset to a permissive 64 KB view for the Applesoft run**
  (`presetRamKB=64` → `isOorAddress` always false → `$6000` and Applesoft's RAM
  workspace live; the microSD card stays plugged, only OOR enforcement is lifted).
  Integer BASIC `$E000` is OOR-exempt (≥ `$8000`) so it needs no relaxation and
  stays on the authentic 8 KB DevBench. `hardReset` reloads Integer automatically;
  `injectBasic` re-loads the Applesoft `$6000` ROM (zeroed by the reset) and
  surfaces a reload failure instead of
  letting the cold-start crash silently. The New-sketch dialog gains a "BASIC"
  language whose Target combo is now **per-language**: picking BASIC offers just
  the two interpreters — "Integer BASIC ($E000)" and "Applesoft Lite ($6000,
  P-LAB microSD)" — as dedicated machine entries, while asm/C still show the three
  graphics machines (`CodeBench` filters the combo by `targetFor()` and snaps the
  selection when the language changes).
- **Built-in BASIC examples + starters** — "Hello (Integer BASIC)" and
  "Hello (Applesoft Lite)" in the Bench *Examples* popup, plus the per-target
  HELLO-WORLD starters used by *New*.
- **Pinned by `bench_basic_inject_smoke`** (`tests/bench_basic_inject_smoke_test.cpp`)
  — boots WOZ headlessly, injects `E000R`/`6000R` + a listing + `RUN` for both
  interpreters and asserts the program's *computed* PRINT result reaches the
  `$D012` display (e.g. `1000+7 → 1007`, `100/8 → 12.5`), proving the
  interpreter executed the injected program rather than echoing the source. A
  third block pins the OOR root cause: `$6000` reads back `$FF` under
  `presetRamKB=8` + strict but the real ROM byte under 64 KB Fantasy — so nobody
  re-points the Applesoft target at an 8 KB / strict machine.

### Added — Bench: four Applesoft BASIC machines + BASIC editor without gutter

- **`New` → BASIC now offers four Applesoft machines** (`Pom1BenchHost.cpp`
  `kP1Machines`/`targetFor`/`injectBasic`): **Applesoft Lite (Apple-1)** =
  `roms/applesoft-lite-cffa1.rom` @ `$E000` (`E000R`, 64 KB-relaxed);
  **Applesoft Lite + microSD** = `applesoft-lite-microsd.rom` @ `$6000`;
  **Applesoft GEN2 HGR** = `sketchs/gen2/applesoft_gen2` @ `$6000` (preset 2);
  **Applesoft TMS9918** = `sketchs/tms9918/applesoft_tms9918`, flashed as a
  CodeTank ROM cartridge @ `$4000` (`4000R`, preset 1). `injectBasic` dispatches
  the ROM load per variant (reloadApplesoftLite{CFFA1,SDCard} / loadInterpreterRom
  for GEN2 / CodeTank flash for TMS9918). `.bas`/`.apf` files route to the GEN2 or
  TMS9918 variant by path. (Integer BASIC drops out of the New grid but is still
  reachable via `.ibas`.) The committed `applesoft-tms9918.bin` ships under
  `software/Apple-1_TMS_CC65/`.
- **BASIC editor hides the gutter line numbers** (`TextEditor` gains
  `SetShowLineNumbers`; CodeBench disables it for BASIC docs) — a BASIC program's
  own line numbers (10, 20, …) are what matter, so the editor gutter is just noise.
- **`applesoft_gen2_smoke` extended** to also cold-start and run the **TMS9918**
  (`4000R`) and **CFFA1** (`E000R`) interpreter cores (`APRINT`/`PRINT 1000+7` →
  `1007`), proving all four renumbered interpreters execute. 35/35 ctest pass.

### Fixed — Applesoft TMS9918: sprite garbage + clipped HPLOT

- **`HGR`/`GR` now park the sprites** (`sketchs/tms9918/applesoft_tms9918/tmsgfx.inc`)
  — the Sprite Attribute Table sat uninitialised (Graphics II SAT `$3B00`, and the
  Multicolor `mc_regs` even pointed it at `$0000` over the framebuffer), so 32
  garbage sprites floated over the bitmap. Both setups now write `$D0` to the
  SAT's first sprite Y (terminates the sprite scan → all hidden); Multicolor's SAT
  moved to `$0B00`.
- **`HPLOT` clamps x to the 256-wide screen** — x came straight from the low byte
  of the 16-bit coordinate, so a GEN2-style `HPLOT … TO 279,191` wrapped to x=23
  and drew a narrow line. `clampx` now pins x≥256 to 255, so the same listing
  draws a full-width line. Pinned by the new `tms9918-hgr` check in
  `applesoft_gen2_smoke` (asserts SAT `$3B00`==`$D0` and the line reaches the
  right-edge cells, via the real VDP).

### Added — Bench: file-type routing, tab-aware mode, markdown hyperlinks

- **The file extension drives the action, re-evaluated on every tab switch**
  (`Pom1BenchHost::targetForPath`, `bench/CodeBench.cpp`): `.s`/`.asm` → assemble,
  `.c` → compile, `.hex`/`.txt` → Woz-hex, `.bas`/`.apf` → inject Applesoft,
  `.ibas` → inject Integer, `.md` → document, **anything else → do nothing**
  (Verify/Run report "nothing to build" instead of silently building a non-source
  file as asm). Switching tabs now refreshes the host's active-source context and
  re-derives the mode from the front tab's extension, so the status bar + toolbar
  always match the tab you're looking at. `targetIndex == -1` is a first-class
  "no build target" state (markdown / unknown), guarded in the status bar.
- **`.apf`/`.bas` BASIC injection is GEN2-aware** — a BASIC file in a GEN2/HGR
  path injects into **Applesoft GEN2** (new target: the `applesoft_gen2`
  interpreter loaded at `$6000` on the GEN2 card, preset 2) so a turtle/graphics
  listing runs with the GEN2 commands; elsewhere it uses the stock microSD
  Applesoft. New `EmulationController::loadInterpreterRom` drops the sketch-built
  interpreter into RAM without resetting the running WOZ Monitor; `injectBasic`
  loads `software/Graphic HGR/applesoft-gen2.bin` for the GEN2 target.
- **Markdown links are clickable** (`bench/Markdown.cpp` — `RenderMarkdown` now
  returns the clicked URL): `[text](other.md)` resolved relative to the document
  opens the target in a new tab when it exists; `http(s)://` links copy to the
  clipboard. Covered by the expanded `applesoft_gen2_smoke` (PRINT→GEN2 /
  APRINT→Apple-1, HGR/HGR2/lo-res, HOME/HTAB/VTAB, SCRN, and end-to-end injection
  of the shipped `Tortue.apf` lo-res drawing).

### Added — 6502 software (`sketchs/`): Applesoft Lite interpreter sketch

- **`sketchs/apple1/applesoft_lite/`** — the full **Applesoft Lite** (Microsoft
  6502 BASIC, floating-point) interpreter source from `txgx42/applesoft-lite`,
  packaged as a DevBench sketch so it assembles in the Bench (Verify) like any
  other Apple-1 ASM sketch. Sources verbatim (`applesoft-lite.s`, `io.s`,
  `cffa1.s`, `wozmon.s`, `macros.s`, `zeropage.s`); the only edit is one
  `.feature force_range` line so modern ca65 (≥ 2.18) accepts the 2008 source's
  negative immediates / `<label-1` precedence. `.sketch.json` drives the build
  (`cfg` + `extraAsm`), and `applesoft_lite.cfg` links the canonical
  `$E000-$FFFF` 8 KB ROM image (BASIC `$E000-$FEFF` + Woz Monitor `$FF00`).
  **The DevBench build is byte-identical to the shipped
  `roms/applesoft-lite-cffa1.rom`** — i.e. this sketch is the editable source of
  the same interpreter that backs the Bench "Applesoft Lite" BASIC runtime
  (relocated to `$6000` as `roms/applesoft-lite-microsd.rom`). Verify/compile is
  preset-neutral; faithful run needs `$E000-$FEFF` backed (the CFFA1 flavour this
  build targets).

### Added — 6502 software (`sketchs/`): Applesoft GEN2 (the BASIC for the GEN2 card)

- **`sketchs/gen2/applesoft_gen2/`** — Applesoft Lite turned into the BASIC for
  Uncle Bernie's GEN2 colour card: CFFA1 disk I/O removed, a full Apple II-style
  graphics + console command set added, and **`PRINT` retargeted to the GEN2
  screen**. New statements: `TEXT GR GR2 HGR HGR2 MIX NOMIX SHOW VBL COLOR=
  HCOLOR= PLOT HLIN..AT VLIN..AT HPLOT..TO HOME HTAB VTAB APRINT` plus the
  `SCRN(x,y)` function. The three freed CFFA tokens become TEXT/GR/HGR and the
  rest are inserted as new statement tokens ($A2-$B1), renumbering every operator
  + function token; dispatch is robust to this (`MATHTBL` off `TOKEN_PLUS`,
  `UNFNC` off `TOKEN_SGN`, positional tokenizer). Handlers + tables in
  `gen2gfx.inc`: lo-res on the `$0400` page (Apple II interleave), hi-res on
  `$2000`/`$4000` via a ÷7 byte/bit calc + the `dev/lib/gen2` scanline tables + a
  16-bit Bresenham for `HPLOT TO`.
- **Output model — `PRINT` → GEN2, `APRINT` → Apple-1.** An Apple II-style `CSW`
  char-out vector (`io.s` `OUTDO` does `JMP (CSW)`): it defaults to the Apple-1
  WOZ ECHO `$FFEF` (so prompt/`LIST`/errors/`INPUT` echo stay on the terminal),
  the `PRINT` wrapper points it at a GEN2 text console (`GCOUT`: cursor, CR, wrap,
  scroll on the `$0400` page) for its output, and `APRINT` forces it back.
  `HOME`/`HTAB`/`VTAB` drive the GEN2 cursor.
- **Pages + sync.** `HGR2`/`GR2` draw on page 2 (`$4000`/`$0800`); `SHOW n`
  displays page n and routes drawing to the hidden page (tear-free double
  buffering); `VBL` is a coarse vertical-blank wait. COLDSTART pins HIMEM at
  `$2000` so BASIC storage (`$0800-$1FFF`) can't grow into the HGR framebuffer.
  Builds to `software/Graphic HGR/applesoft-gen2.bin` (~9.6 KB at `$6000`, run
  `6000R` on preset 2).
- **Pinned by `applesoft_gen2_smoke`** (`tests/applesoft_gen2_smoke_test.cpp`) —
  boots the ROM headlessly and asserts: the renumbered core runs and `APRINT`
  reaches `$D012` (`1000+7 → 1007`); `PRINT` writes screen codes to `$0400`;
  `HGR`/`HPLOT` fill `$2000`; `GR`/`COLOR=`/`PLOT`/`HLIN`/`VLIN` fill `$0400`;
  `HGR2` fills page 2 `$4000`; `HOME`/`VTAB`/`HTAB` place a glyph; `SCRN(5,5)`
  reads back the plotted colour.

### Added / Fixed — Bench: editor syntax highlighting

- **`langBasic()` syntax definition** (`bench/BenchLang.cpp`) for the BASIC editor
  targets — union of Integer BASIC + Applesoft keywords (statements, numeric +
  string `$` functions, word operators), `REM` line comments, Applesoft float /
  scientific numbers, and `$`/`%` variable suffixes, case-insensitive. Wired via
  `langDef("BASIC")`, so picking either BASIC target colours the listing.
- **Highlighting accuracy pass** (multi-agent audit) — `langBasic`: removed three
  non-existent keywords (`ELSE`, `MOD`, `SQRT` — Apple BASIC has no `IF/THEN/ELSE`,
  no `MOD`, and uses `SQR`), added the Applesoft slot-I/O `PR#`/`IN#` tokens, and
  swapped the `/* */` block-comment sentinels for an un-typeable `\x01` marker so a
  literal `A/*B` can't start a phantom block comment. `lang6502`: added ca65
  character literals (`'A'`) and cheap-local labels (`@name`) as proper tokens.
- **`REM` comment false-positive fixed** (`TextEditor.cpp` tokenizer) — the shared
  single-line-comment matcher is now word-aware and case-insensitive-aware: an
  alphabetic marker like `REM` only starts a comment at word boundaries (so a
  variable `REMARK`/`REMOTE` no longer blanks the rest of the line) and matches
  case-insensitively when the language is case-insensitive (lowercase `rem`). No-op
  for `;` (6502) and `//` (C), which legitimately appear mid-token.
- **C block-comment / string desync fixed** (`TextEditor.cpp` Colorize pass) — a
  `"` INSIDE a `/* */` block comment (e.g. the `the "\"` prompt in the
  `GEN2Countdown.c` header) used to open string mode; the `withinString` branch
  never scans for `*/`, so the comment "never closed" and the opened string
  swallowed every line down to the next `"` (`#include "gen2.h"`), wrecking the
  colouring of the whole file. The sole `withinString = true` site is now guarded
  with `&& !inComment`, so a quote inside a block comment stays comment text and
  `*/` end-detection still runs. No-op for normal strings/char-literals/line
  continuations and for asm/BASIC.
- **Bench-specific `langC()`** (`bench/BenchLang.cpp`) — `langDef("C")` now copies
  the upstream C definition (keeping its custom tokenizer + libc built-ins) and
  adds ~115 real POM1 cc65 library entry points (`woz_puts`/`woz_mon`,
  `gen2_hgr_*`, `apple1_getkey`/`apple1_input_line`, `tms_*`/`screen1_*`,
  `gfx_*`, `telemetry_*`) as KnownIdentifiers plus the cc65 qualifiers
  (`__fastcall__`, `__A__`/`__X__`/`__Y__`, `__asm__`, …) as keywords — names
  sourced verbatim from `dev/lib` headers — so library calls in sketches stand out.

### Added — Bench: editor right-click context menu

- **Right-click context menu in the DevBench code editor** (`bench/CodeBench.cpp`)
  — Cut / Copy / Paste / Delete / Select All / Undo / Redo, each enabled by the
  live editor state (selection, read-only, clipboard, undo/redo depth). Extension
  point for later actions (comment block, go-to-error, snippet insert). The
  editor's built-in right-click-on-selection quick-copy is now gated by a new
  `TextEditor::SetHandleRightClickCopy(bool)` (ImGuiColorTextEdit) and disabled by
  the Bench so the right button cleanly owns the menu.

### Added — Bench: multi-file tabs + Markdown preview/edit

- **Multi-document tabs** (`bench/CodeBench.{h,cpp}`) — the DevBench editor went
  from one buffer to a set of open documents rendered in a real tab bar. Each tab
  is an independent `Doc` (its own `TextEditor`, path, target, dirty flag, error
  markers, syntax). New / Open / Examples open in a tab (Open focuses the tab if
  the file is already open); a trailing **`+`** button and per-tab close box (with
  an unsaved-dot) manage the set; closing the last tab respawns a fresh sketch.
  Build / Run / status / toolchain reflect the **active** tab. Reference-stable via
  `vector<unique_ptr<Doc>>` so opening a file mid-frame can't dangle the active doc.
- **Markdown presentation + editing** (`bench/Markdown.{h,cpp}`, new `RenderMarkdown`)
  — opening a `.md`/`.markdown` file gives a **Preview / Edit** toggle: Preview is a
  lightweight rendered view (ATX headings sized via ImGui 1.92 `PushFont(NULL,size)`,
  **bold**/*italic*/`code`/[links], fenced code blocks as read-only selectable
  boxes, bullet/ordered lists, blockquotes, horizontal rules); Edit drops back to
  the text editor. Links copy their URL to the clipboard on click. Verify/Run are
  no-ops on a Markdown doc ("nothing to build"). `Markdown.cpp` added to the bench
  sources (desktop + WASM).

### Fixed — emulator (`src/`): bug-hunt sweep

Emulator-side fixes lifted from `TODO.md` — a defensive pass over snapshot/rewind
deserialisation, the CPU core, the TMS9918, the peripheral/storage stack, and a
second pass over the ImGui UI threading, card-conflict gating, and CLI parsing.
Built clean; all 32 `ctest` pass (Klaus, Harte cycle-exact, `cpu_interrupt`,
`snapshot_smoke`, `iec_snapshot_smoke`) (2026-06-21).

- **HIGH — heap overflow from a forged snapshot** (`Memory.cpp`
  `readSnapshotSections()`, MEM handler) — `ramSize` / `presetRamKB` restored
  from a `.snap`/rewind blob are now `std::clamp`'d (0..64 / 4..64). An
  unvalidated `ramSize` previously drove an out-of-bounds heap write of up to
  ~64 MB in `resetMemory()` / `clearMemory()`.
- **MEM section length validated** (`Memory.cpp`) — the declared `sectionLen`
  (`0x10000` + 12 scalar bytes) is checked before reading; a truncated/forged
  length now fails cleanly instead of loading garbage into RAM/state. Added
  `SnapshotReader::fail()` helper (`SnapshotIO.h`).
- **`loadBinary()` unseekable-file guard** (`Memory.cpp`) — checks
  `file.tellg() < 0` (procfs/FIFO/unseekable paths) before casting to `size_t`,
  mirroring `loadROM`; previously `SIZE_MAX` wrapped the size guard and threw an
  uncaught `std::length_error`.
- **CPU IRQ/NMI mutual exclusion** (`M6502.cpp` `step()`) — IRQ and NMI are now
  mutually exclusive with NMI taking priority (one interrupt per instruction
  boundary, 7 cycles, 3 bytes). A simultaneous IRQ+NMI previously ran BOTH
  handlers (14 cycles, 6 bytes pushed, wrong priority). Latent today (no
  production `setNMI()` caller); vectors unchanged (`$FFFA` NMI, `$FFFE` IRQ).
- **Undocumented multi-byte opcode lengths** (`M6502.cpp`,
  `Disassembler6502.cpp`) — 63 undocumented multi-byte opcodes that were
  dispatched to `Unoff()` (which never advanced PC, desyncing the instruction
  stream) are now mapped to `Unoff2` (2-byte) / `Unoff3` (3-byte) by their real
  NMOS addressing-mode length; the disassembler carries the matching addressing
  mode (mnemonic stays `???`). Documented opcodes untouched; Harte cycle-exact
  still passes.
- **TMS9918 5th-sprite latch independent of F** (`TMS9918.cpp`) — the per-line
  5th-sprite (5S) latch no longer gates on the F (frame) flag; 5S and F are
  independent status latches on silicon, matching the VBlank fallback path.
- **TMS9918 F (VBlank) edge across large slices** (`TMS9918.cpp`
  `advanceCycles()`) — the F flag is set by counting VBlank entries across the
  whole cycle span, so an oversized single `advanceCycles()` slice no longer
  drops the F edge.
- **microSD Timer-2 running flag persisted — SNAPSHOT FORMAT v3 → v4**
  (`MicroSD.cpp`, `SnapshotIO.h` `kSnapshotVersion`) — `t2Running` is now
  cleared in `reset()` and serialised/deserialised. The byte is appended at the
  end of the MicroSD section, gated on `r.version() >= 4`, so older v3 snapshots
  still load (`t2Running` defaults false). Independent of the POM1 application
  version string.
- **IEC card enum range-check on load** (`IECCard.cpp` `deserialize()`) —
  `role_`, `rxPhase_`, `txPhase_` restored from a snapshot are range-validated
  and fall back to `Idle` if out of range, mirroring `Drive1541`/`CFFA1`.
- **WiFiModem escape-guard '+' flush** (`WiFiModem.cpp` `advanceCycles()`) — the
  escape-guard timeout branch now flushes buffered `'+'` chars to the socket
  (1–2 trailing `'+'` followed by an idle pause were silently dropped from the
  TCP stream).
- **D64 fallback allocation spirals from the directory track** (`D64Image.cpp`
  `allocateSector()`) — fallback allocation now spirals outward from track 18,
  alternating below/above — authentic CBM DOS order — instead of packing from
  track 1; misleading comment corrected.
- **DevBench build-log header idempotency** (`Pom1BenchHost.cpp`
  `prependBuildLogHeader()`) — the guard used `compare(0, 24, …)` on a 25-char
  marker and never matched; now uses `rfind(marker, 0)`.
- **MEDIUM — screen-init data race** (`Screen_ImGui.cpp` `initializeScreen()`) —
  the power-on grid rewrite now holds `bufferMutex`. Reachable via
  `resetDisplay()` → `hardReset()` on the emulation thread (TerminalCard telnet
  hard-reset) concurrently with the UI thread's `render()` copy of `screenBuffer`;
  every sibling mutator already locked, this one didn't.
- **Toolbar honours the silicon-strict card-conflict gate** (`MainWindow_Menu.cpp`
  `renderToolbar()`) — the A1-SID / TMS9918 / GEN2 / A1-IO-RTC toolbar buttons now
  route through `gateStrictPlug()` like the Hardware menu, so a conflicting P-LAB
  pair (Parmigiani "one board at a time") can no longer be created from the toolbar
  in silicon-strict mode.
- **Cassette deserialize validates its element count** (`CassetteDevice.cpp`
  `deserialize()`) — the recorded-transition `count` is checked against the bytes
  actually present before `assign()`, mirroring `readByteVector()`; a forged
  `.snap`/rewind blob can no longer drive a multi-GB allocation attempt. Added
  `SnapshotReader::bytesAvailable()` accessor (`SnapshotIO.h`).
- **CLI address parser rejects trailing garbage** (`CliDispatcher.cpp`
  `parseAddr16()`) — both `std::stol` calls now verify the whole string was
  consumed (`idx == size()`), so `--load`/`--run`/`--break` reject malformed
  addresses like `"12G4"` instead of silently using a truncated value, matching
  `parseIntPositive()`.

### Changed — emulator (`src/`): GEN2 silicon params follow the SILICON/FANTASY buttons

- **GEN2 HGR silicon-fidelity knobs are now armed/disarmed by the silicon
  profile toggle** (`MainWindow_Menu.cpp` toolbar ruler/horse button,
  `MainWindow_HardwareWindows.cpp` master `SILICON STRICT`/`MULTIPLEXING FANTASY`
  button, `MainWindow_Presets.cpp` preset apply). All four GEN2 power-on knobs
  (`setGen2RandomPowerOn` → random latch / floating-bus noise / vertical scanner
  phase / framebuffer DRAM noise) now flip with the master profile, and the four
  individual Silicon Strict Inspector checkboxes track it. Previously only the
  preset path touched them; the toggle buttons left GEN2 out of sync. The
  headless path stays deterministic.

### Fixed — emulator (`src/`): bug-hunt sweep (continued — passes 3-4)

Further adversarial bug-hunt passes over real-time audio, host I/O error
handling, the DRAM-refresh CLI feature, and snapshot/reset robustness of the
storage cards. Built clean; all 32 `ctest` pass (2026-06-21).

- **Tape export checks write errors** (`CassetteDevice.cpp` `saveAciTape` /
  `saveWavTape`) — both now `flush()` + verify stream state after writing, so a
  full/failing disk reports an error instead of silently leaving a truncated
  `.aci`/`.wav` reported as success.
- **`--dram-refresh` / `--no-dram-refresh` now honoured in GUI mode**
  (`main_imgui.cpp`, `MainWindow_ImGui.{h,cpp}`) — the override was applied only
  on the headless path; the GUI launch dropped it so the preset default always
  won. Added `setDramRefreshOverride()` + an apply branch mirroring
  `--silicon-strict`.
- **Cassette ramp-in counter is now atomic** (`CassetteDevice.{h,cpp}`) —
  `audioRampInSamplesRemaining` was reachable from the realtime audio-callback
  thread and main-thread resets under two different mutexes (a data race). Made
  `std::atomic<uint32_t>`; `clearLiveAudioState()` no longer needs a mode-specific
  lock (which would have dead-locked callers already holding `audioStreamMutex`).
- **microSD write-finish reports I/O errors** (`MicroSD.cpp` `cmdWriteFinish()`) —
  a failed/truncated host write now returns `I/O ERROR` to the guest instead of
  acknowledging the SAVE as OK.
- **DRAM-refresh stall counter cleared on reset** (`M6502.cpp` `hardReset()`) —
  `hardReset()` now calls `resetDramRefreshStallCount()` so the inspector's
  "stall cycles since reset" readout matches its label after a reset/preset switch.
- **microSD MCU FSM phases validated on snapshot load** (`MicroSD.cpp`
  `deserialize()`) — `mcuPhase`/`nextPhaseAfterResponse` are clamped to `IDLE`
  if out of range; a forged snapshot could otherwise wedge the MCU (the
  command-byte switch has no `default`). Mirrors the IEC/Drive1541/CFFA1 clamps.
- **IEC daughterboard FSM reset with the microSD VIA** (`Memory.cpp`
  `resetMemory`/`initMemory`) — `iecCard->busReset()` now runs alongside
  `microSD->reset()`, so an F5 hard reset mid-transfer no longer leaves the IEC
  serial-bus FSM desynced from the freshly-cleared VIA it rides on.
- **1541 error-channel read state reset on snapshot load** (`Drive1541.cpp`
  `deserialize()`) — the in-flight channel-15 `errBuffer_`/`errCursor_`/`errBuilt_`
  (not serialized) are now reset on load so the next read re-derives a clean
  stream from the restored `errCode_` instead of duplicating/restarting bytes.
- **`--dram-refresh` documented** (`doc/CLI.md`) — added the flag row next to
  `--silicon-strict` (it was missing from the canonical CLI reference).

### Added — 6502 software (`dev/`): shared graphics library, shared font, TMS9918 demos

6502-side work that ships under `dev/` (libraries + `sketchs/` + `dev/projects/` programs),
lifted from `dev/TODO6502.md`. The programs build to `software/<dir>/` via the
per-project Makefiles; dev loop → `sketchs/doc/APPLE1DEV.md`.

- **Shared geometry/number library `dev/lib/gfx/`** (2026-06-16/17) — additive
  layer factoring the line/circle/rect/ellipse + integer→ASCII routines that GEN2
  HGR and the TMS9918 bitmap card each duplicated. Backend resolved at link time
  (Parmigiani "one card at a time"): `gfx-gen2.lib` (280×192) and `gfx-tms.lib`
  (256×192) link-on-demand `ar65` archives. **GEN2 rewired** — `gen2_hgr_line/
  rect/circle` forward to `gfx_*`, `gen2_hgr_putx` → `gfx_hexstr`, new
  `gen2_hgr_ellipse`; **TMS9918 rewired** — `screen2_line/circle/ellipse` +
  `printlib` dec/hex route through `gfx_*`, TMS gains `screen2_rect`. Wired into
  the 5 GEN2 Makefiles, the 4 `screen2` TMS demos, and the Bench's GEN2-C cl65
  line; `make -C dev/lib/gfx check` compiles every TU against both backends.
- **Fast byte-aligned TMS rectangle fill** (2026-06-17) — `screen2_filled_rect`
  (`dev/apple1-videocard-lib/lib/screen_ext.c`) replaced its per-pixel
  `screen2_line` loop with a scanline left-partial / full-byte-run / right-partial
  fill (the TMS analogue of GEN2's `fill_pixrect`); ~10–14× fewer VRAM-port
  accesses, verified pixel-identical to a reference fill on 14 edge cases.
- **Card-neutral text façade `gfx_text` (axis 3)** (2026-06-20) — an 8×8
  cell-cursor model so a program positions text/numbers and compiles for either
  card by backend choice alone: `gfx_gotoxy` / `gfx_putc` / `gfx_text` /
  `gfx_putu` / `gfx_puti` / `gfx_putx`. Shared `gfx_text.c` owns the cursor +
  advance/wrap + formatting; per-card cell backends map a cell to the native blit
  (`gfx_text_backend_gen2.c` → `gen2_hgr_puts8`, 35×24; `gfx_text_backend_tms.c`
  → `screen2_putc`, 32×24 with `FG_BG` colour). Additive — the rich per-card text
  (GEN2 16×16 + NTSC colour, TMS sprites/true-colour) is untouched.
  `sketchs/portable/hello_gfx_text/` builds the SAME source for both cards and
  pins the façade; GEN2 render verified under POM1.
- **Shared Beautiful Boot font, multi-format emitter** (2026-06-17) —
  `tools/build_shared_font.py` emits one master (`dev/lib/hgr/bbfont_cp437.inc`)
  to both cards: HGR (`gen2_bbfont.inc`, bit 0 = left pixel) and TMS
  (`bbfont_tms.inc`, pattern table, bit 7 = left = bit-reversed HGR byte), plus
  the 37-glyph HUD subset (`font_hud8x8.inc`) now generated from the same master
  (Snake/Sokoban HUD text becomes BB). `--check` mode; `emit_bbfont.py` is now a
  compat shim.
- **TMS9918 Mode 2 (bitmap) graphics** — `init_vdp_g2`
  (`dev/lib/tms9918/tms9918m2.asm`), exercised by 7 programs (`tms9918_mandel`,
  `_asteroids`, `_maze3d`, `_light_corridor`, `_clone`, `_logo`).
- **TMS9918 Mode 1 demoscene** (2026-05-08) — `sketchs/tms9918/demo_plasma/`, a
  6502 port of Cruzer/jblang's *Plascii Petsma*: 12 effects × 16 palettes,
  auto-cycling, 1 433 B, stock 4 KB layout.
- **5th-sprite-overflow raster trap** (2026-05-08) —
  `dev/lib/tms9918/tms9918_5strigger.asm` (`arm_5s_trigger` / `wait_5s_trigger`,
  `WAIT_5S` macro): schedule a mid-frame palette/name-table swap without /INT (the
  TMS9918 has no line interrupt). Demo `sketchs/tms9918/demo_split/` (palette split
  at scanline 96).
- **Sprite-cloning (Bug N°8) visual fixture** (2026-05-08) —
  `sketchs/tms9918/demo_clone/`: SPACE toggles the illegal M1+M2 hybrid so the
  sprite-clone cascade appears/disappears for side-by-side comparison; validates
  the cloning model (`sketchs/doc/Programming_TMS9918.md` §15 Bug N°8).
- **Silicon-strict port of every TMS9918 program** (2026-04-30) —
  `tools/silicon_strict_patch.py` injected 351 `tms9918_pad12` NOPs across all
  TMS9918 projects + `lib/tms9918/*.asm`; all 3 CodeTank ROM layouts rebuild clean
  (`sketchs/doc/Programming_TMS9918.md` §25).
- **`dev/projects/*/README.md` TODO placeholders resolved** (2026-06-16).

### Added — DevBench menu + Bench GEN2 text target

- **DevBench top-level menu** (`MainWindow_Menu.cpp`) — groups the dev tooling:
  *POM1 Bench (sketch editor)*, *Telemetry Side Channel*, *TMS9918 VDP
  Inspector*, *Silicon Strict Inspector*. Telemetry and Silicon Strict moved
  here out of Settings.
- **TMS9918 VDP Inspector always available** — opening it from DevBench
  auto-plugs the TMS9918 (and evicts A1-AUDIO SE) so there is a live VDP to
  inspect; `renderTMS9918InspectorWindow()` is no longer gated on
  `tms9918Enabled` (`MainWindow_ImGui.cpp`, `if (showTMS9918Inspector)`).
- **Bench "Bernie GEN2 TXT" target** + 4th machine axis in the New-sketch
  dialog (`Pom1BenchHost.cpp`) — native GEN2 TEXT mode (40×24, page `$0400`,
  built-in font, `$C251`), asm + C starters (`apple1_gen2.cfg` / `C-gen2`).
  The New-sketch matrix is now 2 languages × 4 machines (`targetFor` =
  `language*4 + machine`).
- **Bench New-dialog hints** — optional `IBenchHost::languageHints()` /
  `machineHints()` (`bench/IBenchHost.h`, `Pom1BenchHost.cpp`), shown inline in
  the dialog; P-LAB cited for the TMS9918 entries.
- **`gen2_hgr_puts()` / `gen2_hgr_row()`** in `dev/lib/gen2c`
  (`gen2.c`/`gen2.h`) — draw an ASCII string in GEN2 HIRES using an embedded
  Beautiful Boot font, pixel-doubled (H+V) to solid white with no NTSC
  artifacts (16×16 cells, 18px pitch); `gen2_hgr_row` resolves a scanline base.
- **Settings → A1-SID version & addresses** submenu (`MainWindow_Menu.cpp`) —
  pick A1-SID (`$C800-$CFFF`) vs A1-AUDIO SE (`$CC00-$CC1F`) and list all 29 SID
  register addresses for the active variant.

### Changed — Preset table merge + GEN2/TMS starters

- **Preset table simplified** (`MainWindow_Presets.cpp`) — the A1-SID and
  A1-AUDIO Special Edition presets merged into ONE preset #6 (I/O window
  selectable in *Settings → A1-SID version & addresses*). All later presets
  renumbered (old 8→7 … 14→13); final range 0–13. Invariant kept: no TMS9918
  preset without CodeTank — P-LAB Multiplexing Fantasy (#11) now plugs CodeTank
  GAME1 (`roms/codetank/Codetank_GAME1.rom`). POM1 Multiplexing Fantasy (#13)
  now plugs ACI by default. Preset numbers updated repo-wide in docs/tools.
- **GEN2 hello-world starters** — GEN2 HGR asm + C now render BBFont
  "HELLO WORLD" (pixel-doubled white text) instead of a fill/X;
  `gen2_hgr_clear`/`gen2_hgr_puts` optimised (~20× faster — page/Y-indexed
  clear, per-row base resolution). TMS9918 asm starter does a proper VDP init
  (blank during setup → clear VRAM → park sprites → screen on last).
- **Bench window** (`bench/CodeBench.cpp`) — taller default (660×720) + min
  size (520×480, `SetNextWindowSizeConstraints`); bottom status bar pinned
  flush to the window bottom (was leaving a ~30px gap).

### Added — State rewind (microM8-style timeline)

- **Timeline scrub + delta ring (MVP)** — `RewindBuffer`: delta-encoded ring of
  in-memory snapshot blobs (section + 256 B chunk deltas, keyframe-anchored
  segments, 128 MB budget eviction), captured from the emulation slice
  (~4 captures/s). UI: **CPU → State Rewind…** scrub panel — slider preview
  (pause + restore), *Resume here* (truncate the rewound-past future), *Back to
  live*. Pinned by `rewind_buffer_smoke`.
- **Inline timeline band in the toolbar** — a live scrubber between the
  silicon/fantasy badge and the About button (`renderToolbar`). Recording
  auto-starts once so the band is live out of the box; dragging it back
  previews that instant on every plugged display (`rewindSeekTo` restores +
  republishes the state), and releasing resumes the machine there
  (`rewindResumeHere`). Shows `LIVE` / `REWIND -N.Ns`, auto-sizes to the gap and
  widens with the window. Shares the proven seek/resume API with the panel.
- **Snapshots now capture the visible screen** (new `SCREEN` section via a
  `DisplayDevice::serialize` hook + `Screen_ImGui` override). The Apple-1 text
  grid lives in the display device, not in RAM, so before this a rewind/restore
  moved CPU+RAM back but the on-screen text stayed at the live frame — scrubbing
  appeared to do nothing on a text preset. Now the grid (content + scroll +
  cursor) rides along, so dragging the timeline shows the older screen images.
  Benefits `.snap` save/load too; backward-compatible (old `.snap` files simply
  lack the section). Memory-only fixtures (no display) skip it.
- **Desktop only** — rewind capture runs on the dedicated emulation thread; the
  single-threaded, memory-bounded WASM build can't afford the periodic
  full-state capture on its one main-loop thread, so rewind (capture + the
  toolbar band + the *State Rewind* panel) is compiled out (`#if !POM1_IS_WASM`)
  in the web build.

### Added — Uncle Bernie GEN2 colour graphics: cycle-accurate beam-racing engine (POM2 back-port)

GEN2 moves from a passive `$2000-$3FFF` framebuffer + end-of-frame MAME
rasteriser to a cycle-accurate, beam-raced video subsystem driven by the
release card's `$C250-$C257` soft switches.

- **Hardware spec resolved** — Bernie's `doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf`
  transcribed; Q1–Q10 closed in `doc/GEN2_RELEASE_questions.md`. Read-only
  switches (a read toggles + returns HST0 in D7; writes are no-ops), HST0 high in
  H/V-blank with a notch during the 3-cycle colour burst, HIRES page 2
  `$4000-$5FFF`, decode `SEL = $Cxxx & !A11 & A9 & A4`, indeterminate power-on
  latch (software must init; Apple-1 RESET leaves it alone), 65 cyc/line × 262
  @60 Hz / 312 @50 Hz.
- **Developer guide ("Bernie SDK")** — `doc/GEN2_RELEASE.md`: `$C25x` map, HST0
  recipes, `$C05x`→`$C25x` porting table, dual-monitor, 48 KB RAM, the POM1 dev
  loop. Reusable `dev/lib/gen2/` (`gen2.inc` equates + cheat-sheet,
  `gen2_sync.asm` coarse `gen2_waitvbl` + exact `gen2_beam_lock`).
- **Video clock + floating bus** — `Gen2VideoScanner` (`src/Gen2VideoScanner.{h,cpp}`):
  NTSC cycle counter (`65×262`/`65×312`), verbatim MAME `scanner_address`,
  `floatingBus(mem)` (HGR page 1 `$2000` / page 2 `$4000`); advanced from
  `Memory::advanceCycles()` when the HGR framebuffer is attached. Pinned by
  `gen2_floatingbus_smoke`.
- **Soft switches `$C250-$C257` + HST0** — registered on `PeripheralBus` over
  `$C200-$C7FF` with Bernie's mirror decode (`$C2/$C3/$C6/$C7xx`, A4=1); a read
  toggles the switch, journals a `Gen2VideoScanner::Event`, and returns
  `(HST0<<7) | xorshift-noise`; decoded writes are blocked. 50/60 Hz jumper
  exposed (`setGen2FiftyHz`) + persisted in the `GEN2VID` snapshot section.
  Pinned by `gen2_softswitch_msb_smoke`.
- **Beam-raced renderer** — per-frame `VideoEvent` journal (`emuCycle` as the
  single source of truth, republished at every video-frame rollover),
  `frameCycleToPos` + `forEachBeamSegment`, `GraphicsCard::render(memory,
  endState, frameStart, events, linesPerFrame)` with a zero-regression
  `rasterizeToBuffer()` fast path for pre-beam HGR programs. Modes: TEXT 40×24
  B&W (built-in 5×7 font), LORES 40×48/16-colour, HIRES 280×192 NTSC artifact,
  MIXED, PAGE2. **Vertical and horizontal mid-scanline splits** (whole-line
  decode, clipped write-back, NTSC neighbour context preserved at the boundary).
  Pinned by `gen2_beam_race_smoke`, `gen2_horizontal_split_smoke`. OOR-strict
  carve-out extended to `$2000-$5FFF` (card DRAM behind both HGR pages).
- **Product integration** — preset 13 plugs the engine via
  `setHgrFramebufferAttached`; Hardware Reference + tooltips + memory map
  updated; CLAUDE.md updated; validation demo `sketchs/gen2/demo_a1_crazycycle/`
  (→ `software/Graphic HGR/A-1-CrazyCycle.{bin,txt}`, `E000R`) — latch init by
  reads, HGR colour test card, then a beam-raced TEXT window mid-pattern with
  cycle-exact HST0 sync and per-line horizontal splits.

### Added — Telemetry side channel (dev-only test-harness port)

Binary, frame-delimited bridge for automated game testing — the generalisation
of the Terminal Card's `$D012`→TCP→`$D010` bridge. The game writes its state to a
4-byte MMIO window; an external harness reads it over TCP and drives synthetic
input. Design: `doc/TELEMETRY_SIDE_CHANNEL.md`. Not real hardware — a dev aid in
the "fantasy" category.

- **`TelemetryPort`** (`src/TelemetryPort.{h,cpp}`) — a `pom1::Peripheral`
  modelled on `TerminalCard`: MMIO window `$C440-$C443`
  (`TELE_DATA`/`TELE_CTRL`/`TELE_IN`/`TELE_INLEN`), outbound frames
  `0xAA <len16-le> <payload>` flushed from `advanceCycles`, inbound FIFO, TCP
  server on localhost (default `:6503`, reuses `SocketHandle` + the TerminalCard
  socket pattern, WASM no-op stubs), `KeyInjector` for synthetic keyboard input
  (`$D010/$D011`). State is transient — `serialize`/`deserialize` are no-ops.
- **Bus window `$C440-$C443`** — the `$C4xx` A9=0 dead zone GEN2's decoder
  (`SEL = $Cxxx & !A11 & A9 & A4`) is structurally blind to and no other card
  claims (ACI `$C0/$C1xx`, SID `$C8xx+`, Juke-Box ≤ `$BFFF`, PIA `$Dxxx`);
  registered on the `PeripheralBus` at priority 30 so it wins over GEN2's broad
  `$C200-$C7FF` pass-through. Disabled by default.
- **`--telemetry-port <N>`** — opens the channel on `127.0.0.1:N` (CliDispatcher
  → MainWindow override → `EmulationController::setTelemetryListenPort` +
  `setTelemetryEnabled`). Documented in `doc/CLI.md`.
- **Deterministic lock-step** (`kCtrlLockstepOn` / `$02`) — an end-frame write
  parks the CPU at that instruction until the harness sends `kAckByte` (`0x06`):
  `Memory` calls `M6502::stop()` for a cycle-exact halt, `EmulationController`'s
  slice loop pumps the socket via `TelemetryPort::serviceStall()` between slices
  (stateMutex released — no deadlock, UI stays live), a 5 s timeout auto-resumes a
  dead harness. Game-transparent (no game-side polling). Verified end-to-end:
  exactly one frame per ACK, CPU provably parked between frames — pinned by
  `tools/test_telemetry_lockstep.py` (assembles a 6502 emitter, drives it over
  the socket).
- **`--telemetry-log <path>`** — tees the outbound frame stream to a binary file
  (same framing as the socket) for golden-trace CI — no live harness needed,
  diff a run against an expected capture. Implies enabling the port. Captures
  every frame even under socket backpressure. Documented in `doc/CLI.md`.
- **UI status panel** — *View → Telemetry Side Channel…* shows the snapshot
  (enabled, listen port, connected harness, lock-step, frames/bytes) with an
  Enable toggle; fed via `EmulationSnapshot.telemetry` / `SnapshotPublisher`.
- **`--headless`** — run with no GLFW window / GL / ImGui (`runHeadless` in
  `main_imgui.cpp`): a separate driver builds `EmulationController` (own emulation
  thread, null screen), applies speed + telemetry + Phase-C deferred verbs, then
  idles until SIGINT/SIGTERM. Lets the telemetry regression tests run on a
  display-less CI box — verified: `tools/test_telemetry_lockstep.py` now launches
  `--headless` and passes with `DISPLAY` unset (while the GUI path correctly fails
  `glfwInit` there). **`--preset` / `--enable` / `--disable` are applied headless
  too** (`MainWindow_ImGui::applyHeadlessConfig` — RAM + cards + BASIC ROM,
  plugged immediately since there is no frame loop): `tools/test_headless_preset.py`
  proves `--preset 13` plugs Uncle Bernie's GEN2 (the soft-switch HST0 bit
  toggles) with no display, while the default machine does not.
- **`Memory` out-of-line `~Memory()`** — added so the forward-declared peripheral
  `unique_ptr`s only need their complete type at the single dtor definition point
  (was previously relying on transitive includes in every TU that destroys a
  `Memory`).
- **Not yet** (tracked in `TODO.md`): the CI **GitHub Actions** workflow itself
  (now unblocked — it can drive `--headless --preset` + the telemetry tests).

### Added — Telemetry game-testing SDK kit

Turns the raw telemetry mechanism into a usable kit — the "dream SDK" loop
(compile → load → automated test that *sees* state + *drives* input),
demonstrated end-to-end with no human and no display.

- **`dev/lib/telemetry/telemetry.inc`** — cc65 6502-side library: `$C440-$C443`
  equates + macros (`TELE_ARM`, `TELE_PUT`/`TELE_PUTA`/`TELE_PUTI`, `TELE_FRAME`)
  so a game emits its per-frame state in a couple of lines.
- **`tools/pom1_telemetry.py`** — Python harness library: `TelemetryClient`
  (connect-with-retry, `read_frame`, `send`, `ack`, `step`) + a `launch_headless`
  context manager (boots POM1 `--headless`, connects, tears down). Tests reason
  over frames + inputs instead of parsing sockets.
- **Worked example** — [`sketchs/apple1/demo_telemetry/`](sketchs/apple1/demo_telemetry/) (a "homing" game built
  on `telemetry.inc`, → `software/Telemetry/A1_TelemetryDemo.bin`) +
  `tools/test_telemetry_demo.py`: the harness reads `[player, target, won]`,
  sends a direction each frame, and the player converges on the target in 15
  deterministic frames — verified headless (no `DISPLAY`).
- `tools/test_telemetry_lockstep.py` refactored onto the library (dogfood) and
  switched to `--headless`.

### Added — POM1 Bench (in-app Arduino-style cc65 / Wozmon IDE)

An in-app sketch editor + build + upload + serial-monitor loop — the author
front-end for Bernie's "dream SDK". **Desktop-only** for the cc65 asm/C path (no
subprocess in WASM); the web build keeps the Wozmon-hex target + a "desktop
only — download the app" CTA banner (`IBenchHost::headerNote()`).

- **Serial Monitor (= telemetry)** — the *Telemetry Side Channel* panel became a
  real serial monitor: TX tap (`txMonitorRing`, hex/ASCII view, autoscroll,
  Clear), RX injection line (ASCII/Hex → `TelemetryPort::injectInbound`), a
  *Step frame* button (releases exactly one lock-step frame), and a *Log to
  file* field (same stream as `--telemetry-log`).
- **Editor + Upload** — *DevBench → POM1 Bench*: a 6502/cc65 editor (vendored
  ImGuiColorTextEdit, MIT, with a 6502 syntax definition), Open/Save, and Upload
  as Wozmon-hex or raw-bytes@$ (both stop → reset-vectors → hardReset → run).
- **Verify / Build & Run via cc65** (desktop) — toolchain probe (`whichExe`,
  exe-relative then `$PATH`), a *Board*/*Target* selector, `ca65`/`ld65`/`cl65`
  output captured into a Build-output console with clickable error lines
  (`parseBenchErrorMarkers` → `TextEditor::SetErrorMarkers`).
- **Arduino-style chrome** — teal toolbar with round Verify/Upload/New/Open/Save
  + Serial-monitor buttons, Source/Board selectors, a sketch tab, cream editor,
  dark console (orange error lines), teal status bar.
- **Targets = machine + build** — `kBenchTargets[]` bundles a POM1 preset
  (`applyMachineConfig`), a cc65 `.cfg`, a source mode, and an optional companion
  asset; selecting a target plugs the machine it runs on. Includes the
  TMS9918-C-as-CodeTank-ROM path (16K@$4000 → pad 32K → `loadCodeTankRom` →
  `4000R`). The New-sketch dialog is a 2 languages × 4 machines matrix
  (Apple-1 dual-4K/8K · P-LAB TMS9918 · Uncle Bernie GEN2 HGR · Bernie GEN2 TXT).
- **Built-in Examples** — Blink (asm/hex), Hello world (C/TMS9918),
  A-1-CrazyCycle (GEN2 HGR), Telemetry demo.
- **Bundled cc65 (packaging)** — `Pom1BenchHost::probe()` resolves
  `ca65/ld65/cl65` exe-relative first (`<exe>/cc65/bin`, macOS `Resources`,
  AppImage `share/POM1`) + a `POM1_CC65_DIR` override; `ensureCc65Home()` points
  `CC65_HOME` at the bundle's `share/cc65`. `tools/build_cc65_bundle.sh` stages a
  relocatable tree (self-tests `cl65`); the three packagers stage it
  conditionally (warn + continue if absent). Pinned by `process_util_smoke`.

### Added — CI + cycle-exact CPU test

- **GitHub Actions CI** (`.github/workflows/ci.yml`, Linux) — build → `ctest`
  (Klaus + Harte cycle-exact + smoke + graphics regression) →
  `make -C dev/projects` (build-all). Verified green on GitHub infra.
- **Per-opcode cycle-exact CPU oracle** (`cpu_harte_smoke`) — Tom Harte "65x02
  ProcessorTests", 100 cases × 151 documented opcodes, asserting final regs +
  RAM **and** cycle count (15100/15100). Fixed several `M6502.cpp` cycle counts
  (`zp,X`/`zp,Y`/`(zp,X)`/INC-DEC memory/JSR/RTS/BRK/RTI), decimal ADC/SBC
  (Bruce-Clark NMOS), and the PLP/RTI B-bits. Klaus stays green; IRQ/NMI/BRK line
  timing covered by `cpu_interrupt_smoke`.
