# Buzzard Bait — Désassemblage commenté complet

> Compréhension **machine** du jeu *Buzzard Bait* (Sirius Software, 1983), tel
> que porté sur Apple-1 + carte **GEN2 HGR** d'Uncle Bernie et exécuté dans POM1.
>
> Ce document **complète** `README.md` (qui décrit le *portage*) et le listing
> `buzzard_bait.s` (désassemblage da65 ré-assemblable, annoté côté portage
> uniquement). Ici on documente **le jeu lui-même** : chaque routine, les
> données/assets, la mécanique de jeu, et surtout **le moteur de sprites XOR**.
>
> Méthode : désassemblage linéaire vérifié (harnais Python + da65), émulation des
> routines de construction de tables, rendu ASCII des sprites. Les adresses sont
> des adresses **CPU réelles**. La zone relocalisée `$2800-$3FFF` est notée à son
> adresse d'exécution `$8000-$97FF` (offset **+$5800**) quand c'est pertinent.

---

## 1. Vue d'ensemble

*Buzzard Bait* est un **jeu de tir / de protection** (pas un Joust — voir l'errata
ci-dessous). Le joueur tient un **tireur au sol**, en **bas** de l'écran, qui se
déplace **horizontalement** (`J`=gauche, `L`=droite, `K`=stop), **saute**
(`ESPACE`) et **tire vers le haut** (`I`). En **haut** de l'écran se trouvent
**trois nids** (verts). Des **oiseaux** (les « buzzards ») volent ; en bas se
promènent de **petits personnages**. Les oiseaux piquent pour **attraper** un petit
personnage et le **remonter dans un nid**, ce qui **fait grandir leur progéniture**
(= un nouvel oiseau apparaît). Le joueur doit **protéger les personnages** en
abattant les oiseaux ; tirer un oiseau qui transporte un personnage **libère**
celui-ci, qui retombe et est **sauvé**. Le tout en HGR Apple II, à pleine page.

> **Errata (v2)** — une première version de ce document décrivait à tort le jeu
> comme un *clone de Joust* (monture volante, joute « le plus haut gagne », œufs).
> C'est **faux** : il n'y a ni monture, ni joute. Le **moteur de sprites (§3) reste
> valable** (il décrit *comment* on dessine, indépendamment du thème) ; les sections
> de **mécanique (§1, §7, §8)** ont été corrigées d'après le code **et** la
> description du jeu réel. Ce que j'appelais « œufs » sont les **petits personnages**
> ; les « bouches de lave » sont les **nids** ; la « lance/flap-puff » est le **tir**.

### À l'écran (capture POM1)

La capture confirme le modèle :

- **En haut** : **3 nids** = coupes **vertes** abritant un petit **dôme blanc** (le
  « petit » qui grandit) ; des oiseaux viennent s'y poser.
- **Au milieu** : les **oiseaux** — ailes **orange** déployées, corps **bleu** —
  volent en piquant vers le bas.
- **En bas, sur la ligne de sol verte** : les **petits personnages** (silhouettes
  blanches) qui marchent, et **le joueur** : un **véhicule en dôme rayé
  rose/violet** qui se déplace horizontalement, saute et tire vers le haut.
- **Bandeau** : `SCORE` · `-2-` (niveau, var `$10/$11`) · `HIGH`.

Le binaire (`$0940-$7BFF`, 29 376 octets) est **autonome** : pas de DOS, pas de
ROM Applesoft requise. Il contient le moteur, la logique, tous les sprites, le
logo, les sons et les tables.

### Cycle de vie au démarrage

```
$0940  entry          NOP×14 (zone d'atterrissage du chargeur), puis :
$094E  Init_Title...  remplit l'écran TEXTE, affiche le crédit, joue le jingle
$09D9  Relocate       copie $2800-$3FFF -> $8000-$97FF, JMP $8000
$8000  cold start     init matériel + tables, tombe dans :
$8013  new game       reset partie (vies $40=3), tombe dans :
$804D  frame loop     ~40 JSR à plat, rebouclés chaque trame (vsync logiciel)
```

---

## 2. Carte mémoire du binaire

| Plage CPU | Contenu | Exécution |
|-----------|---------|-----------|
| `$0940-$094D` | 14 × `NOP` — zone d'atterrissage du chargeur disque d'origine | en place |
| `$094E-$09D8` | Boot : init écran-titre texte, crédit, jingle 1-bit | en place |
| `$09D9-$09FF` | `Relocate_Engine` (copie le moteur en `$8000`) | en place |
| `$0A00-$1B30` | **Moteur bas** : sprites, texte, score, son, RNG, collisions, particules | en place |
| `$1B38-$1C35` | **Moteur de sprites** (clear / OR-blit / XOR-blit / XOR-clip / Get_SpriteParams) | en place |
| `$1C44-$1E0A` | **Tables d'adresses de lignes HGR** (hi `$1C4B`, lo `$1D4B`, 192 entrées) | données |
| `$1E0B-$1FFF` | Font bloc du logo (`$1E0B`, 9×7) + tables ; reliquat ASCII "TRACK/SECTOR" | données |
| `$2000-$27FF` | **Inerte** : reliquat disque/Applesoft — devient le framebuffer à l'exécution | (écrasé) |
| `$2800-$3FFF` | **Moteur principal** → relocalisé en `$8000-$97FF` | à `$8000` |
| `$4000-$6BFF` | **Données** : bitmaps de sprites pré-décalés, tables de pointeurs, sons | données |
| `$6C00-$7B06` | **Code gameplay/son** : IA oiseaux, personnages/nids, tir, respawn, effets | en place |
| `$7B07-$7BFF` | Petites tables d'index/pointeurs | données |

> **Le framebuffer HGR page 1 est `$2000-$3FFF`** (entrelacement Apple II
> standard). Le moteur a été *déplacé hors de la page graphique* (`$2800-$3FFF`
> → `$8000`) précisément parce que cette plage devient l'écran. La zone
> `$2000-$27FF` contenait du code/données *jetables* (titre/menu en mode texte),
> écrasés une fois le mode graphique activé — d'où l'absence de relocalisation
> pour cette moitié.

---

## 3. Le moteur de sprites XOR — l'art de l'optimisation

C'est le cœur technique du jeu. L'objectif : **animer des dizaines de sprites en
HGR sans scintillement et sans détruire le décor**, sur un 6502 à 1 MHz. La
solution combine **quatre** techniques classiques de l'Apple II, exécutées ici
avec une rigueur exemplaire.

### 3.1 Le problème HGR

L'écran HGR Apple II, ce sont 192 lignes × 40 octets, où **chaque octet code 7
pixels** (bit 0 = pixel le plus à gauche) et le **bit 7** sélectionne le décalage
demi-pixel / la phase couleur NTSC. Deux difficultés :

1. **Adressage entrelacé** : l'adresse d'une ligne n'est pas linéaire. Ligne `y`
   → `$2000 + (y&7)·$400 + ((y>>3)&7)·$80 + (y>>6)·$28`. Calculer ça par ligne
   coûterait cher.
2. **Granularité 7 pixels** : pour poser un sprite au pixel `x` quelconque, il
   faut le **décaler de `x mod 7` bits** dans l'octet — un décalage multi-octets
   avec propagation de retenue, très coûteux par octet.

### 3.2 Technique 1 — Tables d'adresses de lignes précalculées

Au lieu de calculer l'entrelacement, deux tables de **192 octets** donnent
directement l'adresse de chaque ligne :

- **`$1C4B` `Hgr_LineHi[y]`** = octet de poids fort de l'adresse de la ligne `y`.
- **`$1D4B` `Hgr_LineLo[y]`** = octet de poids faible.

Vérifié (octets à `$1C44`) : `20 24 28 2C 30 34 38 3C` **répété** (`$1C44` et
`$1C4C`), puis `21 25 29 2D 31 35 39 3D` répété, puis `22 26 …` — soit un bloc de
64 octets répété 3× sur 192 lignes. **Chaque poids-fort couvre 8 lignes
consécutives** (les lignes `y` et `y+8` partagent le même octet de poids fort et ne
diffèrent que par le poids faible) : `$21` apparaît donc à la **ligne 16** (`$1C54`),
pas à la ligne 8. C'est bien l'entrelacement HGR page 1 standard. *(Deux bases
décalées de 7 selon l'entrée du blitter : `$1C44`/`$1D4B` et `$1C4B`/`$1D52`.)*
Le blitter fait juste :

```asm
LDY $1A            ; $1A = numéro de ligne courant
LDA Hgr_LineLo,Y   ; -> $14
LDA Hgr_LineHi,Y   ; -> $15   ($14/$15 = pointeur ligne, prêt pour (zp),Y)
```

> **Détail du portage** : les 72 lignes *hors écran* (184-255), qui valaient `$D0`
> sur Apple II (= ROM Applesoft, écriture sans effet = poubelle de clipping), ont
> été redirigées vers `$98` sur Apple-1 (RAM libre au-dessus du moteur), sinon
> elles écraseraient la PIA `$D0xx`. Cf. `README.md`.

### 3.3 Technique 2 — Décomposition pixel-X → (colonne, phase) par table

Plutôt que de décaler à l'exécution, le moteur précalcule **au boot** deux tables
de 256 octets, indexées par la **position-X en pixels** de l'objet :

- **`$0600` `Col[x]`** = colonne-octet (0-39) où commence le sprite → `$18`.
- **`$0500` `Shift[x]`** = **phase de décalage 0-6** dans l'octet → `$19` (et `X`).

Elles sont bâties par `$1B5C` (`Col`) et `$1B7A` (`Shift`). En les émulant on
obtient (extrait) :

```
pixelX : Col  Shift          pixelX : Col  Shift
 $00   :  0    0              $07   :  2    0
 $01   :  0    2              $08   :  2    2
 $02   :  0    4              $09   :  2    4
 $03   :  0    6              $0A   :  2    6
 $04   :  1    1              $0B   :  3    1
 $05   :  1    3              $0C   :  3    3
 $06   :  1    5              $0D   :  3    5
```

Motif : **4 phases paires {0,2,4,6} en colonne paire, 3 phases impaires {1,3,5} en
colonne impaire**, soit 7 sous-positions par paire de colonnes. C'est exactement
l'entrelacement de **phase couleur NTSC** de l'Apple II (les pixels adjacents
alternent de phase ; le « demi-dot shift » impose le stagger +1 entre octets
voisins). Le résultat : une résolution horizontale d'**un pas par unité de
pixel-X**, traduite en (colonne, phase) sans aucun calcul à l'exécution.

`Get_SpriteParams` (`$1C36`) fait la conversion en 6 instructions :

```asm
$1C36  STY $1A          ; ligne
$1C38  LDA $0600,X      ; X = pixel-X  ->  $18 = colonne-octet
$1C3D  LDA $0500,X      ;            ->  $19 = phase 0-6
$1C42  TAX              ; X = phase  (clé d'indexation du sprite pré-décalé)
$1C43  RTS
```

### 3.4 Technique 3 — Sprites **pré-décalés** (la pièce maîtresse)

Puisque `Get_SpriteParams` renvoie la **phase 0-6** dans `X`, chaque sprite est
stocké en **7 copies pré-décalées** (une par phase). Le blitter **ne décale
jamais** : il charge directement la bonne copie.

La preuve est mécanique et **universelle** dans le code : chaque routine de dessin
indexe une **paire de tables de pointeurs lo/hi séparées d'exactement 7 octets** :

| Drawer | Table lo | Table hi | Δ | Sprite (dim octets×lignes) |
|--------|----------|----------|---|----------------------------|
| `$1790` tir / perso porté | `$6758` | `$675F` | 7 | 2×$0A |
| `$173F` joueur (corps) | `$5474` | `$547B` | 7 | 4×$0D |
| `$173F` joueur (pattes) | `$5776` | `$577D` | 7 | 3×5 |
| `$0E92` objet (état A) | `$60C9` | `$60D0` | 7 | 5×$0E |
| `$0E92` objet (état B) | `$5ED1` | `$5ED8` | 7 | 5×$0E |
| `$0ECB` (vel<0 / ≥0) | `$57ED`/`$59E5` | `$57F4`/`$59EC` | 7 | 5×$0E |
| `$0EF7` ennemi | `$56B2` | `$56B9` | 7 | 2×$0D |
| `$0F36` lettre logo | `$53A9` | `$53B0` | 7 | 2×7 |

Δ = 7 partout. **CQFD : 7 phases pré-décalées par sprite.** L'idiome de dessin est
identique pour les ~31 drawers :

```asm
LDX position_pixelX      ; position horizontale de l'objet
LDY ligne                ; ligne du haut (ou Y de l'objet)
JSR $1C36                ; -> $18=colonne, $19=phase, X=phase(0-6)
LDA  sprite_lo,X         ; pointeur du bitmap pré-décalé pour CETTE phase
STA  $1BEE               ; auto-modifie l'opérande source du blitter XOR
LDA  sprite_hi,X
STA  $1BEF
JMP  $1BD2               ; lance la boucle XOR
```

> **Coût/bénéfice** : 7× la taille de chaque sprite en ROM, contre **zéro
> décalage** à l'exécution. Un blitter qui décale à la volée paie ~3 à 7 fois le
> temps par octet (LSR/ROL/propagation de retenue). Buzzard Bait préfère brûler la
> RAM/ROM (c'est statique, gratuit) pour rendre la boucle interne minimale. C'est
> *le* compromis gagnant de l'arcade Apple II.

Le choix **gauche/droite** (orientation) ou l'**état** (vivant / œuf / explosion)
sélectionne un **jeu de 7 phases différent** (ex. `$0E92` : si `$0780,X` est
négatif → table `$5ED1`, sinon `$60C9`). Adressage complet :
`sprite[état/orientation][phase 0-6]`.

### 3.5 Technique 4 — Dessin **XOR** (tracé = effacement)

La boucle interne combine la source par **OU-EXCLUSIF** avec l'écran :

```asm
            ; pré: $14/$15=ligne, $18=col départ, $25=col fin, $16=hauteur,
            ;      $1A=ligne courante, X=index linéaire dans le sprite
$1BD2  loop_row:
$1BD5  LDA $18 / ADC $17 / STA $25     ; $25 = colonne de fin = départ + largeur
$1BDB  LDA Hgr_LineLo,Y -> $14         ; (Y=$1A) adresse de la ligne courante
$1BE2  LDA Hgr_LineHi,Y -> $15
$1BE7  LDY $18                         ; Y = colonne de départ
$1BE9  inner:
$1BE9  CPY #$28      ; (2)  colonne >= 40 ? (clip bord droit)
$1BEB  BCS $1BF4     ; (2/3) -> saute le STORE mais continue (garde l'alignement)
$1BED  LDA $DCBA,X   ; (4)  octet du sprite  [opérande $1BEE/$1BEF auto-modifiée]
$1BF0  EOR ($14),Y   ; (5)  XOR avec l'écran
$1BF2  STA ($14),Y   ; (6)  réécrit
$1BF4  INX           ; (2)  avance dans la donnée sprite (linéaire, row-major)
$1BF5  INY           ; (2)  colonne suivante
$1BF6  CPY $25       ; (3)  fin de rangée ?
$1BF8  BNE $1BE9     ; (3)
$1BFA  DEC $1A       ; ligne suivante (de bas en haut)
$1BFC  DEC $16       ; compteur de hauteur
$1BFE  BNE $1BDB
$1C00  RTS
```

Propriétés :

- **XOR est auto-inverse** : redessiner le même sprite au même endroit **l'efface**
  et **restaure le décor** dessous (plateformes, lave, étoiles). Pas besoin de
  sauvegarder le fond. L'animation = *effacer (re-XOR à l'ancienne position) →
  bouger → re-XOR à la nouvelle position*.
- **Source en `LDA abs,X` (4 cycles)** et non `LDA (zp),Y` (5-6) : l'opérande
  absolue est **auto-modifiée** par le drawer (cf. 3.6). 1 cycle gagné par octet.
- **Index `X` linéaire** sur toute la donnée : le sprite est un blob *row-major*
  (largeur `$17` octets × hauteur `$16`), `X` le parcourt en continu ; l'adresse
  de ligne est recalculée par table, la colonne `Y` est remise à `$18` par rangée.
- **Clip bord droit** : `CPY #$28 / BCS` saute le `STA` mais fait quand même `INX`,
  donc le sprite reste **aligné** (on n'écrit pas hors page, mais on « consomme »
  l'octet). Le clip *haut/bas* est géré par la variante `$1C01` (cf. 3.7).
- Sens **bas → haut** (`DEC $1A`) : commodité d'écriture et de clip ; la donnée est
  ordonnée en conséquence.

Coût ≈ **29 cycles/octet dessiné** (≈ 13 si clippé). Pour un buzzard 5×14 =
70 octets, ≈ 2 000 cycles, soit ~2 ms — on tient aisément plusieurs dizaines de
sprites par trame.

### 3.6 Technique transverse — Source **auto-modifiée**

L'opérande de `LDA $xxxx,X` à `$1BED` (octets `$1BEE/$1BEF`) est **réécrit** par
chaque drawer avant le `JMP $1BD2`. On a recensé **31 sites** qui font
`STA $1BEE` / `STA $1BEF` — c'est-à-dire 31 routines de dessin de sprites, toutes
sur le même moteur. Une entrée alternative `$1BC8` charge la source depuis la
page zéro `$2B/$2C` (`STA $1BEE`/`$1BEF`) puis tombe dans la boucle ; c'est la voie
« pointeur déjà en zp ». D'où la fréquence : **`$1BD2` est la cible de saut la plus
fréquente du jeu (×22 en JMP)**.

### 3.7 Les variantes du blitter

| Entrée | Mode | Clip | Usage |
|--------|------|------|-------|
| `$1B45` | `Clear_HgrPage` | — | efface `$2000-$3FFF` (remplit de `$00`) |
| `$1B97` | **OU + force bit 7** (`ORA ($14),Y / ORA #$80`) | droit | **opaque, non effaçable** — *uniquement le logo* (`$0F36`) |
| `$1BC8`→`$1BD2` | **XOR** | droit | animation, source depuis `$2B/$2C` |
| `$1BD2` | **XOR** | droit | animation, source déjà auto-modifiée (les 31 drawers) |
| `$1C01` | **XOR** | droit **+ haut/bas** (`LDA $1A / CMP $31 / BCS`) | sprites au bord vertical de l'écran |

> Le **OU** force le bit 7 (`ORA #$80`) pour un aplat de couleur opaque : le logo
> « BUZZARD BAIT » est dessiné une fois et n'est jamais animé, donc l'absence
> d'effaçabilité du OU n'est pas un problème. **Tout ce qui bouge est en XOR.**

### 3.8 Synthèse — pourquoi c'est excellent

| Coût naïf | Solution Buzzard Bait | Gain |
|-----------|----------------------|------|
| Adresse de ligne entrelacée (mul/masquages) | 2 tables précalc. 192 o | ~0 cycle/ligne |
| Décalage `x mod 7` bits par octet | 7 copies pré-décalées + table phase | ~0 cycle/octet |
| Sauvegarder/restaurer le fond | XOR auto-inverse | 0 octet de backbuffer |
| `LDA (zp),Y` source (5-6 cyc) | `LDA abs,X` auto-modifié (4 cyc) | 1 cyc/octet |
| Routine par sprite | 1 boucle, opérande+pointeur auto-modifiés | code minimal |

Le moteur **déporte tout le travail variable vers des tables statiques** et garde
une boucle interne de 7 instructions sans branche (hors clip). C'est la
philosophie d'optimisation 6502 par excellence : *précalculer, ne jamais décaler,
écrire en place, réutiliser une seule boucle*.

### 3.9 Preuve visuelle du pré-décalage

Décodage de la table du sprite **joueur** (`$5474` lo / `$547B` hi, 7 entrées) :
les 7 phases pointent vers 7 bitmaps distincts (`$5482, $5552, $54B6, $5586,
$54EA, $55BA, $551E`). Rendu des phases 0/1/2 (4 octets × 13 lignes, `#`=pixel
allumé, `:`=bit 7) :

```
phase 0 @ $5482      phase 1 @ $5552      phase 2 @ $54B6
.#.....              ..#....              ...#...     <- même motif,
.#.#...              ..#.#..              ...#.#.        décalé de +1 pixel
..##..# .##...#      ...##.. #.##...      ....##. .#.##.   par phase
#.#.#.#  (jambes)    .#.#.#. #.#.        ..#.#.# .#.#
```

Le motif (silhouette + jambes) **glisse d'un pixel vers la droite à chaque
phase**, le débordement passant dans l'octet suivant. C'est exactement du stockage
**pré-décalé** : 7 copies figées en ROM, zéro décalage à l'exécution. CQFD.

---

## 4. Démarrage et relocalisation

### `$094E` Init_TitleScreen
Remplit l'écran TEXTE page 1 (`$0400-$07FF`) de `#$A0` (espaces), passe GEN2 en
TEXTE/PAGE 1 (`$C251/$C254`), puis décode le crédit de boot depuis `Boot_CreditText`
(`$0981`, 40 octets `EOR #$AB`) vers `$0400` → « GEN2 HGR PORT - UNCLE BERNIE ».
Tombe dans le **jingle** (`$0978-$09CE`) : mélodie 1-bit par bascules `$C030`
cadencées par `JSR mon_WAIT` (`$9900`, shim du portage remplaçant `$FCA8`).

### `$09D9` Relocate_Engine
Boucle de copie à octet de poids fort auto-modifié : `LDA $2800,X / STA $8000,X`,
incrémente les deux pages-fortes jusqu'à ce que la destination atteigne `$98`,
patche `$09D8` en `RTS`, puis `JMP $8000`. Copie donc **`$2800-$3FFF` → `$8000-$97FF`**
(24 pages = 6 Ko) et passe la main au moteur relocalisé.

## 5. Moteur bas (`$0A00-$1B30`) — catalogue des routines

### Rendu graphique de base
- **`$1B38` Gen2_GraphicsOn** — active HGR plein écran (soft switches GEN2
  `$C250/$C257/$C252/$C254`).
- **`$1B45` Clear_HgrPage** — remplit `$2000-$3FFF` de `$00`.
- **`$1B5C` / `$1B7A`** — construisent au boot les tables `Col[]` (`$0600`) et
  `Shift[]` (`$0500`) du décodage pixel-X (cf. §3.3).
- **`$1C36` Get_SpriteParams** — pixel-X (`X`) + ligne (`Y`) → colonne (`$18`),
  phase (`$19`=`X` retourné). Appelée **×33** (autant que de dessins de sprite).
- **`$1B97` / `$1BC8` / `$1BD2` / `$1C01`** — blitters OU / XOR (cf. §3.7).
- **`$0F1C` Invert_HgrPage** — `EOR #$80` sur toute la page (flash/inversion phase).
- **`$0A00` Hgr_XorPatternFlash** — remplit toute la page d'un motif 2-octets
  choisi par `$0992` (0-4) depuis `$0A45/$0A4A` ; flash pulsé de l'écran-titre
  (re-XOR pour effacer). *(Le brief l'appelait « Draw_HgrGlyph » — à tort.)*

### Sprites de jeu (tous via `Get_SpriteParams` + XOR `$1BD2`)
- **`$173F`** dessine le **joueur (tireur)** : corps 4×$0D (table `$5474/$547B`)
  puis, sous condition, une 2ᵉ partie 3×5 (`$5776/$577D`).
- **`$1790`** dessine un sprite 2×$0A (`$6758/$675F`) — sert au **projectile** du tir
  (`$7AAF`) et au **petit personnage** porté, acteur `$7A`.
- **`$0E92`** dessine un **oiseau** générique 5×$0E (orientation via bit de
  `$0780,X` → table `$60C9` ou `$5ED1`).
- **`$0ECB`**, **`$0EF7`**, **`$12DE`**, **`$1323`** — variantes pour d'autres
  classes d'acteurs (personnages, projectiles ennemis, créature volante…).
- **`$16AB` Draw_Enemy** (×17) — rendu de l'acteur d'indice `$23` (13×2) ;
  `$1624` est le pilote IA/déplacement qui l'appelle.
- **`$12B8`** — petit blitter XOR 2 octets pour fragments/projectiles
  (`$129D/$12AC/$1246/$1269` choisissent la table source selon le type).

### Texte et score
- **`$17B1` Draw_GlyphString**(ptr=A:X, len=Y, col=`$18`, row=`$1A`) — imprime une
  chaîne HGR ; **auto-modifie** l'opérande source (`$17BC/$17BD`) ; parcourt la
  chaîne **du dernier au premier** caractère (les chaînes sont stockées à
  l'envers). Le leaf `$1892 Draw_Glyph` rend un glyphe (font perso, `SBC #$C1`).
- **`$1A0E` Draw_ScoreDigit** — un gros chiffre HGR 7 lignes (font `$19C8`).
- **`$1A41`** init affichage score ; **`$1A63` Draw_Score** (suppression des zéros
  de tête) ; **`$1AAD` Draw_HiScore** ; colonnes depuis `$1AA5`.
- **`$1AD0` Add_To_Score**(A=points) — addition **décimale** avec retenue dans le
  tableau BCD `$00-$07` ; gère le hi-score et la **vie bonus** (`INC $40`,
  `$1217` dessine l'icône). Routine clé du scoring.
- **`$1217` Draw_LivesIcon** — indicateur de vies (`LDX $40`).

### Entrée, RNG, utilitaires
- **`$1B2C` Get_Key** — lit le clavier via shim `kbd_read`, force majuscule
  (`AND #$DF` si ≥ `$E0`) ; bit 7 = touche présente. Le getkey canonique (×17).
- **`$1B23` Wait_ForKey** — `kbd_clear` puis attend une touche.
- **`$15FD` PRNG** (×33) — générateur logiciel 4 octets sur `$1F/$20/$21/$22`
  (mélange `+$65`, `EOR`, `SBC $58`, `ROL`+`EOR`). Sert aux positions de spawn,
  vitesses de particules, IA. (*Détail confirmé par lecture, valeurs exactes des
  constantes à revérifier.*)
- **`$161B` / `$0FFD`** — boucles de temporisation (cadence d'animation).
- **`$13BC` Collision_Test** (×18) — recouvrement de boîtes englobantes :
  cet objet (`$2A/$2B/$2C`) vs l'autre (`$2E/$2F/$30`), avec correction de
  bouclage X (`$2A≥$F0`). **Retenue = collision.** Cœur de la détection de chocs.
- **`$11E6` / `$11F6`** — comptage d'ennemis actifs et **spawn** (apparition d'un
  acteur dans un slot libre).

### Particules / explosions
- **`$0B5F` Spawn_Explosion** — sème 16 fragments (x `$0900,X`, y `$0910,X`,
  dx `$0920,X`, dy `$0930,X`), vitesses aléatoires (`$15FD`) + germes signés.
- **`$0BE1` Update_Explosion** — anime les 16 fragments (gravité `INC dy`, friction
  dx, clamp → mort `$F3`), dessin via `$0C40 Draw_Fragment`.

### Menu / écran-titre
- **`$0C7C` Menu_DrawHelp** — efface HGR, écrit 4 lignes d'aide (`$17B1`, chaînes
  inversées à `$0CC4`) : « J LEFT / L RIGHT / I FIRE / K STOP / SPACE JUMP /
  PRESS ANY KEY TO PLAY ».
- **`$102F` Menu_AnyKeyStart** — *(portage)* n'importe quelle touche force le mode
  clavier (`$35=$FF`) et démarre (`JMP $801D`).
- **`$107F` Setup_PlayScreen** — construit l'écran titre+score, grand logo
  (`$0FB6`/`$0F36`), sol animé (`$10E0`), puis `JMP $801D`.
- **`$0F36` Draw_LogoChar** — une grosse lettre-bloc : lit un bitmap 9×7 dans la
  font `$1E0B`, et pour chaque bit allumé pose un bloc 2×7 en **OU-blit** (`$1B97`,
  opaque) à la grille (`$51`,`$52`). `$0FB6` enchaîne les lettres de « BUZZARD BAIT ».
- **`$10E0` + `$1138`** — sol/bandes de couleur (masques `$55/$2A/$AA/$D5`).

## 6. Moteur principal relocalisé (`$8000-$97FF`) — boucle de jeu

> Adresses d'exécution `$80xx-$97xx` ; le code source est à `−$5800` dans le binaire.

### Trois points d'entrée en cascade

```
$8000  COLD START   (1 fois)   init matériel + tables
  $8000 JSR $1B5C / $1B7A   construit Col[]/Shift[]
  $8006 JSR $1B38           HGR ON
  $8009 JSR $1B19 / $1A41   init vidéo / score
  $800F JSR $80E6           Init_KeyBindings (table $09A0-$09A6)
        ↓ (tombe dans)
$8013  NEW GAME     (à chaque mort/game-over via JMP $8013)
  $8013 JSR $1B45           efface l'écran
  $8016 LDA #0 / STA $40
  $801A JSR $107F           écran titre/sélection
  $8020 LDA #5 / STA $37    *** $37 = seuil de spawn/difficulte = 5 (PAS les vies) ***
  $8024 JSR $8A04           Init_Game_State (remet à zéro ~30 vars + tableaux d'objets)
  $8027 … JSR $1837/$1A63/$1AAD/$6CB3   init terrain + sous-systèmes
        ↓
$804D  FRAME LOOP   (rebouclée chaque trame)   ~40 JSR à plat
```

### La boucle de trame (`$804D`)

Liste plate, sans boucle interne : ~40 `JSR` exécutés de haut en bas par trame,
mêlant routines du moteur (`$8xxx`), du moteur bas (`$0A-$1Bxx`) et du bloc
gameplay (`$6C-$7Bxx`). Extrait structurant :

```
$804D JSR $88A6   Wave_Scheduler : compteurs d'anim + script de vagues + niveau
$8050 JSR $6C1C   logique nids/objets (3 slots)
…     (IA ennemis, collisions, dessins, son — voir §8)
$809B JSR $8111   Draw_Sweeper      (ennemi balayeur de bord)
$80AA JSR $8137   Update_Sweeper
$80B0 JSR $9009   Update_Roamer     (ennemi descendant)
…
$80C8  ── queue de boucle (machine à états maître) ──
       LDA $40 ; BMI $80DD      ; $40 < 0  → GAME OVER
       LDA $3E ; BEQ $80D4      ; $3E == 0 → trame suivante
       LDA $3B ; BEQ $80D7
$80D4  JMP $804D                ; cas courant : trame suivante
$80D7  JSR $6E34 ; JMP $804D
$80DD  JSR $15EF ; JSR $9003 (effacement transition) ; JMP $8013  ; → NOUVELLE PARTIE
```

### Routines moteur notables
- **`$80E6` Init_KeyBindings** — écrit la table de contrôle `$09A0-$09A6`
  (I/J/K/L/Z/espace) — voir §7.
- **`$8A04` Init_Game_State** — remise à zéro par partie : ~30 vars zp, registres
  son `$09B1-$09B4`, drapeaux `$40=3`/`$3E=$FF`/`$7D=$FF` ; tableaux d'objets
  `$F4,X=$FF` (6), `$9E,X=0` (8), `$83,X=0` (3) ; germes `$56/$57=$E0`, `$63=3`.
  *(Ne touche pas `$37` — les vies sont posées par l'appelant.)*
- **`$88A6` Wave_Scheduler** — le **séquenceur de vagues**, décodé en détail en
  **§6.1** ci-dessous. En bref : cycle les compteurs d'anim `$32`/`$33`, puis joue un
  **script temporel** piloté par le compte-à-rebours `$3F`.
- **`$8530` / `$86BC`** — résolveur de **contact** (probablement mode démo, §8.1/§8.8) :
  test de recouvrement joueur vs 8 objets `$0700+` (`$13BC`), poussée/rebond pondéré
  par la hauteur, arme `$5C=5`, inverse la vitesse de l'objet.
- **`$9000-$901E`** — table de saut (trampolines) vers les routines son/sprite
  (`$9037` Play_SFX 1-bit via `$905C` ; `$923D` Update_Roamer ; `$9299`
  draw multi-segment ; `$92ED` effacement de transition de niveau).

### 6.1 Le séquenceur de vagues `$88A6` — *décodé*

Le scheduler tourne **en premier** chaque trame. Il fait deux choses : cycler les
**compteurs d'animation globaux**, puis jouer un **script temporel** de vague.

```asm
$88A6  DEC $32 ; BPL + ; LDA #2 ; STA $32    ; $32 : compteur d'anim 3 phases (2→1→0→2)
$88AE  DEC $33 ; BPL + ; LDA #9 ; STA $33    ; $33 : compteur d'anim 10 phases (9→…→0→9)
$88B6  LDA $3F ; BEQ RTS                     ; $3F = 0 : script au repos → rien
$88BA  DEC $3F                               ; sinon : avance la timeline
$88BC  LDA $63 ; BNE + ; JSR $8970           ; si $63=0 : rampe d'intro "GET READY"
$88C3  LDA $3F ; <chaîne de CMP>             ; dispatch sur la valeur courante de $3F
```

**`$3F` est un compte-à-rebours-timeline** (des *timestamps* d'événements).
Il est **armé à `$FF` quand une vague est nettoyée** : `$7196` (`Bird_Killed`) fait
`DEC $36` (oiseaux restants) ; à 0 → **`$3F=$FF`**, `$74=$2D` (durée de la rampe
d'intro), `DEC $63`. Donc **chaque vague abattue relance la séquence suivante**.
Pendant l'intro (`$74≠0`, via `$8970`), `INC $3F` *maintient* la timeline en haut
(les nids clignotent, « get ready ») jusqu'à épuisement de `$74`, puis elle se
déroule. À l'init de partie, `$3F` est posé par `$8A04` (`$8A34`).

Le **script de vague**, à mesure que `$3F` descend de `$FF` vers 0 :

| `$3F` | Cible | Événement |
|-------|-------|-----------|
| `$FE`/`$FA`/`$F6` | `$15C1` (slot 2/1/0) | **animer/allumer les 3 nids** (via `$AF,X`, l'état de flamme du nid) |
| `$F4` | `$309D` | `INC $12` (rampe 0→5, plafonnée) — bonus/difficulté par vague |
| **`$F2`** | `$3151` | **NIVEAU +1** : `INC $10` ; si `$10`=10 → `$10=0`, `INC $11` ; redraw `$1811` |
| `$EE` | `$3096` | recompter les actifs (`$11E6` → `$13`), si `$63≠0` |
| `$EB`/`$0C` | `$3162` | `JMP $17DD` (HUD / dessin) |
| `$E5` | `$3119` (slot 0) | **spawn oiseau** slot 0 (puis `+$12` au score) |
| `$E4` | `$3168` | `JMP $81FB` si `$63=0` |
| `$E0` | `$3165` | `JMP $77DC` |
| `$D5`/`$C5`/`$B5`/`$A5`/`$95` | `$3119` (slot 1/2/3/4/5) | **spawn oiseaux** échelonnés (`+$12` chacun) |
| `$10` | `$315F` | redraw niveau (`$1811`) |
| **`$08`** | `$312E` | **spawn vague COMPLÈTE** : les 6 slots d'un coup (`$11F6` ×6), `$5C=0` |
| `0` | — | RTS (timeline terminée) |

Mécanique des spawns : **`$3119`** ne lâche le slot `X` que si `X ≤ $13` (nombre
d'actifs, recompté à `$EE`) → un **remplissage progressif** ; **`$15C1`** n'anime pas
des oiseaux mais les **3 nids** (lecture/écriture de `$AF,X`). Le `+$12` (`$3127` :
`LDA $12 / LDX #1 / JMP Add_To_Score`) ajoute la rampe `$12` aux dizaines du score à
chaque spawn de la timeline.

> **Lecture d'ensemble** : nettoyer une vague (`$36→0`) ⇒ `$3F=$FF` ⇒ **niveau +1**
> (`$F2`), nids qui s'allument (`$FE/$FA/$F6`), puis **arrivée échelonnée** des
> oiseaux (`$E5…$95`), close par un **lâcher complet de 6** (`$08`). Tout le rythme
> d'une partie tient dans cette unique table de jalons sur `$3F` — c'est le « niveau »
> et la « difficulté » encodés en données, pas en code (cf. §12, logique table-dirigée).

## 7. Physique du joueur — tireur au sol (`$78BA`/`$78FB`/`$7976`)

Le joueur est un **personnage au sol**, en **bas** de l'écran (ligne `Y=$B7`=183),
qui marche horizontalement, **saute** et **tire vers le haut**. Modèle **vérifié** :

### Déplacement horizontal (`$78E1`-`$78F9`)
La consigne de direction `$1C` (du lecteur en jeu `$7A7C`, §8.4) déplace
`$1B` (X joueur) de **±2 px/trame** (`$1C` = `$01` → droite, `$FF` → gauche, `0` →
stop), borné à `< $7F`. Pas de vitesse continue : c'est un déplacement direct
gauche/droite le long du sol.

### Saut + gravité (`$78FB`, `$7976`)
`$39` = vitesse verticale, avec la **sentinelle `$83` = « au sol »**. Sauter
(`$7976`, touche ESPACE) n'est possible qu'au sol (`$39==$83`) et pose
**`$39 = $F8` (−8, impulsion vers le haut)**. Ensuite, chaque trame (gating sur
`$32`) :

```
si $39 ≠ $83 :                 ; en l'air
   INC $39                     ; gravité : la vitesse remonte −8 → … → 0 → +1 …
   $3A (Y) += $39              ; intègre la position
   si $3A ≥ $B7 :              ; touché le sol
      $39 = $83 ; $3A = $B7    ; atterri (re-calé sur la ligne de sol)
```

C'est un **saut parabolique** classique (impulsion + gravité accélérée), **pas** un
vol. Le joueur revient toujours se poser sur la ligne `$B7`.

### Tir (`$7AAF`, touche I)
Tirer émet un **projectile vers le haut** : il apparaît 9 px au-dessus du joueur
(`$1D,X = $3A−9`, `$78,X = $1B+4`, 2 slots), est dessiné par `$1790`, et **monte**
de 4 px/trame (`$7AD9` : `$1D,X −= 4`) jusqu'à disparaître en haut. Un tir qui
percute un oiseau le tue (§8.5). C'est le « tir » du jeu (ce que la v1 appelait à
tort « lance/flap-puff »).

> **Chemin de contrôle secondaire = mode démo (confirmé)** — le moteur relocalisé
> contient *aussi* un lecteur `$8833` (file `$3033`) + intégrateur `$87A9`
> (file `$2FA9`) d'un schéma **volant** (rebond sur 4 bords, vitesses `$5A/$5B`).
> Une **trace POM1** (§8.8) confirme qu'il ne tourne **que dans l'attract auto-joué**
> et **jamais** pendant la vraie partie (sol + saut + tir via `$7A7C`/`$78BA`).

---

## 8. Mécanique de jeu

> **Comment on joue.** Le joueur (tireur au sol, §7) **abat les oiseaux en tirant
> vers le haut** (`$7AAF`→`$70CD`, marque des points, §8.5) et **meurt** si un
> oiseau touche son **corps** (`$7237`→`$6C74`, §8.7). Le but est de **protéger les
> petits personnages** : les oiseaux les attrapent au sol et les remontent dans les
> nids (§8.6). La routine `$2EC7` ci-dessous (moteur relocalisé) est un **résolveur
> de contact** opérant sur un **tableau d'objets distinct** (`$0700+` page 7) ;
> comme le schéma de contrôle volant de §7, elle appartient probablement au
> **mode démo** et **non** au cœur du jeu livré (qui marque les points et la mort via
> le bloc gameplay, oiseaux en page zéro `$B2-$DF`). *Articulation non tranchée — §8.8.*

### 8.1 Résolveur de contact à hauteur (`$2EC7`) — *code du **mode démo** (confirmé §8.8)*

Chaque trame, **`$8530`/`$86BC` + `$2EC7`** balaient 8 objets `$0700+` et testent le
recouvrement avec le joueur :

```
$2EC7  si $3E (joueur actif) < 0 → sortie
       $27 = $62 (compteur d'ennemis ≈ 7)
       JSR $86BC   ; pose la taille de boîte ($2D-$30 = $0E = 14 px)
       JSR $13AB   ; charge la boîte JOUEUR ($29 X, $2B/$2C Y)
  boucle ennemis (X = $27 … 0) :
       $2A = $0700,X (X ennemi) ; $2C = $0740,X (Y ennemi)
       JSR $13BC   ; test AABB joueur vs ennemi
       BCC → HIT   ; (retenue effacée = recouvrement)
```

**`$13BC` Collision_Test** : test de boîtes englobantes (AABB) sur X et Y avec
**gestion du bouclage horizontal** (`$2A ≥ $F0` → repli via `$2E`). Retenue
effacée = collision.

À la collision, **`$8701` (file `$2F01`)** et **`$8722` (file `$2F22`)** dérivent
la poussée à partir des **deltas de position** — la hauteur relative oriente le
rebond :

```
$8701  poussée horizontale :  d = joueur.X − ennemi.X
       d ≥ +4  (CMP #$04) → joueur poussé à DROITE ($5A=$03), ennemi.Xvel inversé ($0780,X EOR #$FE)
       d ≤ −5  (CMP #$FC) → joueur poussé à GAUCHE ($5A=$FD), idem

$8722  poussée verticale (selon la hauteur) :  d = joueur.Y − ennemi.Y
       d ≥ +5  (CMP #$05 ; joueur PLUS BAS) → joueur rebondi vers le BAS ($5B=$06)
       d ≤ −4  (CMP #$FD ; joueur PLUS HAUT) → joueur rebondi vers le HAUT ($5B=$FA),
                et l'objet ennemi passe en état $07C0,X = 3
```

> Sur l'écran HGR, **Y croît vers le bas**. Selon la hauteur relative, l'objet est
> repoussé vers le haut/bas et `$5C=5` (sidération) est armé. C'est un **rebond
> élastique**, pas la décision de score/mort du jeu (§8.5/§8.7). Comme il s'appuie
> sur les vitesses `$5A/$5B` et le tableau `$0700+` du **chemin volant** (§7), il
> appartient vraisemblablement au **mode démo** — documenté ici par exhaustivité.

Le gestionnaire de hit (`$2EEB`) : redessine l'objet (`$0E92`), applique
`$8701`+`$8722`, arme `$5C=5`, puis `JMP $87A9` (intégrateur volant).

### 8.2 Score, hi-score et vie bonus (`$1AD0`) — *vérifié*

Le score est un tableau **décimal** de 7 chiffres en page zéro `$00-$06` (`$00` =
unités). `Add_To_Score` (`$1AD0`, points dans A, entré avec X=0) additionne avec
propagation de retenue base-10 (`CMP #$0A / SBC #$0A / INX`). Ensuite :

- **Hi-score** : compare `$00…` au hi-score `$08-$0D` (6 chiffres) ; si dépassé,
  lève `$24` (dessine une 2ᵉ copie/ombre) et met à jour.
- **Vie bonus** : quand le chiffre `$03` vaut `0` ou `5` (donc à intervalle régulier
  de score), et si le drapeau `$57` n'a pas déjà servi → **`INC $40`** (une vie de
  plus), `$57=$FF` (anti-répétition), `$1217` redessine l'icône de vies. Schéma
  classique « vie tous les N×5000 points ».

### 8.3 RNG — `$15FD` (*vérifié*)
Générateur pseudo-aléatoire logiciel non linéaire sur 4 octets `$1F/$20/$21/$22` :
`$1F+=$65 ; $20^=$1F ; $21-=$58 ; $22=ROL($22)^$21 ; A=$22^$1F^$20`. Source de
hasard unique du jeu (positions de spawn, vitesses de particules, IA, scintillement
de respawn). Appelé ~33×.

### 8.4 Contrôles en jeu et le tir (`$7A7C`, `$7AAF`) — *vérifié*

Le lecteur clavier **en jeu** est `$7A7C Get_KeyIngame` :

```
$7A7C  JSR $1B2C            ; lit le clavier
       BPL → applique $1C   ; pas de touche
       CMP $09A0 → TIR      ; JSR $7AAF (tire un projectile vers le haut)
       CMP $09A2 → $1C=$FF  ; GAUCHE
       CMP $09A3 → $1C=$00  ; STOP
       CMP $09A1 → $1C=$01  ; DROITE
       JSR $9930            ; applique la direction $1C au tireur + redessine
```

| Touche | Var | Action |
|--------|-----|--------|
| **I** (`$09A0`) | — | **TIR** (`$7AAF`, projectile vers le haut) |
| **J** (`$09A2`) | `$1C=$FF` | gauche |
| **L** (`$09A1`) | `$1C=$01` | droite |
| **K** (`$09A3`) | `$1C=$00` | stop |
| **ESPACE** (`$09A4`) | — | **saut** (`$7976`, §7) |

Méta-touches via `$793E` : `#$92`→pause, `#$9B`→quitter (`$0A50`), `#$93`→bascule
son (`$79C9`). `$1C` = consigne de direction (relative au clavier, ou X-cible
absolue en mode paddle via `$7843/$7863`).

> **Touches reconfigurables** : la table de contrôle `$09A0-$09A6` est éditable via
> un **écran « DEFINE KEYBOARD »** (`$936D` dessin, `$938B` saisie ; runtime). Il
> liste les actions (SHOOT/UP/DOWN/JUMP/STOP…, colonne gauche `$9458`) et la touche
> liée (colonne droite `$93C4`, avec noms spéciaux « LEFT ARROW », « SPACE »,
> « COMMA », « BACKSLASH »), ESC pour sortir. Les défauts IJKL+espace du portage
> Apple-1 sont posés par `Init_DefaultKeys` (`$28EA`).

**Le tir (`$7AAF`)** trouve un slot libre (`$1D,X==0`, 2 slots → 2 tirs simultanés
max), pose le projectile 9 px au-dessus du joueur (`$1D,X=$3A−9`, `$78,X=$1B+4`),
le dessine (`$1790`), arme un son (`$41=$14`). Le projectile **monte** de 4 px/trame
(`$7AD9` : `$1D,X−=4`) et disparaît en haut. Toucher un oiseau = kill + points (§8.5).

### 8.5 Tirer les oiseaux, kills et score — *vérifié*

> Les **oiseaux** occupent 9 slots page-zéro (`$B2`=X, `$BB`=Y, `$C4`=orientation,
> `$CD`=trame, `$D6`=état, `$DF`=personnage attrapé/lié), groupés 0-2 / 3-5 / 6-8 —
> **trois types** d'oiseaux avec sprites/tailles/scores distincts.

**ABATTRE — un tir touche un oiseau (`$70B0`→`$70CD`)** : pour chaque projectile
actif, construit sa boîte (`$1398`, 2×9) et la teste, dans l'ordre, contre les 3
**nids**, puis les oiseaux 6-8 (boîte 9×4 → **+300**), 3-5 (12×7 → **+50**), 0-2
(14×10 → **+100**). À l'impact : `$718A` (effet + son), le projectile expire. Si
l'oiseau **transportait un personnage**, celui-ci est **libéré** (`$71B0` met son
état `$F4=$01` → il retombe, §8.6) ; le joueur récupère ainsi le petit personnage.

**MOURIR — un oiseau touche le corps du joueur (`$7237`)** : boîte joueur
(`$13AB`, 9×4) vs tous les oiseaux ; tout recouvrement → tue l'oiseau (score crédité)
**ET** `JMP $6C74` (le joueur explose, §8.7). Il faut donc **tirer** les oiseaux,
pas les percuter.

**Les 4 (et seuls) sites de score du jeu** — tous `JSR $1AD0` :

| Adresse | Points | Événement |
|---------|--------|-----------|
| `$71D1` | **+100** | abattre un oiseau type 0-2 (gros) |
| `$71E4` | **+300** | abattre un oiseau type 6-8 (rapide) |
| `$71FA` | **+50** | abattre un oiseau type 3-5 (moyen) |
| `$709A` | **+100** | **récupérer un petit personnage** (qui retombe au sol) |

`$7196 Enemy_Killed_Bookkeeping` : `DEC $36` (oiseaux en vol) ; à 0 → `$3F=$FF`
(vague nettoyée), `$74=$2D`, `DEC $63`. `$71B0` libère le personnage lié
(`$F4[$DF,X]=$01`) — sauf le type 6-8 (tué par `$71D7`) qui n'en transporte pas.

### 8.6 Les petits personnages : capture, nids et sauvetage — *vérifié*

C'est **le cœur du jeu**. Les **petits personnages** occupent 6 slots
(`$E8`=X, `$FA`=Y, `$F4`=**état**, `$EE`=orientation). L'état `$F4` est leur machine :

| `$F4` | sens | handler |
|-------|------|---------|
| `0` | **au sol** (marche sur la ligne `$B7`), OU **déposé dans un nid** (`Y≈$26`) | `$6D89` dépose au nid, `$6DD2` |
| `$01–$1D` | **en chute** (relâché par un oiseau abattu ; valeur = vitesse) | `$7035` |
| `$1E–$2A` | animation (`+2`/trame) | `$7019` |
| `$80` | **émerge** par le bas (`$C9`→`$B7`) en début de vague | `$6FF4` |
| `$FF` | **porté par un oiseau** (position asservie à l'oiseau), ou consommé | — |

**Le flux, vérifié dans le code :**

1. **Apparition** — les personnages émergent par le bas de l'écran (`$80` : `$FA`
   décroît `$C9`→`$B7`) puis **marchent au sol** (`$F4=0`, `$FA=$B7`).
2. **Capture (`$72B9`)** — un oiseau **non chargé** (`$DF,X` < 0) dont la boîte
   recouvre un personnage **au sol** (`$F4=0`, `$FA=$B7`) l'**attrape** : il **lie**
   `$DF,X = index du personnage`, met le personnage en état **porté** (`$F4=$FF`) et
   **asservit sa position à l'oiseau** (`$E8/$FA` = pos oiseau + offset).
3. **Transport vers un nid** — l'oiseau remonte ; le personnage le suit (haut de
   l'écran).
4. **Dépôt au nid (`$6F71`)** — quand le personnage porté atteint la boîte d'un des
   **3 nids** (X `$7B07`, Y `$7B13`), il est **consommé** (`$F4=$FF`), le nid
   s'**anime** (`$1522/$1545`, état de flamme `$AF`) et **`INC $36`** : **un nouvel
   oiseau apparaît** — *« faire grandir leur progéniture »*. **Mauvais pour le joueur.**
5. **Sauvetage** — si le joueur **abat l'oiseau porteur** (§8.5), `$71B0` met le
   personnage en chute (`$F4=$01`). Il **retombe** (`$7035` : `$FA += $F4`, `INC $F4`).
   Au sol (`$FA ≥ $B7`), s'il est récupéré (`$7079` : joueur proche, à ≤ 7 px) →
   **+100 points** (`$709A`). Sinon il continue (`$7019`/`$7069`).

> Les **3 nids** sont en haut : X = `$7B07` = `$00 $3D $7A` (gauche/centre/droite),
> Y = `$7B13` = `$2C $34 $2C` (44/52/44, apparié à `$7B07` partout — dessin `$70DD`,
> spawn `$6FE1`, proximité `$6F9A`). La table `$7B0D` = `$26 $2E $26` (Y−6) est la
> **boîte de dépôt du captif** (`$6D89`), pas la position des nids.
> Le joueur, lui, est en bas (Y=`$B7`). Tout l'enjeu est
> d'**intercepter les oiseaux avant qu'ils ne déposent un personnage dans un nid**.

### 8.7 Mort du joueur, respawn et nids

- **Mort** : `$6C74 Player_Explode` — pose `$3E=$FF`, sème **64 particules**
  (`$0400/$0440/$0480/$04C0`), timer `$3B=$3C` (60 trames), crépitement haut-parleur
  (`$6D25`). Déclenchée par un **oiseau qui touche le joueur** (`$7237`) ou par un
  danger tombant (`$6E6F`).
- **Respawn** : `$54` décompte ; à 0, `$6E34 Player_Spawn` : **`DEC $40` (−1 vie)**,
  replace le joueur, `$39=$83` (posé au sol), redessine. **Game-over** quand `$40 < 0`
  (queue de boucle `$80C8`).
- **Les 3 nids** : `$6C1C`/`$6C3F` ouvrent/ferment le nid, `$6E0B` anime, `$6FD2`
  lâche un oiseau frais (slots 6-8) — réapprovisionnement des vagues, indépendamment
  du dépôt de personnage.
- **Projectiles ennemis (×8)** : `$96`=X/`$9E`=Y/`$86`=vel — lâchés par les oiseaux
  (`$6E9B`, niveau ≥ 2), chute avec gravité (`$6F2C`), tuent le joueur au contact
  (`$6E6F`). Dessinés par `$12DE`.
- **Créature volante** (`$56`/`$81`/`$55`, dessin `$0ECB`, sprites bidirectionnels
  `$57ED`/`$59E5`) : danger que le tir peut chasser (`$7200`). *(Identité non
  confirmée.)*

### 8.8 Les deux systèmes de contrôle — démo vs partie (**trace à l'exécution, confirmé**)

Le jeu contient **deux** systèmes de contrôle/collision sur **deux jeux de tableaux** :

- **Partie réelle** — bloc gameplay (§8.5/§8.6) : entrée `$7A7C` → physique sol
  `$78BA`, tir `$7AAF`→`$70CD`, mort `$7237`, capture `$72B9`. Tableaux page zéro
  (oiseaux `$B2-$DF`, personnages `$E8-$F4`).
- **Démo/attract** — moteur relocalisé : entrée volante `$8833`, physique volante
  `$87A9`, résolveur de contact `$86C7` (= file `$2EC7`, runtime `+$5800`), sur les
  objets **page 7** `$0700-$07C0`.

**Trace POM1 (headless, instrumentation de comptage de PC)** — exécution du jeu en
attract (sans touche) puis en partie (touche injectée via `--paste`) :

| PC (runtime) | rôle | **attract** | **partie** |
|--------------|------|------------:|-----------:|
| `$86C7` | résolveur de contact volant (`$2EC7`) | **1621** | **0** |
| `$8833` | lecteur d'entrée volant | **1621** | **0** |
| `$87A9` | intégrateur physique volant | actif | **0** |
| `$804D` | boucle de trame (référence) | 8663 | 18800 |
| `$102F` | menu « toute touche démarre » | 0 | **1** |
| `$7A7C` | **entrée sol (vraie partie)** | — | **315** |

> **Conclusion (vérifiée).** Le chemin volant `$8833`/`$87A9`/`$86C7`/`$0700+` ne
> s'exécute **que dans la démo en attract** ; dès qu'une touche lance la partie
> (`$102F` tiré une fois), il **tombe à zéro** et c'est l'entrée-sol `$7A7C` (315
> hits) qui prend le relais. **Ce n'est donc pas un vestige mort : c'est le mode
> démo auto-joué**, distinct du tireur-au-sol de la vraie partie. *(Méthode :
> hook env-gaté `POM1_PCWATCH` dans `M6502::run`, désactivé/retiré ensuite ;
> chargement `bb_full.bin` = image `$0940` + shims `$9900`, machine 64K + GEN2.)*

> **Corroboration (labelling complet du moteur, §13).** La cartographie de tout le
> bloc `$8111-$86BC` confirme que le **tableau `$0700+` est la nuée d'oiseaux de la
> démo** : il a son propre mover (`$875E`), ses collisions oiseau↔danger (`$8547`),
> oiseau↔oiseau (`$8621`/`$866C`/`$8691`) et flyer↔danger (`$8572`) — un moteur de vol
> complet *parallèle* au jeu réel. La démo a aussi ses propres objets (personnages qui
> tombent `$0800`, étincelles `$64/$69`, cible `$6F/$70`, danger `$5D/$5E`). Question de
> §8.8 donc **résolue** : `$0700+` = démo, sans ambiguïté.

### 8.9 IA des oiseaux : machine à états + trajectoires scriptées — *décodé*

Une fois apparu (`$77DC` pose l'état `$D6,X=$F0` « émerge »), chaque oiseau est piloté
par une **machine à états** doublée d'un **séquenceur de trajectoires scriptées**.

**`$749F` Bird_AI_Dispatch** (oiseau d'indice `$27`) aiguille sur l'état `$D6,X` :

| `$D6,X` | comportement |
|---------|--------------|
| `<0` (`$F0`) | **émerge** (`$73F0`) — entre dans la zone de jeu puis passe en état actif |
| `0` (et `5`) | **vol/croisière** (`$74C0`) — gated par `$32` ; descente + **plongeon aléatoire** (tirage vs `$37`) |
| `5` | **atterrit** (`$7442`) · `$0A` **marche** au sol (`$7476`) |
| `1`-`4` | **manœuvres scriptées** (interpréteur ci-dessous) |

**L'interpréteur `$7514` (Y) + `$7545` (X)** : pour l'état courant, deux tables de
pointeurs donnent un **script** (Y : `$763C/$7644`, X : `$762C/$7634`, **indexées par
l'état**). Le compteur de trame `$CD,X` avance ; chaque octet est un **delta de
mouvement** appliqué à la position (`$BB,X`=Y, `$B2,X`=X) selon les signes de
direction `$A6,X` (vert.) / `$C4,X` (horiz.). Le marqueur **`$70` = fin** : on
**inverse le sens** (`$C4,X` EOR l'octet suivant), on **tire un nouvel état aléatoire**
(1-4, `$15FD`), reset `$CD,X`, re-dispatch. Y est plafonnée par `$7B2E,X`, X clampée
hors du **trou de lave central** (`$75CA`).

> **Difficulté encodée dans le moteur** : le delta-X passe par `$13F8`
> (`CPX #6 / ROL A`) — pour les **oiseaux des slots 6-8** (tier « rapide », +300 pts),
> le déplacement horizontal est **doublé**. Les 3 tiers ne diffèrent donc pas que par
> le score : le tier élevé **vole littéralement 2× plus vite** (même script, vitesse ×2).

### 8.10 Vérification dynamique — *trace POM1 (mode attract), confirmé*

Toute la logique décodée (§6.1 séquenceur, §8.5/8.6 mécanique, §8.9 IA) a été
**validée à l'exécution** : POM1 ré-instrumenté (hook env-gaté dans `M6502::run`
loggant `PC + X + dump mémoire` à chaque passage sur un handler), jeu lancé en
**attract** (le démo joue la vraie boucle, sans input). 225 événements observés.

**Séquenceur de vagues (§6.1) — confirmé au jalon près :**
- `$8951` (level-up) ne se déclenche **que** lorsque `$3F=$F2` ; `$10` passe
  `00→01→02→03` (un incrément par vague).
- `$8919` (spawn) aux seuils **exacts** `$E5,$D5,$C5,$B5,$A5,$95`, avec `X` = slot 0→5.
- `$892E` (vague pleine) à `$3F=$08` ; `$719C` (arm) toujours avec `$36=00` (vague
  nettoyée) ; `$8970` **gèle** `$3F` pendant l'intro (premières vagues bloquées ~`$E4`).

**Mécanique (§8.5/8.6) — confirmée :**
- **Capture** `$7308` (35×) : un oiseau prend un personnage au sol.
- **Dépôt au nid** `$6FB5` (16×) : `$36` (oiseaux actifs) **remonte** → un nouvel oiseau.
- **Score** : +100 `$71D1` (39×, centaines `$02`), +50 `$71FA` (39×, dizaines `$01`),
  +300 `$71E4` (11×), +100 ramassage `$709A` (6×) — **avec retenue décimale** observée
  (50+50 → `$01`:05→00, `$02`:0→1) exactement comme `Add_To_Score` (§8.2).

**IA (§8.9) — confirmée :** les états `$D6/$D7/$D8` des oiseaux passent par `00` (vol),
`01-04` (manœuvres scriptées) et `$0D` (abattu) au fil de la partie.

> Tout le modèle (timeline, spawns, niveaux, capture/dépôt, score, états d'IA) est
> donc **validé en marche, pas seulement lu**. *(Hook de trace retiré après mesure ;
> méthode reproductible : `POM1_TRACE=8951,8919,…` + `POM1_TRACE_ZP=3F,10,36,…`.)*

---

## 9. Assets et données

### 9.1 Organisation de `$4000-$67F1` — que des sprites

Toute la zone `$4000-$67F1` est un **pavage contigu de paires (table de pointeurs +
bloc de bitmaps)**. Chaque entité = une table de 14 octets (7 lo en `A`, 7 hi en
`A+7`) suivie de son bloc de **7 trames pré-décalées** entrelacées (pas =
largeur×hauteur). **Il n'y a aucune image plein écran stockée** : l'écran-titre
« BUZZARD BAIT » est **rendu procéduralement** depuis la font bloc `$1E0B`.

| Plage | Famille (lecteur) | Géométrie | Identité |
|-------|-------------------|-----------|----------|
| `$40CB / $418F` | `$16AB` | 2×13 | **petit personnage** qui marche (2 directions) |
| `$4253 / $4881 / $493E / $4539` | `$13FF/$1443/$1485/$14ED` | 6×9→4×4 | famille **graduée** (explosion ?) |
| `$508B / $52C9` | `$125A/$127D` | 3×10 | **nid** / élément animé |
| `$5474` | `$1742` | 4×13 | **joueur (tireur au sol)** *(corps)* |
| `$55EE` | `$170A` | 2×13 | petit personnage (variante) |
| `$56B2` | `$0F0D` | 2×9 | petit personnage (anim de chute/sauvetage) |
| `$5776` | `$1770` | 3×8 | 2ᵉ partie du joueur |
| `$57ED / $59E5` | `$0ED9/$0EE8` | **5×14** | **créature volante** (2 directions ; bitmaps en `$57FB`/`$59F3`) |
| `$5BDD / $5D57` | `$0E61/$0E70` | 4×13 | joueur/oiseau (autres orientations) *(à confirmer)* |
| `$5ED1 / $60C9` | `$0EBC/$0EA6` | **5×14** | **oiseau ennemi** (g/d) ; bitmaps en `$5EDF`/`$60D7` |
| `$62C1 / $6473` | `$0DF0/$0DFF` | 6×10 | **grand oiseau battant des ailes** |
| `$6625 / $666B / $6720 / $673C` | `$0DB0/$12AC/$0C6D/$0C5E` | 2×1/2×4 | tirs / étincelles / fragments |
| `$6758` | `$17A2` | 2×10 | **petit personnage debout** (le captif) |
| `$6810-$6BFF` | — | — | **table de masques** `FF FF 00 00` (bords / effacement du blitter) |
| `$6C00-$6C05` | — | — | table de hauteurs son `D0 B0 80 70 60 50` |

> *D'après la capture : le **joueur** est un petit **véhicule en dôme rayé
> rose/violet** (dessiné par `$173F`/`$5474`, possiblement en 2 parties avec
> `$5776`) ; les **petits personnages** sont des silhouettes blanches (`$40CB`,
> `$6758`) ; les **oiseaux** ont des ailes orange + corps bleu (`$5ED1/$60C9`, 5×14).
> Le bloc 4×13 que j'avais rendu en §3.9 est en fait une silhouette/personnage, pas
> le véhicule joueur.*

### 9.2 Galerie (rendus ASCII réels)

**Oiseau ennemi** — `$5EDF`, 5×14 (tête en haut à droite, aile, corps, serres) :
```
....... ....... .#####. ....... .......
......# .#....# ####### #...... #......
.####.# .#.#### ####### ###..#. .......
..###.# .#.#### ####### .#.#.#. #......
....#.# .#.#.## ###.#.# .#..... .......
......# .....#. ###.#.. ....... .......
....... ....... #.###.# ....... .......
....... ....... ..#.### .#..... .......
....... ....... ....### ##.#... .......
....... ....... ....#.# .#.#.#. .......
```
(Le bloc `$60D7` est le **même oiseau miroir** — confirme que les deux bancs 5×14
sont les deux orientations gauche/droite.)

**Grand sprite 4×13** — `$5BEB` (tête/cou, large bande rayée, corps, jambes) — le
joueur-tireur ou un grand oiseau selon l'orientation *(identité exacte à confirmer)* :
```
#...... ....... ......# .......
###.... ....... ....### .......
#.#.... ....... ....#.# .......
..##### ####### #####.. .......   <- aile déployée
......# .#.#.#. #...... .......
....... .#####. ....... .......
......# ####### #...... .......
......# .#.#.#. #...... .......
....... .#####. ....... .......
```

**Petit personnage (le captif)** — `$6766`, 2×10 (petite figure debout que les
oiseaux attrapent et remontent aux nids) :
```
..##.##
.###.##
...#.#.
...#.#.
...#.#.
..#####
...###.
....##.
```

**Petit personnage qui marche** — `$40D9`, 2×13 (tête, torse, deux jambes) :
```
.##....    tête
.##....
.######    épaules/bras
..##...
####...
.#####.
.###...
.####..    jambes
```

### 9.3 Tables de spécification `$7B07-$7BFF` (indexées par X)

- **Les 3 nids** : `$7B07`(3) X `00 3D 7A` (gauche/centre/droite) ; `$7B13`(3) **Y
  `2C 34 2C`** (44/52/44, haut de l'écran) — paire (X,Y) utilisée par `$70DD`
  (dessin/tir vs nid), `$6FE1` (spawn), `$6F9A` (proximité). `$7B0D`(3) `26 2E 26`
  = boîte de **dépôt du captif** (`$6D89`). `$7B0A/$7B10` = largeurs/fenêtres.
- `$7B16`(9) X-domicile oiseau ; `$7B1F`(9) X-bord/demi-tour ; `$7B25`(9) limites ;
  `$7B2E`(9) Y `C0×3 BC×3 BB×3` (proche du sol).
- `$7B37`(9) **carte de partenaire** `03 04 05 00 01 02 06 07 08` ; `$7B40`(9)
  transition d'état `01×6 07×3` ; `$7B49`(9) **jet d'agressivité** `60×3 30×3 C0×3`.
- `$7B52`(6) trame d'anim init ; `$7B58`(6) Y init `F5 FC F5 E8 EF E8`.
- `$7B5E+` : nouvelle table de masques `FF FF 00 00` (clipping de sprite au bord).

> Les groupes par 3 (`×3`) organisent les **9 oiseaux en 3 groupes** (scorés
> +100/+300/+50, plafond/agressivité distincts). À l'écran ils se ressemblent
> (mêmes ailes orange) ; les 3 groupes tiennent plus du **point d'apparition / barème**
> que de types visuellement différents.

### 9.4 Scripts de trajectoire des oiseaux `$764C-$77DB` — *décodés*

8 états (0-7), chacun pointant (via `$762C/$7634` pour X, `$763C/$7644` pour Y) vers
un script de **deltas de vitesse par trame** (octets signés ; `$70` = fin → flip
sens + nouvel état aléatoire). État 5 ≡ état 0 ; état 6 = aucun script. Mécanisme :
voir §8.9. Profils décodés (lecture image-par-image) :

| État | dx (horizontal) | dy (vertical) | Allure |
|------|-----------------|---------------|--------|
| 0/5 | `+1 +1 −1 −1 −1 −1 +1 +1` (8 tr.) | `0 +1 0 0 0 −1 0 0` → fin | **vacillement sur place** (8 tr.) → transition |
| 1 | `+2`×14, décélère → `−2` | monte `0→+3` puis redescend `→0` | **grand arc plongeant** (sweep + cloche) |
| 2 | `+2 +1 0 −1` (glissé) | arc doux `0→+2→0→−2` | **vol plané** descendant |
| 3 | `+2`×20 puis `1 0 1 0…` | `1 0 1 0…` (montée en marches) | **montée rapide en battant** |
| 4 | accel/décel puis battement, pic `+4` | arcs `0→+4`, battements | **plongeon/remontée agressif** |
| 7 | `+2…→−2` (23 tr.) | `0→+3→0` (23 tr.) | manœuvre d'émergence |

Chaque profil est une **trajectoire sculptée à la main** — c'est ce qui donne aux
oiseaux leur vol caractéristique (piqués, planés, cloches). ~`$190` octets de scripts
encodent tout le « pattern » des ennemis : **aucun `if` de trajectoire dans le code**.
Le code reprend à `$77DC` (init d'oiseau : état `$F0`, frame `$7B52,X`, Y `$7B58,X`).

### 9.5 Font logo `$1E0B`
Lettres-blocs 9 lignes × 7 px (1 octet) pour « BUZZARD BAIT », dessinées par
`$0F36` en **OU-blit** (§3.7). Les glyphes ne sont **pas** rangés dans l'ordre
ASCII : le tracé du titre les sélectionne par index. *(Encodage précis non
entièrement confirmé.)*

---

## 10. Carte de la page zéro et des tableaux d'objets

> Page zéro très chargée et **réutilisée** entre sous-systèmes (RAM précieuse).
> Quand deux usages se chevauchent, le contexte (routine appelante) tranche.

### Rendu / blitter
| zp | rôle |
|----|------|
| `$14/$15` | pointeur de ligne HGR courante (lo/hi), depuis `Hgr_LineLo/Hi[$1A]` |
| `$16` | hauteur du sprite/glyphe (compteur de lignes) |
| `$17` | largeur du sprite (octets) **ou** index de chaîne de glyphes |
| `$18` | colonne-octet de départ (0-39), sortie de `Get_SpriteParams` |
| `$19` | phase de décalage 0-6 (sortie de `Get_SpriteParams`) |
| `$1A` | ligne HGR courante (0-191) |
| `$25` | colonne de fin = `$18 + $17` |
| `$31` | limite de clip vertical (variante blitter `$1C01`) |
| `$2B/$2C` | **pointeur source de sprite** (auto-modifie `$1BEE/$1BEF`) — *réutilise les octets de boîte de collision* |
| `$0500/$0600` | tables `Shift[]` / `Col[]` du décodage pixel-X (256 o, bâties au boot) |

### Score / état de partie
| zp | rôle |
|----|------|
| `$00-$06` | **score** décimal 7 chiffres (`$00`=unités) |
| `$08-$0D` | **hi-score** décimal 6 chiffres |
| `$10/$11` | **niveau** (unités / dizaines) |
| `$13` | nombre d'ennemis actifs |
| `$24` | drapeau « nouveau hi-score » (dessine 2ᵉ copie) |
| `$36` | **ennemis restants dans la vague** (`DEC` à chaque kill ; 0 → vague nettoyée) |
| `$37` | **seuil de spawn/difficulté = 5** (uniquement comparé ; ⚠ *n'est pas* les vies) |
| `$3B` | timer d'explosion (60) / sous-état de boucle |
| `$3E` | **état joueur** : `0` = vivant, `$FF` = mort/en explosion |
| `$40` | **VIES** (init 3 ; `+1` bonus `$1B0D` ; `−1` mort `$6E42` ; game-over si `<0`) ; dessiné par `$1217` |
| `$54` | timer mort/respawn (→0 = réapparition) |
| `$57` | drapeau « vie bonus déjà attribuée » (anti-répétition) |
| `$63` | vies-écran / réserve restantes (≈3) |
| `$7E` | numéro de vague (0-3) |
| `$0994/$09A7` | recharge `$63` / activation son |

### Joueur
| zp | rôle |
|----|------|
| `$1B` | X joueur (curseur de travail) |
| `$1C` | consigne de direction (lecteur `$7A7C`) ou X-cible (paddle) |
| `$3A` | Y joueur (curseur de travail), borné `[$0E,$B7)` |
| `$39` | état/vitesse verticale : `$83`=posé/idle, `$F8`=impulsion de saut |
| `$5A/$5B` | **vitesse X / Y du joueur en MODE DÉMO** (physique volante `$87A9`, →`$1B`/`$3A`) ∈ {−3,0,+3}/{−6,0,+6}. En partie réelle le joueur utilise `$1B`+`$39` (§7) ; `$5A/$5B` n'y sont pas écrits (trace : `$87A9`=0). Un audit statique signale des écrivains `$8841`/`$87DB`/`$79DA`, mais leur appelant `$87A3` n'est pas atteint en jeu réel. |
| `$5C` | timer de sidération (hit-stun) / id de son |
| `$5D/$5E` | position « domicile » du joueur (`$D0`/`$6E`) |
| `$5F` | orientation joueur |
| `$1D,X` / `$78,X` | **tir du joueur** : Y / X (2 slots, projectiles montants) — §8.4 |
| `$7A` | index d'acteur joueur |
| `$76` | porte de saut ; `$77` position paddle ; `$3C` anti-rebond bouton |

### Oiseaux ennemis (9 slots, groupés 0-2 / 3-5 / 6-8)
| tableau | rôle |
|---------|------|
| `$B2,X` | X | `$BB,X` Y | `$C4,X` orientation | `$CD,X` trame | `$D6,X` **état** | `$DF,X` **personnage attrapé** (lié), `<0` = non chargé |
| `$DC,X` | état de bouche de spawn ; `$AF,X` état de flamme |
| `$0700/$0740/$0780/$07C0,X` | objets **page 7** (8 slots) du résolveur de contact `$2EC7` (cf. §8.8) |

### Petits personnages (6 slots) et dangers (8 slots)
| tableau | rôle |
|---------|------|
| `$E8,X / $FA,X / $F4,X / $EE,X` | personnage : X / Y / **état** (machine §8.6 : sol/porté/chute/nid) / orientation |
| `$96,X / $9E,X / $86,X / $8E,X` | danger tombant : X / Y / vel-X / vel-Y |
| `$56 / $81 / $55` | créature volante : X / Y / direction |

### Particules / explosions
| tableau | rôle |
|---------|------|
| `$0900-$090F / $0910 / $0920 / $0930` | 16 fragments : x / y / dx / dy (`$0BE1`) |
| `$0400 / $0440 / $0480 / $04C0,X` | 64 particules de mort joueur (`$6C85`) et objets de transition de niveau (`$92ED`) |
| `$3D` | index de boucle particules ; `$7B` durée de vie ; `$7C` direction de fragment |

### Divers
| zp | rôle |
|----|------|
| `$1F/$20/$21/$22` | état du **PRNG** (`$15FD`) |
| `$23 / $27 / $38 / $62` | index d'acteur courant (différents lecteurs/IA) |
| `$26` | index round-robin d'ennemi |
| `$28 / $34` | masques de bande du sol (`$1138`) / délai |
| `$32 / $33` | **phases d'animation globales** (0-2 / 0-9) ; portes de sous-systèmes (son, dangers, bouches) |
| `$35` | mode de contrôle (`$FF`=clavier, `5`=joystick) |
| `$41-$45` | compteurs de canaux d'effets sonores |
| `$48-$53` | état du traceur de grosses lettres (`$0F36`) |
| `$72/$73/$74/$75` | compteurs (intro de vague, file de trail) |
| `$82` | drapeau gel/pause/démo (attract-mode) |
| `$83,X` | état/clignotement des 3 objets bouche (`$6C1C`) |
| `$09A0-$09A6` | **table des touches** (I/J/K/L/Z/espace remappables) |

---

## 11. Son

Tout le son est **1-bit** par bascule du haut-parleur `$C030` (sur Apple-1, routé
vers la **sortie cassette ACI TAPE OUT**). Pas de puce sonore.

- **Jingle de boot** (`$0978-$09CE`) : 3 phrases de notes, chaque demi-période
  temporisée par `JSR mon_WAIT` (`$9900`, shim du portage qui remplace le `$FCA8`
  du Moniteur Apple II). `$C030` est basculé entre deux attentes.
- **`$6C06` SFX_Click** : le générateur de ton principal. 21 bascules `$C030`, la
  période lue dans la table `$6C00` (= `D0 B0 80 70 60 50`, période décroissante =
  pitch montant) indexée par `$5C` (id d'événement / hauteur). Appelé depuis la
  chaîne de trame (le moteur pose `$5C` avant d'appeler).
- **Crépitement d'explosion** : bascules `$C030` directes dans le déplaceur de
  particules `$6D25`, modulées par `$09A7` — le bruit de mort du joueur.
- **`$79C9` bascule SON ON/OFF** (touche `#$93`) : `LDA $6C0D / EOR #$10 / STA $6D7C
  / STA $6C0D`. `$6C0D` et `$6D7C` sont l'**octet de poids faible de l'opérande
  `LDA $C030`** (en `$6C0C` et `$6D7B`). `EOR #$10` change `$30→$20`, donc
  `LDA $C030` → `LDA $C020` : le haut-parleur est redirigé vers un soft-switch
  **inaudible** → mute. Re-presser dé-mute. Ce n'est **pas** une modulation de
  hauteur ; la hauteur de `SFX_Click` vient de `$5C` + la table `$6C00,X`.
- **Moteur relocalisé** : table de saut `$9000-$901E` → `$9037 Play_SFX` (périodes
  dans `$905C`, conditionné par `$32==2` et le compteur `$09B3`). Registres son en
  `$09B1-$09B4` ; canaux d'effets `$41-$45` ; table de tons `$0A9F` = `50 60 70 80
  B0 D0`.

---

## 12. Synthèse — pourquoi ce code est remarquable

*Buzzard Bait* est un cas d'école d'**ingénierie arcade 6502** :

1. **Le moteur de sprites (§3)** est la pièce maîtresse : il déporte **tout** le
   travail variable (adresse de ligne, décalage horizontal) vers des **tables
   statiques** (lignes HGR précalculées, sprites **pré-décalés en 7 phases**), pour
   réduire la boucle interne à 7 instructions de **load/EOR/store** sans aucun
   décalage. Le **XOR** rend l'animation non destructive (tracé = effacement, le
   décor survit) sans backbuffer. L'**auto-modification** (opérande source + pointeur)
   fait tenir ~31 dessinateurs de sprites sur **une seule** boucle.

2. **La gestion mémoire est rusée** : le moteur est **chargé dans la page graphique**
   (`$2800-$3FFF`) puis **relocalisé hors-champ** (`$8000`) pour que `$2000-$3FFF`
   devienne le framebuffer ; la moitié basse `$2000-$27FF` (titre/menu jetables)
   n'est volontairement pas sauvée — elle est sacrifiée à l'écran.

3. **La logique de jeu est table-dirigée** : l'IA des oiseaux rejoue des **scripts de
   trajectoire** (`$762C-$77DC`), les vagues suivent un **chronomètre de seuils**
   (`$88A6`), et les paramètres par groupe (plafond, agressivité, score) vivent dans
   des tables parallèles indexées par slot (`$7B16-$7B5D`). Ajouter un comportement
   = éditer une table, pas du code.

4. **Le portage Apple-1 (cf. `README.md`)** greffe tout cela sur une machine sans
   carte langage ni ROM Applesoft : soft-switches GEN2 remappés, son via ACI, latch
   clavier logiciel, table de clipping HGR redirigée `$D0→$98`, shims en RAM basse.

> **Pour le lecteur** : commencez par §3 (le moteur de sprites — l'« excellence et
> l'optimisation » recherchées), puis §6 (la boucle), puis §8 (la mécanique). Le
> harnais d'analyse (`bb.py dis/hex/spr`) et les routines de construction de tables
> émulées (`tables.py`) sont reproductibles ; chaque adresse citée est vérifiable
> sur le binaire `buzzard_bait.bin`.

### Limites & questions ouvertes
- **Mécanique validée par capture** (§1) : tireur au sol, 3 nids, oiseaux qui
  enlèvent les personnages. Le **chemin volant `$8833`/`$87A9`/`$2EC7`/`$0700+`** est
  désormais **confirmé = mode démo** par trace POM1 (§8.8 : 1621 hits en attract → 0
  en partie).
- **Identités de sprites** : sprite du **véhicule joueur** (lequel des blocs
  4×13 ?), famille graduée `$4253` (explosion ?), créature volante (`$57ED`) —
  partiellement confirmées par la capture, à finaliser au cas par cas.
- **Réutilisation de page zéro** : quelques adresses (`$56/$57`, `$2B/$2C`) servent à
  plusieurs sous-systèmes ; le tableau §10 donne l'usage dominant.

---

## 13. Audit contradictoire (provenance & fiabilité)

Ce document a été soumis à un **audit adversarial multi-agents** : 68 affirmations
falsifiables extraites, chacune confiée à un agent sceptique chargé de la *réfuter*
contre le binaire (double-vérification anti faux-positif). Résultat : **7 erreurs
réelles corrigées** (le moteur, la mécanique et la conclusion « mode démo »
tiennent ; les fautes étaient des décalages/seuils fins) :

| # | Sujet | Erreur | Correction |
|---|-------|--------|-----------|
| C08 | §3.2 table HGR poids-fort | « signature » faisait passer `$21` à la ligne 8 | chaque poids-fort couvre 8 lignes ; `$21` à la ligne 16 (bloc de 64 répété 3×) |
| C53 | §8.1 poussée gauche | seuil `−4` | **`−5`** (`CMP #$FC`) |
| C54 | §8.1 poussée haut | seuil `−3` | **`−4`** (`CMP #$FD`) — asymétrie réelle +5/−4 |
| C58 | §8.6/§9.3 Y des nids | `$7B0D` (`26 2E 26`) | **`$7B13` (`2C 34 2C`)** ; `$7B0D` = Y de dépôt du captif |
| C63 | §11 toggle son `$79C9` | « auto-mod de délai/pitch » | **mute/unmute** (`LDA $C030`→`$C020` inaudible) |
| C66 | §9.1 oiseau `$5ED1` | base donnée comme bitmap | `$5ED1` = table de pointeurs ; bitmap en `$5EDF` (miroir `$60D7`) |
| C68 | §9.1 créature volante | `4×8` | **`5×14`** (`$16=$0E`,`$17=$05`) ; cohérent avec §3.4 |

Un point d'audit (**C57**, « `$5A/$5B` utilisés en jeu réel ») a été **rejeté** :
c'est une sur-réfutation statique, contredite par la trace runtime (`$87A9`=0 en
partie) ; voir §10. Un doute de l'audit sur `Shift[$00]` a aussi été levé (deux
émulations indépendantes donnent `0`, §3.3 confirmé).

**Avancement (P6 + P7 faits).** Le **moteur relocalisé `$2800-$3FFF` est désormais
intégralement labellisé** (94 labels dans le listing) : boucle de jeu réelle, mode
démo complet (flyer auto-joué + ses objets `$0800`/`$64`/`$6F`/`$5D`), table de saut
son `$9000-$901E`, player SFX `$9037`, roamer/trail, transition de niveau `$92ED`,
écran **DEFINE KEYBOARD** `$936D`, et la table de données démo `$8A87-$8FFF`. La
logique de jeu (vagues §6.1, IA §8.9) a été **validée dynamiquement** (§8.10), et le
**diff contre l'original Apple II (§14)** prouve que 99% du binaire est le jeu Sirius
d'origine, byte-identique (le portage ne change que 0,99% — l'interface matérielle).

**Zones encore non exhaustives** (honnêteté) : géométries de quelques sprites
non-joueur (à confirmer au rendu), font logo `$1E0B` (ordre des glyphes), et le
cross-référencement *complet* des variables zp multi-usage. Le doc est **fiable sur la
structure, la mécanique et le moteur** ; restent quelques **identités d'assets** à
finaliser visuellement.

---

## 14. Diff contre l'original Apple II (P7) — *le portage est 100% chirurgical*

L'image Apple II **non portée** a été extraite de `POM2/disks_3.5/TheBestGames.2mg`
(`/BUZZARD.BAIT`, ProDOS BIN @ `$0940`, 29376 octets — la source exacte du portage)
via un lecteur ProDOS écrit pour l'occasion, puis comparée octet à octet au binaire
porté `buzzard_bait.bin`.

**Résultat : seulement 291 octets diffèrent (0,99 %), sur 41 régions.** Chaque région
tombe dans une famille de portage **déjà documentée** — zéro changement non annoté :

| Catégorie | sites | octets | exemple |
|-----------|------:|------:|---------|
| Clavier (latch logiciel `$C000`/`$C010` → shims `$9910`/`$9930`) | 20 | 60 | `AD 00 C0`→`20 10 99` |
| Menu (aide réécrite + sélecteur « toute touche ») | 9 | 104 | `$0C7C`, `$102F` |
| Hors-mémoire (table HGR clipping `$D0`→`$98`) | 1 | 72 | `$1D03` (72 entrées) |
| Boot (crédit de crack → crédit GEN2) | 1 | 35 | `$0981` |
| Son (`jsr $FCA8` → `jsr $9900`) | 4 | 10 | `A8 FC`→`00 99` |
| Graphisme (soft switches `$C0xx`→`$C2xx`) | 2 | 6 | `$0963`, `$1B3A` |
| Touches (flèches/A/S → I/J/K/L) | 4 | 4 | `$28F0` `88`→`CA` (←→J) |
| **Total** | **41** | **291** | |

### Deux conclusions

1. **Le portage Apple-1/GEN2 est 100 % chirurgical et 100 % documenté.** Le diff
   contre le vrai original confirme qu'il n'existe **aucune** modification hors des
   sites `[PORTAGE]` annotés (`README.md` + `.info`). La documentation du portage est
   donc **complète et exacte**, vérifiée contre la source.

2. **99,01 % du binaire (29085 octets) est identique à l'original Sirius 1983.** En
   particulier les **données de sprites** (`$4000-$6BFF`), le **moteur de sprites
   XOR** (§3), le **wave-scheduler** (§6.1), l'**IA scriptée** (§8.9), le **son** et
   toute la **logique de jeu** sont **byte-identiques** à l'original. Toute l'analyse
   de ce document décrit donc le **jeu Apple II d'origine**, pas une particularité du
   portage — le portage ne touche que l'interface matérielle (E/S graphisme, clavier,
   son) et la cosmétique (crédit, menu, schéma de touches).

> *Reproductible : extracteur ProDOS + script de diff dans le scratchpad d'analyse ;
> l'original vit dans le dépôt voisin `POM2` (non recopié ici — droits).*
