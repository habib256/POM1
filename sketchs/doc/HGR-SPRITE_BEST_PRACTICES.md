## HGR / GEN2 — Sprites animés : guide pratique & performance (cc65 / 6502)

Complément opérationnel pour animer des sprites **HGR couleur** sur la carte
GEN2. Là où [`doc/HGR_SPRITES_X1_X2.md`](../../doc/HGR_SPRITES_X1_X2.md) fige le
**format** (régimes ×1 artefact / ×2 couleur, contrat de parité, ABI), ce
document couvre le **runtime** : mouvement 2 px lisse, rendu incrémental, RAM,
et surtout le **modèle de coût** — pourquoi un sprite HGR est cher, combien on
peut en animer, et ce qu'il faudrait pour aller plus loin.

> **Référence de format / couleur** : [`doc/HGR_SPRITES_X1_X2.md`](../../doc/HGR_SPRITES_X1_X2.md).
> **Guide GEN2 asm / C** : [`Programming_GEN2.md`](Programming_GEN2.md) · [`Programming_GEN2C.md`](Programming_GEN2C.md).
> **ABI runtime** : `dev/lib/gen2c/gen2.h` (docstrings faisant autorité).
> **Projet de référence** : [`sketchs/gen2/demo_sprite_animals/`](../gen2/demo_sprite_animals/) (8 animaux Fauna, ×2, 2 px lisse) — portage HGR du TMS `demo_sprite_animals`.
> **Catalogue** : `dev/lib/gen2/sprites/` (SCROLL-O-SPRITES, mono ×1) · **éditeur** : `src/hgrsprite/`.

---

### 0. L'invariant fondateur — pas de sprites matériels

L'Apple 1 / GEN2 est un **framebuffer nu**. Un « sprite » n'existe pas en
silicium : c'est un rectangle d'octets HIRES qu'un programme **blitte lui-même**.
Chaque pixel de sprite coûte du CPU. C'est *toute* la différence avec le
TMS9918 (dont ce demo est le portage) : le TMS compose 32 sprites au balayage
pour **0 cycle CPU/pixel**. Toutes les techniques ci-dessous ne sont qu'une
tentative de simuler en logiciel ce que l'autre carte fait gratuitement — et
c'est *pour ça* que le HGR plafonne (§8).

Corollaire immédiat : **le blit domine le budget frame**. Toute optimisation
sérieuse doit réduire le **nombre d'écritures d'octets**, pas l'overhead de
boucle.

---

### 1. Choisir le régime : ×1 artefact vs ×2 couleur

| Régime | Dim (16×16) | o/sprite | Couleur | Quand |
|---|---|---|---|---|
| **×1 mono** | 3×16 | 48 | artefact NTSC (parité de colonne + bit 7 = groupe) | compact, couleur « gratuite » mais non choisie |
| **×2 couleur** | 6×32 | 192 | **choisie** (Violet/Vert/Bleu/Orange/Blanc), intrinsèque aux octets | quand tu veux une teinte fiable par sprite |

Le master canonique reste le **mono ×1 48 o** (`dev/lib/gen2/sprites/`). Le ×2
est un régime de *consommation*, dérivé du mono par `gen2_hgr_inflate_x2`
(miroir 6502 de `HgrSpriteBlit::magnifyColor2x` de l'éditeur). Détails couleur
byte-exacts dans [`doc/HGR_SPRITES_X1_X2.md`](../../doc/HGR_SPRITES_X1_X2.md) §2.

**Règle** : bake le ×2 **hors-ligne** (générateur, §6) plutôt que d'appeler
`inflate_x2` au runtime — voir le piège cc65 en §7.

---

### 2. Contrat de parité ×2 — LA règle de correction

La couleur ×2 est **parité-verrouillée** : chaque pixel doublé allume le dot
pair *ou* impair d'un color-clock. Si le sprite glisse d'un nombre **impair** de
pixels, la teinte bascule (Violet↔Vert, Bleu↔Orange).

- Un ×2 doit tomber sur une **colonne de pixel paire**. En byte-aligné
  (`gen2_hgr_blit7` snappe sur `x = 7·col`, parité alternée), cela impose
  **colonnes-octet paires : `x = 14·n`**.
- **Deux granularités de mouvement**, au choix du projet :
  - **14 px** — byte-aligné pur, rapide, mais saccadé ;
  - **2 px lisse** — via **pré-shifts** (§3), la voie du demo.

Le bit 7 (groupe de palette) est **per-octet** : un ×2 posé sur un fond d'une
autre palette casse la couleur du fond. Sur **fond noir** (cas usuel), aucun
souci.

---

### 3. Mouvement 2 px lisse — les 7 phases pré-décalées

En HGR, 1 octet = 7 pixels. Bouger de 2 px **ne décale pas une colonne d'octets**
— ça **repositionne les bits dans chaque octet**. On pré-cuit donc, par sprite,
**7 copies pré-décalées** couvrant une période de 14 px par pas de 2 px
(décalages 0, 2, 4, 6, 8, 10, 12) :

- un décalage **pair** préserve la parité NTSC → la teinte survit ;
- le **bit palette** est recalculé par octet décalé ;
- chaque phase élargit le sprite (16×16 ×2 = 32 px → jusqu'à ~44 px après +12).

**Adressage runtime** (sans division, cf. §5) : pour un pixel `x`,
```
phase = (x % 14) / 2          colonne-octet = 2·(x / 14)
```
puis blit la phase à cette colonne. Vertical : 1 px (le décalage horizontal ne
touche pas les lignes). Voir `blit_animal()` + les banks `x2ps_<nom>` dans
`GEN2Animals.c`, et le générateur `gen_x2.py` (§6).

> **Alternative pixel-précis générique** : `gen2_hgr_sprite` (moteur pré-shift
> « Buzzard-Bait ») fait la même chose pour du **mono** (couleur = artefact), 1 px
> précis. Pour du **×2 couleur** on bake nos propres phases (2 px, parité).

---

### 4. Rendu incrémental — le squelette d'une boucle rapide

Un `clear` plein écran (8 KB ≈ 40 000 cyc) + re-render du texte **à chaque frame**
tue le framerate. Le bon squelette :

1. **Décor statique dessiné UNE fois par page** (les 2 pages du double-buffer).
2. Chaque frame, sur la page cachée : **effacer** les anciennes empreintes puis
   **redessiner** aux nouvelles. Pas de clear global, pas de texte re-rendu.
3. **Double-buffer** : flip en V-blank (`gen2_wait_vbl` + `gen2_show_page`) →
   pas de tearing. Chaque page garde son **historique 1-frame** des positions
   (`ox/oy[i][page]`, comme `GEN2Bounces`).

```c
for (;;) {
    pidx = page - 1; gen2_set_draw_page(page);
    for (i = 0; i < N; ++i) erase_animal(i, ox[i][pidx], oy[i][pidx]);   /* 0-fill rect */
    for (i = 0; i < N; ++i) { draw_animal(i, cx[i], cy[i]);              /* SET blit    */
                              ox[i][pidx] = cx[i]; oy[i][pidx] = cy[i]; }
    gen2_wait_vbl(); gen2_show_page(); page ^= 3; /* 1<->2 */
    set_pos();  /* positions du frame suivant */
}
```

**Effacer avec un FILL de rectangle, pas un blit de forme.** Sur fond noir,
effacer = écrire des 0 sur le rectangle de l'empreinte : `gen2_hgr_fill_rect(y,
rows, col, ncols, 0)` a une boucle interne `STA` serrée (pas de lecture source,
pas de `AND`/`EOR` par octet) → **~2× moins cher** qu'un `GEN2_CLEAR`-blit de la
forme, et sans données de bank/phase. Le rectangle d'erase doit couvrir
**exactement** le rectangle qu'a dessiné le blit (mêmes `col/ncols/y/rows`) —
sinon traînée. (Réserver `GEN2_CLEAR`-blit aux fonds non-uniformes, rares.)

**Effacer TOUT puis dessiner TOUT** (deux passes séparées) — pas un
erase+draw par sprite dans une seule boucle. Sinon, quand deux sprites se
recouvrent, l'effacement de l'un peut trouer le voisin déjà dessiné. Erase-all
→ draw-all garantit un fond noir propre sous chaque sprite avant redessin.

> **Preuve de correction** (recouvrements) : rendre la version incrémentale et
> une version « redraw all » à **frame égale** (plafonner la boucle à N frames
> puis spin, dumper après stabilisation) → **même hash de framebuffer**. Comparer
> à cycle égal est trompeur : l'incrémental tourne plus vite, donc les sprites
> sont plus avancés au même cycle.

---

### 5. Zéro `mul`/`div` par frame — tout en LUT

cc65 n'a **pas** de multiplication/division matérielle : chaque `*`/`/` non
trivial est une routine logicielle (des dizaines à centaines de cycles). Dans une
boucle sprite, on les **élimine** :

- **Sinus pré-scalé** : au lieu de `sin[phase] * ampli / 12` par frame, bake au
  boot une table `sX[i][32]` déjà multipliée par l'amplitude de chaque sprite ;
  avance la phase par **accumulateur** (`ph += vitesse`, & 31) plutôt que
  `t * vitesse`.
- **Adressage sprite** : `x → (colonne-octet, phase, offset dans la bank)` via
  des tables `baseT[]`, `phTb[]`, `bankOff[][]` remplies **une fois** au boot ;
  `blit_animal` n'y fait que des lookups. (Les `/14`, `%14`, `/7` vivent dans
  `init_luts`, exécuté une seule fois.)

Résultat mesuré sur le demo : `set_pos` / `blit_animal` / `main` = **0 routine
mul/div** par frame (toutes déplacées au boot).

---

### 6. Trim aux boîtes englobantes — RAM *et* vitesse

`gen2_hgr_blit7` **paie pour les octets à zéro** (un `SET`/`CLEAR` de 0 coûte
quand même `LDA/ORA/STA`). Or un sprite baké 7×32 est surtout du vide. Le
générateur `gen_x2.py` bake donc chaque phase à sa **vraie boîte** :

- lignes utiles seulement (`yoff`, `nrows`) — indépendant de la phase ;
- **largeur par phase** (`wp[phase]`, 4 ou 5 octets) — le contenu glisse à
  droite avec le décalage, chaque phase à sa largeur exacte.

Effet sur le demo 8 animaux : **7 100 o** de banks (vs **12 544 o** en 7×32
plein). Ce trim est **ce qui fait tenir 8 sprites** quand la DevBench force-linke
tout le runtime (§7), *et* accélère le blit (~36 % d'octets en moins).

Régénérer / ajouter un sprite :
```
python3 sketchs/gen2/demo_sprite_animals/gen_x2.py   # imprime banks + métadonnées
# coller le bloc dans GEN2Animals.c (le .c reste mono-fichier, cf. §7)
```

---

### 7. Pièges cc65 / RAM (déjà diagnostiqués)

1. **`gen2_hgr_inflate_x2` est miscompilé par cc65 `-Oirs`** : l'optimiseur
   supprime le corps qui pose les pixels → sprite ×2 tout noir **on-target**
   (l'hôte compile juste, donc `hgr_inflate_x2_smoke` — un pin hôte — ne le voyait
   pas). La cible utilise donc une **implémentation asm** :
   `dev/lib/gen2c/gen2_hgr_x2.s` (le `.c` reste en **référence hôte** derrière
   `#if !defined(__CC65__)`). Pin on-target : `dev/lib/test/micro/t13_c_inflate_x2.c`
   (`ctest lib_micro_tests`). ⇒ **Préfère de toute façon baker le ×2 hors-ligne**
   (§6) : zéro coût runtime.
2. **La DevBench force-linke TOUT le runtime gen2c** (dont le pool BSS 1,5 KB du
   sprite-engine), contrairement au `make` qui prend un link-set réduit. Les
   grosses banks débordent alors la BSS `$6000-$BEFF`. Remèdes :
   - parquer les **tableaux non-initialisés** (LUTs, historique) en **LOWBSS**
     (`$0C00-$1FFF`, RAM libre sous les framebuffers) via
     `#pragma bss-name(push,"LOWBSS")` — les remplir **avant** toute lecture
     (crt0 ne zéroe pas LOWBSS) ;
   - le trim par-phase (§6) réduit la RODATA des banks.
3. **La DevBench compile un SEUL fichier** : un sketch destiné à la DevBench doit
   tenir dans **un `.c` auto-contenu** (banks inlinées, pas de `.inc` frère).
4. **Sprite x2 dans le link-set DevBench** : `gen2_hgr_x2.s` doit être dans les
   `asmSources` de `dev/bench/gen2c.json` **et** du fallback `src/Pom1BenchHost.cpp`
   (sinon `Unresolved external '_gen2_hgr_inflate_x2'`).

---

### 8. Modèle de coût & framerate — combien de sprites ?

**Le cap de framerate = 60 fps**, imposé par le V-blank de la carte (Apple II
NTSC : 262 lignes × 65 cycles = **17 030 cycles/frame**). `gen2_wait_vbl` ne
plafonne que si le compute **tient** dans ce budget.

Coût d'un blit trimé (~5 o × ~27 lignes) ≈ **~3 700 cyc**, dont l'essentiel dans
le RMW par octet (~24 cyc/octet : `LDA src,Y / ORA dst,Y / STA dst,Y` + boucle).
Par animal : **2 blits** (erase + draw). D'où :

```
coût/frame ≈ overhead_fixe (~3 000) + N × 2 × 3 700
```

| Animaux | blits | ~cyc/frame | fps |
|---|---|---|---|
| **2** | 4 | ~17 000 | **~60 (cap)** |
| 4 | 8 | ~33 000 | ~15–17 |
| 8 | 16 | ~63 000 | ~8–10 |

⇒ **Seuls ~2 sprites** tiennent le **60 fps plein**. Au-delà, la boucle est
compute-bound (`wait_vbl` retourne aussitôt) et le framerate chute en **~1/N**.
« Fluide » à 4 sprites veut dire *mouvement propre à ~15 fps*, pas 60 fps.

**Pourquoi 2 px est incompressible** : à chaque pas de 2 px, **tous** les octets
de l'empreinte changent (via la phase pré-décalée) — il n'y a **aucun octet
inchangé** à sauter. Le coût `2 × Σ(octets) × 24` est donc plancher dans ce
modèle. Pour le battre, il faut changer de renderer (§9).

> **Piège de mesure** : cc65 travaille en instructions, le cap en cycles. Le
> `cycles` du snapshot CPU (`M6502::serialize`) n'est **pas** un cumul total.
> Pour mesurer un vrai `cyc/frame`, calibre sur la sonde V-blank (`for(;;){
> wait_vbl; ++cnt; }` → `cnt` frames = `cnt × 17030` cycles) ou dessine une jauge
> de progression lue depuis le framebuffer dumpé.

---

### 9. Aller plus loin — taxonomie des renderers

Pour dépasser le plafond (p. ex. 8 sprites @ 60 fps), il faut lâcher **une** des
trois contraintes : le 2 px lisse, la résolution, ou le recouvrement.

| Renderer | Mécanisme | Gain | Prix |
|---|---|---|---|
| **Aligné 14 px + delta colonne** | à 14 px, seuls les bords entrant/sortant changent ; on n'efface/dessine qu'1-2 colonnes | ×5-10 | **mouvement saccadé** (garder Y lisse atténue) |
| Passe fusionnée `STA` | 1 seule passe sur old∪new, `STA` opaque = efface+dessine | ~40 % | **casse le recouvrement** (les 0 poinçonnent le voisin) ; OK si sprites disjoints |
| Sprites compilés (SMC) | code déroulé `LDA/STA`, adresses patchées | ~15 % | entrelacement HGR → code auto-modifiant, complexe |
| Save-under (`gen2_sprengine`) | sauver/restaurer le fond sous le sprite | **~0 ici** | sur fond noir, dégénère en `CLEAR`+`SET` ; utile seulement avec un décor statique complexe |
| Dirty-tiles | ne recomposer que les tuiles touchées | marginal | seulement si sprites **épars** |
| Beam-race mono-buffer | dessiner devant le faisceau | ~0 travail | timing cycle-exact, très fragile |
| Flipbook (frames pré-calculées) | copier une page pré-cuite/frame | petit | RAM prohibitive (8 KB × période) |
| **Lo-res GEN2 (40×48)** | 16× moins de « pixels » | ×16 | **perte de résolution** (gros blocs) |

**Les seuls qui atteignent réellement 8 @ 60 fps** : **14 px + delta colonne**
(on lâche le 2 px) ou **lo-res** (on lâche la résolution). Les autres empilés
plafonnent vers ~2×, insuffisant pour le ×3,5 requis. La vraie réponse reste :
**l'Apple II n'a pas de sprites matériels** — c'est le mur.

---

### 10. Modes de blit — sémantique & recouvrement

`gen2_hgr_blit7(x, y, wbytes, h, src, mode)` — `src` pré-packé 7 px/octet.

| Mode | Op par octet | Usage | Recouvrement |
|---|---|---|---|
| `GEN2_SET` | `dst |= src` (OR) | **dessiner** sur fond noir | additif → les deux sprites visibles ✔ |
| `GEN2_CLEAR` | `dst &= ~src` | **effacer** une forme connue | ✔ |
| `GEN2_XOR` | `dst ^= src` | mono auto-effaçant (draw==erase) | **casse** : double-toggle → trou noir dans la zone commune ✘ |

Le demo Fauna utilise **`CLEAR` (erase) + `SET` (draw)** — pas XOR — précisément
parce que les animaux se recouvrent. `SET`/`CLEAR` sont opaques et corrects sous
recouvrement (avec erase-all → draw-all, §4). XOR ne serait pas moins de blits
et trouerait les recouvrements.

> **Optimisation runtime partagée** : `_gen2_blit7_run` (`dev/lib/gen2c/gen2_blit.s`)
> **hisse le test de mode hors de la boucle interne** (3 boucles spécialisées
> SET/CLEAR/XOR au lieu de re-tester `b_mode` à chaque octet) → **+30 %** mesuré,
> pour *tous* les appelants de `gen2_hgr_blit7`.

---

### 11. Checklist avant merge (sprites HGR animés)

- [ ] ×2 baké **hors-ligne** (générateur), pas d'`inflate_x2` au runtime (§7.1).
- [ ] Mouvement ×2 : soit **14 px** byte-aligné, soit **2 px** via phases
      pré-décalées — jamais un `x` impair (parité, §2).
- [ ] Rendu **incrémental** : décor 1×/page, erase-all puis draw-all, double-buffer
      + `wait_vbl` (§4).
- [ ] **0 `mul`/`div` par frame** : sinus pré-scalé + LUT d'adressage (§5) —
      vérifier l'asm généré (`grep 'jsr.*\(mul\|div\)'`).
- [ ] Banks **trimées** par phase (§6) ; sketch DevBench **mono-fichier**.
- [ ] Tableaux non-init en **LOWBSS** si build DevBench (§7.2).
- [ ] Correction sous recouvrement prouvée par **hash à frame égale** (§4).
- [ ] Budget framerate estimé (§8) : au-delà de ~2 sprites, c'est compute-bound —
      documenter le fps attendu.

---

### 12. Projet & fichiers de référence

| Rôle | Fichier |
|---|---|
| Demo animé (8 ×2, 2 px, incrémental, trim) | `sketchs/gen2/demo_sprite_animals/GEN2Animals.c` |
| Générateur banks ×2 + trim | `sketchs/gen2/demo_sprite_animals/gen_x2.py` |
| Runtime blit / inflate / API | `dev/lib/gen2c/gen2.h`, `gen2_sprites.c`, `gen2_blit.s` |
| Inflate ×2 asm (fix `-Oirs`) | `dev/lib/gen2c/gen2_hgr_x2.s` (+ `.c` réf hôte) |
| Pré-shift générique (mono) | `dev/lib/gen2c/gen2_preshift.c` (`gen2_hgr_sprite`) |
| Catalogue mono ×1 | `dev/lib/gen2/sprites/` + `tools/build_hgr_sprites.py` |
| Éditeur de sprites | `src/hgrsprite/` (`HgrSpriteBlit::magnifyColor2x`) |
| Spec format ×1/×2 | [`doc/HGR_SPRITES_X1_X2.md`](../../doc/HGR_SPRITES_X1_X2.md) |
| Pins | `hgr_inflate_x2_smoke` (hôte) · `t13_c_inflate_x2.c` / `lib_micro_tests` (on-target) |
