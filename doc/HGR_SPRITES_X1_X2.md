# Sprites HGR — régimes ×1 (artefact) et ×2 (couleur choisie)

Spec de conception pour les sprites GEN2 HGR. Statut : **design figé, avant
implémentation**. Réfs code : `src/hgrsprite/` (éditeur + `HgrSpriteBlit`),
`dev/lib/gen2c/` (runtime), `dev/lib/gen2/sprites/` (catalogue ×1),
`tools/build_hgr_sprites.py` (générateur). Prérequis couleur HGR :
[`sketchs/doc/Programming_GEN2.md`](../sketchs/doc/Programming_GEN2.md).

> **Ce document = le FORMAT.** Pour la **pratique de l'animation** (mouvement 2 px
> lisse via pré-shifts, rendu incrémental, trim RAM, modèle de coût & framerate,
> pièges cc65/DevBench, taxonomie des renderers), voir le guide compagnon
> [`sketchs/doc/HGR-SPRITE_BEST_PRACTICES.md`](../sketchs/doc/HGR-SPRITE_BEST_PRACTICES.md).

## 0. Invariant fondateur

L'Apple 1 / GEN2 n'a **aucun sprite matériel** : un « sprite » est un rectangle
d'octets HIRES qu'un programme blitte lui-même. Deux régimes, **une seule source
stockée** :

- **×1 — la couleur est un artefact NTSC** (le truc de Woz). Elle *émerge* de la
  parité de colonne. Le **seul levier couleur** du ×1 est le **bit 7 = groupe de
  palette** (par octet), qui doit être **sélectionnable** : groupe 0 →
  violet/vert, groupe 1 → bleu/orange (+ décalage d'un demi-dot, comportement HGR
  authentique). Ce n'est **pas** une chroma choisie par pixel — c'est une famille
  d'artefact choisie par octet. Compact : **48 o** (16 lignes × 3 octets). C'est
  la forme d'archive économe, **toujours stockable et blittable telle quelle**.
- **×2 — la couleur est choisie.** Chaque pixel source est doublé dans un
  color-clock aligné, ce qui donne une teinte propre et fiable. Coût : **×4** la
  place (6 o × 32 lignes = 192 o pour un 16×16).

> **Règle d'or.** La *chroma par pixel* d'un sprite HGR n'existe qu'en ×2. En ×1,
> la couleur est de l'artefact — mais on **sélectionne le groupe de palette**
> (bit 7 par octet : violet/vert ou bleu/orange). Le mono ×1 reste le master
> canonique ; le ×2 est un régime de *consommation*, dérivé à la demande.

Les 6 couleurs de l'éditeur — **Noir, Violet, Vert, Bleu, Orange, Blanc** — sont
toutes des couleurs de plein droit (Noir et Blanc compris, cf. §2.2).

## 1. Formats de stockage

### 1.1 ×1 mono — le catalogue (quasi inchangé)

Fichiers `dev/lib/gen2/sprites/sprites_<cat>_hgr.asm` + `.inc`, générés depuis les
sources TMS par `tools/build_hgr_sprites.py`.

- **48 o** = 16 lignes × 3 octets, row-major, **bit 0 = pixel gauche**. 16 px →
  cols 0..6 (octet 0), 7..13 (octet 1), 14..15 (bits 0..1 de l'octet 2).
- **Bit 7 = groupe de palette, sélectionnable (par octet)** — n'est **plus**
  forcé à 0. Il porte le choix violet/vert (0) vs bleu/orange (1) et fait partie
  intégrante des données du sprite. Taille inchangée (48 o) : bit 7 devient
  signifiant, il ne s'ajoute pas.
- Ce layout 7 px/octet + bit 7 est **directement consommable par
  `gen2_hgr_blit7`** — pas de conversion runtime. Toute routine de blit ×1 doit
  **préserver le bit 7 de la source** (`blit7` l'`OR` déjà tel quel ; la voie
  précise préserve la palette par octet).
- `.inc` : `<CAT>_HGR_COUNT`, `<CAT>_HGR_BYTES_PER_SPRITE = 48`. **Ajout** :
  `<CAT>_HGR_W = 3`, `<CAT>_HGR_H = 16` (pour délier le loader du hardcode 3×16).
- **Aucune couleur bakée.** Seul vrai changement : la bannière annonce désormais
  « régime ×1, couleur = artefact NTSC, mono ».

### 1.2 ×2 coloré — éphémère, par projet (jamais commité globalement)

**Décision figée : pas de `sprites_*_hgr_x2.asm` versionné dans le repo.** La
couleur étant un choix, le ×2 pré-cuit est un artefact **par projet**, produit
soit par l'export de l'éditeur (§4), soit par `inflate_x2` au runtime (§3). Quand
un projet choisit de matérialiser un ×2 en `.asm` :

- Labels `<name>_x2_pat:`, base optionnelle `<name>_x2_data:`.
- Géométrie doublée : `2*wBytes × 2*hRows` → **192 o** pour un 16×16.
- Couleur **intrinsèque aux octets** (colonne paire/impaire + bit 7), pas de
  paramètre séparé. En-tête : `; <name> [x2 <couleur>]` + rappel du contrat de
  parité (§2.3) et du mode de blit (§2.2).

| Régime | Dim | o/sprite | Couleur | Produit par |
|---|---|---|---|---|
| ×1 mono | 3×16 | **48** | artefact (parité) | `build_hgr_sprites.py` (repo) |
| ×2 coloré | 6×32 | **192** | choisie, intrinsèque | éditeur / `inflate_x2` (par projet) |

## 2. Modèle couleur ×2 (autorité : `HgrSpriteBlit::magnifyColor2x`)

### 2.1 Le doublage

Chaque pixel source `(sx, sy)` devient un bloc 2×2 : colonnes destination
`2·sx` (**paire = gauche**) et `2·sx+1` (**impaire = droite**), lignes `2·sy` et
`2·sy+1`. Comme le bloc couvre un color-clock NTSC complet, la couleur se
reproduit de façon fiable, contrairement à un pixel ×1 isolé.

### 2.2 Les 6 couleurs → (colonnes allumées, bit 7, mode de blit)

| Couleur | Colonnes allumées | Bit 7 (palette) | Mode de blit conseillé |
|---|---|---|---|
| **Violet** | gauche (paire) | 0 | `SET` |
| **Vert** | droite (impaire) | 0 | `SET` |
| **Bleu** | gauche (paire) | **1** | `SET` |
| **Orange** | droite (impaire) | **1** | `SET` |
| **Blanc** | **les deux** | 0 | `SET` (blanc plein) |
| **Noir** | **les deux** | 0 | `CLEAR` (perce la forme en noir) |

> **Nuance Noir/Blanc.** Ils gonflent vers le **même** masque deux-colonnes ; ce
> qui les distingue est le **mode** : Blanc en `SET` (dessine du blanc plein),
> Noir en `CLEAR` (efface la forme → silhouette noire dans un décor). Deux
> sprites `[x2 white]` et `[x2 black]` ont donc des octets identiques et ne
> diffèrent que par le mode que le programme applique. *(À l'implémentation :
> `magnifyColor2x` ignore aujourd'hui `Black` — il faudra le traiter comme
> « les deux colonnes » pour émettre le masque, le mode `CLEAR` restant au
> choix de l'appelant.)*

### 2.3 Contrat de parité — LA règle de correction

`magnifyColor2x` suppose que le **bord gauche du sprite tombe sur une colonne
paire**. Conséquences à respecter côté programme :

- Un ×2 doit être blitté à **x pair** pour garder sa chroma. À x impair,
  Violet↔Vert et Bleu↔Orange s'inversent.
- `gen2_hgr_blit7` snappe sur les colonnes-octet `x = 7·col`, dont la parité
  **alterne** (0 pair, 7 impair, 14 pair…). En byte-aligné, le ×2 n'est donc
  correct qu'aux **colonnes-octet paires (x = 14·n)**.
- **Granularité de mouvement ×2** (les deux chemins sont fournis et documentés) :
  - **pas de 2 px** via le pré-shift (`gen2_hgr_sprite`) — chroma stable, précis ;
  - **pas de 14 px** via le byte-aligné (`gen2_hgr_blit7`) — plus simple/rapide.

  Le projet choisit sa routine selon son compromis vitesse/fluidité.

## 3. ABI runtime (`dev/lib/gen2c/`)

### 3.1 ×1 — rien de neuf

Le mono 48 o est déjà des octets HGR 7 px/octet → blit tel quel via
`gen2_hgr_blit7` (rapide, byte-aligné), `gen2_hgr_sprite` (pré-shift, précis au
pixel) ou le moteur masqué `gen2_sprengine`. Modes `GEN2_SET/CLEAR/XOR`
inchangés. Couleur = artefact, gratuite.

### 3.2 ×2 — les DEUX voies de fabrication (décision figée)

Un ×2 coloré est *aussi* de simples octets HGR : une fois fabriqué, il se blitte
avec les **mêmes** routines que le ×1, sous le contrat §2.3. Le seul ajout est la
**fabrication** du ×2 depuis le mono, livrée sous deux formes :

```c
/* (A) inflate-once — PATTERN PAR DÉFAUT. À l'init : mono ×1 (7px/o) -> ×2
   coloré en RAM. Miroir 6502 de magnifyColor2x. out = 2*wbytes × 2*h octets.
   Économie ROM du mono (48 o) + vitesse du byte-aligné ensuite. Le ×4 est
   payé une seule fois, en RAM, pour les seuls sprites utilisés en couleur. */
void gen2_hgr_inflate_x2(const unsigned char *mono, unsigned char wbytes,
                         unsigned char h, unsigned char color,
                         unsigned char *out);

/* (B) blit au vol — sans buffer. Double + colore à CHAQUE frame. Économe en
   RAM, coûteux en cycles. Fourni pour les cas où la RAM manque. */
void gen2_hgr_blit_x2(unsigned x, unsigned char y, const unsigned char *mono,
                      unsigned char wbytes, unsigned char h,
                      unsigned char color, unsigned char mode);
```

**Pattern recommandé** : stocker 48 o en ROM → `inflate_x2` une fois au boot dans
un buffer RAM (192 o) → blitter ce buffer chaque frame (`blit7`/`sprite`). Voie
(B) réservée aux budgets RAM serrés.

**Enum couleur** (les 6 de `hgrpaint::HgrColor`, mapping du §2.2) :
`GEN2_HGR_BLACK, _VIOLET, _GREEN, _BLUE, _ORANGE, _WHITE`.

**Décor non-noir** : la palette HGR étant par octet, un ×2 coloré posé sur un
fond d'une autre palette casse la couleur du fond → passer par le moteur masqué
(`gen2_sprengine`) avec données+masque ×2. Documenté comme limite, hors scope
immédiat.

## 4. Ce que l'éditeur exporte (`src/hgrsprite/`)

L'éditeur a déjà `buildSpriteBytes` (mono ×1 / doublé ×2 via `magnifyColor2x`).
L'export gagne un **choix de régime** :

- **×1 (défaut)** : `extract` mono → `<name>_pat:` 48 o, en-tête ×1. L'éditeur
  expose un **sélecteur de groupe de palette (bit 7)** : groupe 0 (violet/vert) /
  groupe 1 (bleu/orange), appliqué à l'export en `OR $80` sur les octets
  concernés. Défaut : toute la forme (le plus simple) ; extension possible :
  par colonne-octet (la palette est per-octet sur HGR). Le canvas ×1 reflète le
  groupe choisi via le pipeline NTSC de l'host.
- **×2** : `buildSpriteBytes` en `mag2_` + `color_` → `<name>_x2_pat:` 192 o,
  en-tête `[x2 <couleur>]` + rappels parité/mode. Nom sanitizé + suffixe `_x2`.
- `SaveSprite` (binaire brut) suit le même choix. **PNG** reste WYSIWYG.

C'est ce chemin qui comble le trou actuel : aujourd'hui le ×2 n'est visible qu'en
PNG et **jamais exporté en octets** exploitables par un programme.

## 5. Loader dev-library, round-trip, tests

- `Pom1HgrPaintHost::devSprites()` : **délier du hardcode** `minBytes=48 / 3×16`.
  Lire la géométrie via `<CAT>_HGR_W/H` (ou l'en-tête sprite) ; charger les deux
  flux et taguer chaque entrée avec régime + couleur. Le browser affiche un badge
  « ×1 » / « ×2 <couleur> ».
- `sprite_asm_export_smoke` : ajouter un cas ×2 (export 192 o → parse → mêmes
  octets).
- Nouveau test proposé `hgr_inflate_x2_smoke` : `gen2_hgr_inflate_x2` (asm) vs
  `magnifyColor2x` (C++) byte-identiques sur une grille — épingle le miroir
  6502 ↔ éditeur (même discipline que `hgr_convert_smoke`).

## 6. `tools/build_hgr_sprites.py` — ce qui change (presque rien)

Reste **mono ×1 pur** (c'est correct comme régime ×1) : bannière honnête, et
émission de `<CAT>_HGR_W/H` dans le `.inc`. **N'émet jamais de ×2** — la source
TMS n'a pas d'info couleur, et le ×2 est éphémère/par-projet. **Bit 7 : groupe 0
par défaut** (la source TMS ne porte pas de palette) ; le format l'autorise, mais
le choix du groupe se fait dans l'éditeur (§4), pas dans le générateur.

## 7. Décisions figées (récapitulatif)

1. **Fabrication ×2 : `inflate_x2` (défaut) ET `blit_x2` (au vol) — les deux.**
2. **Placement ×2 : 2 px (pré-shift) ET 14 px (byte-aligné) — les deux
   documentés, choix laissé au projet.**
3. **Catalogues ×2 : éphémères / par projet — rien de commité globalement.**
4. **Noir et Blanc sont des couleurs à part entière** (Blanc = `SET` plein,
   Noir = `CLEAR`).
5. **Bit 7 sélectionnable en ×1** = groupe de palette (violet/vert ↔ bleu/orange),
   par octet dans les données, choisi dans l'éditeur ; les blits ×1 préservent le
   bit 7. Défaut générateur : groupe 0.

## 8. Ordre d'implémentation — statut

1. **B — export ×2 de l'éditeur** — ✅ fait. `ExportAsm`/`SaveSprite` conscients
   du régime, label `_x2` + note couleur/parité/mode, indicateur UI, test ×2.
   Plus : sélecteur **bit 7 ×1** (§4/décision 5) et **aperçu de parité honnête**
   (l'aperçu suit la parité de Byte X ; placement impair permis + flaggé).
2. **A — runtime** — ✅ fait. `gen2_hgr_inflate_x2` (`dev/lib/gen2c/gen2_hgr_x2.c`,
   miroir exact de `magnifyColor2x`) + `gen2_hgr_blit_x2` (au-vol,
   `gen2_hgr_blit_x2.c`) + enum `GEN2_X2_*` (ordre `HgrColor`) dans `gen2.h` +
   split `gen2c.mk` (`GEN2C_X2_SRCS` / `GEN2C_X2BLIT_SRCS`). Pin :
   `hgr_inflate_x2_smoke` croise le C runtime (compilé hôte) contre l'éditeur C++
   sur 240 cas ; les deux `.c` compilent aussi sous cc65 `-t none -O`. NB : le test
   est un cross-check hôte (pas d'exécution 6502) — même discipline que
   `hgr_convert_smoke`.
3. **Loader dev-library géométrie-agnostique + badges (§5)** — ✅ fait.
   `Pom1HgrPaintHost::parseSpriteFile` lit la géométrie par fichier (`<H> rows x
   <W> bytes`), appelle `parseSpritesAsm` avec le bon `bytes/sprite`, et tague
   `DevSprite` (régime + couleur via la note `x2 colour = …` / suffixe `_x2`). Le
   browser affiche une pastille de teinte (×2) + le tag au survol.
4. **Bannières honnêtes + `<CAT>_HGR_W/H` (§6)** — ✅ fait. `build_hgr_sprites.py`
   annonce le régime « x1 mono, couleur = artefact NTSC, bit 7 = groupe (0 par
   défaut) » et émet `<CAT>_HGR_W = 3` / `<CAT>_HGR_H = 16` dans le `.inc`. Les 15
   catégories régénérées (`--check` clean).
