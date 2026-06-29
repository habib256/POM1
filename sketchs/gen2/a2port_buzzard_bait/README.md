# Buzzard Bait — portage Apple-1 + GEN2 HGR

Désassemblage **ré-assemblable** du jeu *Buzzard Bait* (Sirius Software, 1983)
porté de l'Apple II vers l'Apple-1 équipé de la carte **GEN2 HGR** d'Uncle
Bernie, tel qu'il tourne dans POM1.

> Le jeu jouable se charge via `software/Graphic HGR/BuzzardBait.txt`
> (image + shims du portage, au format hex Woz, lancement auto `0940R`).
> Ce dossier-ci contient le **source ASM** correspondant.

## Provenance

Extrait de `POM2/disks_3.5/TheBestGames.2mg` (volume ProDOS `BEST.GAMES`,
fichier `BUZZARD.BAIT`, type BIN, adresse de chargement `$0940`, 29376 octets)
avec `cadius EXTRACTFILE`.

## Contenu

| Fichier | Rôle |
|---------|------|
| **[`DISASSEMBLY.md`](DISASSEMBLY.md)** | **Compréhension complète du jeu** : carte mémoire, chaque routine, assets/sprites, mécanique, et le **moteur de sprites XOR** en détail. Analyse en prose. |
| `buzzard_bait.s`   | Désassemblage da65 **annoté fonctionnellement** (labels + commentaires par routine ; zones de données en `.byte`). Se ré-assemble **byte-identique**. Les renvois `3.5`, `8.6`… dans les commentaires pointent vers les sections de `DISASSEMBLY.md`. |
| `buzzard_bait.bin` | Image portée de référence (entrée da65 + cible du round-trip). |
| `buzzard_bait.info`| Fichier info da65 : labels matériels + hooks + **labels fonctionnels** (moteur, IA, mécanique) + zones de données. `make disasm` régénère le `.s` depuis ce fichier. |
| `buzzard_bait.cfg` | Config ld65 (segment CODE en `$0940`). |
| `gen2_port.s`      | Routines **ajoutées** par le portage (WAIT son + latch clavier), chargées en RAM basse `$9900+`. |
| `gen2_port.cfg`    | Config ld65 pour `gen2_port.s` (`$9900..$9950`). |
| `Makefile`         | `make` vérifie le round-trip ; `make disasm` régénère le `.s`. |

## Build / vérification

```sh
make            # ca65+ld65 : ré-assemble buzzard_bait.s et confirme l'identité avec buzzard_bait.bin
make disasm     # da65 : régénère le désassemblage (buzzard_bait.regen.s) depuis le .bin
```

## Le portage en bref

> **Chaque site modifié est annoté en ligne** dans `buzzard_bait.s` : chercher
> `[PORTAGE` (ou `RELOCALISATION`). Un tableau récapitulatif de **tous** les
> sites figure aussi en tête du listing. Les annotations sont stockées dans
> `buzzard_bait.info` (attribut `COMMENT` des labels da65) : `make disasm` les
> régénère. Décompte : 6 graphisme + 20 clavier + 4 touches + 5 son + 1 boot + 2 menu + 1 hors-mémoire (table 72 entrées).

Le binaire d'origine est un jeu Apple II HGR autonome. Les familles de
changements (toutes visibles dans `buzzard_bait.s`) :

| Domaine | Apple II | Apple-1 + GEN2 |
|---------|----------|----------------|
| **Graphisme** | soft switches `$C050/51/52/54/57` | remappés `$C25x` (`bit $C251/$C254`, `lda $C250/$C252/$C254/$C257`) |
| **Son** | `jsr $FCA8` (WAIT Monitor) + bascules `$C030` | WAIT fourni en RAM `$9900` (`gen2_port.s`) ; `$C030` → ACI TAPE OUT |
| **Clavier** | `lda $C000` / `bit $C010` | latch logiciel `$9950` : `jsr kbd_read` / `jsr kbd_clear` (`gen2_port.s`) |
| **Touches** | ← `$88` / → `$95` / `A` tir / `S` stop | **IJKL** : I=tir (`$C9`), J=gauche (`$CA`), K=stop (`$CB`), L=droite (`$CC`) — défauts en `Init_DefaultKeys` |
| **Boot** | crédit de crack `ALDO… CCB` | remplacé par `GEN2 HGR PORT - UNCLE BERNIE` (`Boot_CreditText` @ `$0981`) |
| **Menu** | `PLEASE SELECT` (P)addle/(K)eyb/(J)oy/(A)tari | réécrit : aide des touches + `PRESS ANY KEY TO PLAY` (`Menu_DrawHelp` @ `$0C7C`) ; **toute touche** force le mode clavier (`Menu_AnyKeyStart` @ `$102F`) — seul mode jouable sur Apple-1 |
| **Hors-mémoire** | table HGR : 72 lignes de clipping → `$D0xx` (poubelle ROM Applesoft) | redirigées vers `$98xx` (RAM libre) — `Hgr_LineHi` @ `$1C4B` ; sinon ces écritures corrompent la PIA `$D0xx` (clavier/affichage) et impriment des caractères parasites |

### Auto-relocalisation

`Relocate_Engine` (`$09D9`) recopie `$2800-$3FFF` vers `$8000` puis `jmp $8000` :
le moteur principal s'exécute en **`$8000+`**. Dans le listing, la zone
`$2800-$3FFF` est donc du code dont les adresses runtime sont décalées de
`+$5800` (ses références internes pointent vers `$80xx-$97xx`).

### Carte mémoire (runtime)

```
$0940-$27FF  loader + moteur bas (reste en place)
$2000-$3FFF  page HGR 1 (affichee) ; $2800-$3FFF aussi copie vers $8000
$4000-$7BFF  donnees (sprites, images HGR, tables son)
$8000-$97FF  moteur principal (apres relocalisation)
$09A0-$09A6  table des touches de controle
$9910/$9930  kbd_read / kbd_clear      (gen2_port.s)
$9950        KEYLATCH                  (gen2_port.s)
$9900        mon_WAIT                  (gen2_port.s)
```

> Shims en **RAM basse** (`$9900+`, au-dessus du moteur `$8000-$97FF` et de la
> page-poubelle `$9800`) : la zone `$F000-$FEFF` n'est pas garantie présente sur
> le vrai Apple-1 de Bernie, contrairement au 48K (`$0000-$BFFF`).

## Jouer (dans POM1)

Charger `software/Graphic HGR/BuzzardBait.txt` (active la carte GEN2
automatiquement). L'écran d'aide affiche les touches ; **appuyer sur
n'importe quelle touche** lance la partie (mode clavier forcé).

| Touche | Action | Écran |
|--------|--------|-------|
| **I** | tir | `I FIRE` |
| **J** | gauche | `J LEFT` |
| **K** | stop (annule la vitesse horizontale) | `K STOP` |
| **L** | droite | `L RIGHT` |
| **ESPACE** | sauter / battre des ailes | `SPACE  JUMP` |

Activer **Settings → « Keyboard autorepeat »** pour un déplacement continu
(les jeux d'arcade Apple II s'appuient sur l'auto-répétition matérielle du
clavier, absente par défaut sur l'Apple-1 TTL).

## Comprendre le jeu lui-même

`buzzard_bait.s` est optimisé pour le **ré-assemblage** (round-trip à l'octet) et
n'annote que les sites du *portage*. Pour **comprendre le jeu** — chaque routine,
les assets/sprites, la mécanique, et surtout le **moteur de sprites XOR** (sprites
pré-décalés en 7 phases, blit auto-modifiant, tables de lignes HGR) — voir
**[`DISASSEMBLY.md`](DISASSEMBLY.md)** : carte mémoire complète, boucle de jeu,
physique du tireur, oiseaux/personnages/nids, tir & score, galerie de sprites et
page-zéro commentée. *(Le jeu est un **shoot de protection**, pas un Joust — voir
l'errata en tête de DISASSEMBLY.md.)*

### Limites
La zone relocalisée `$2800-$3FFF` (adresses runtime décalées de `+$5800`), le code
auto-modifiant et les données interprétées comme instructions se ré-assemblent
identiques mais ne sont que partiellement *symbolisés* dans le `.s`.
`DISASSEMBLY.md` documente leur **fonction** ; quelques points (articulation des deux
systèmes de collision, identité exacte de 2-3 sprites) restent à confirmer par une
trace à l'exécution.
