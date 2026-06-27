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
| `buzzard_bait.s`   | Désassemblage da65 du binaire **porté** (entrée `$0940`). Se ré-assemble **byte-identique**. |
| `buzzard_bait.bin` | Image portée de référence (entrée da65 + cible du round-trip). |
| `buzzard_bait.info`| Fichier info da65 (labels matériels + hooks + zones de données) pour régénérer le `.s`. |
| `buzzard_bait.cfg` | Config ld65 (segment CODE en `$0940`). |
| `gen2_port.s`      | Routines **ajoutées** par le portage (WAIT son + latch clavier), chargées en RAM libre `$F000-$FEFF`. |
| `gen2_port.cfg`    | Config ld65 pour `gen2_port.s` (`$FB00..$FCB3`). |
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
> régénère. Décompte : 6 graphisme + 20 clavier + 4 touches + 5 son + 1 boot (crédit).

Le binaire d'origine est un jeu Apple II HGR autonome. Quatre familles de
changements suffisent (toutes visibles dans `buzzard_bait.s`) :

| Domaine | Apple II | Apple-1 + GEN2 |
|---------|----------|----------------|
| **Graphisme** | soft switches `$C050/51/52/54/57` | remappés `$C25x` (`bit $C251/$C254`, `lda $C250/$C252/$C254/$C257`) |
| **Son** | `jsr $FCA8` (WAIT Monitor) + bascules `$C030` | WAIT fourni en RAM `$FCA8` (`gen2_port.s`) ; `$C030` → ACI TAPE OUT |
| **Clavier** | `lda $C000` / `bit $C010` | latch logiciel `$FB80` : `jsr kbd_read` / `jsr kbd_clear` (`gen2_port.s`) |
| **Touches** | ← `$88` / → `$95` / `A` tir / `S` stop | **IJKL** : I=tir (`$C9`), J=gauche (`$CA`), K=stop (`$CB`), L=droite (`$CC`) — défauts en `Init_DefaultKeys` |
| **Boot** | crédit de crack `ALDO… CCB` | remplacé par `PORTAGE GEN2 HGR - UNCLE BERNIE` (`Boot_CreditText` @ `$0981`) |

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
$FB00/$FB10  kbd_read / kbd_clear      (gen2_port.s)
$FB80        KEYLATCH                  (gen2_port.s)
$FCA8        mon_WAIT                  (gen2_port.s)
```

## Jouer (dans POM1)

Charger `software/Graphic HGR/BuzzardBait.txt` (active la carte GEN2
automatiquement). Au menu **PLEASE SELECT**, choisir **(K) KEYBOARD**.

| Touche | Action |
|--------|--------|
| **I** | tir |
| **J** | gauche |
| **K** | stop (annule la vitesse horizontale) |
| **L** | droite |
| **ESPACE** | saut / battre des ailes |

Activer **Settings → « Keyboard autorepeat »** pour un déplacement continu
(les jeux d'arcade Apple II s'appuient sur l'auto-répétition matérielle du
clavier, absente par défaut sur l'Apple-1 TTL).

## Limites du désassemblage

`buzzard_bait.s` se ré-assemble à l'octet près. **Tous les sites du portage
sont annotés** (graphisme / son / clavier / touches + relocalisation). En
revanche ce **n'est pas** une reconstruction entièrement symbolique du *reste*
du jeu : le code auto-modifiant, les données interprétées comme instructions
(qui se ré-assemblent néanmoins identiques) et la zone relocalisée
`$2800-$3FFF` (adresses décalées de +$5800) ne sont que partiellement
documentés. C'est volontaire : la demande portait sur les zones **modifiées
pour fonctionner sous Apple-1**, qui sont, elles, exhaustivement commentées.
