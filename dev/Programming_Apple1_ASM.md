# Programmation Apple 1 en assembleur 6502

**Guide pratique — modes texte, HGR (GEN2 Color Graphics Card), TMS9918 (P-LAB Graphic Card)**  
VERHILLE Arnaud — 2026

Ce document récapitule tout ce qu'il faut savoir pour programmer l'Apple 1 émulé par POM1, dans les trois modes vidéo disponibles. Il est tiré de l'expérience concrète du portage de **Sokoban** (47-72 niveaux par mode) et **Connect 4** (les trois modes).

**Documents voisins**

| Fichier | Contenu |
|---------|---------|
| [`APPLE1DEV.md`](APPLE1DEV.md) | Playbook agent : presets, déploiement, CLI examples, pièges courts |
| [`SILICONBUGS.md`](SILICONBUGS.md) | TMS9918 réel vs POM1, timings VRAM strict, sprites |
| [`doc/CLI.md`](../doc/CLI.md) | Liste exhaustive des flags (`--preset`, `--silicon-strict`, …) |
| [`TODO6502.md`](TODO6502.md) | Backlog projets sous `dev/projects/` |

**Sommaire (titres §1–§11)** — toolchain · matériel commun · pièges 6502 · mode texte · HGR · TMS9918 · patterns jeux · zero page · implémentations de référence · ressources externes · checklist.

---

## 1. Toolchain et workflow

### Outils

- **ca65** — assembleur 6502 de la suite cc65
- **ld65** — linker, prend un `.cfg` pour placer segments et ZP
- **python3** — génère le hex dump Woz Monitor à partir du binaire

**Installer cc65** : `sudo apt install cc65` (Debian/Ubuntu) · `sudo dnf install cc65`
(Fedora) · `sudo pacman -S cc65` (Arch) · `brew install cc65` (macOS) ·
<https://cc65.github.io/> (Windows/autres). Vérifier avec `ca65 --version`.

> **Le plus simple pour débuter** : le **POM1 Bench** intégré (*DevBench → POM1
> Bench*, desktop) édite, assemble/compile (asm **ou** C) et lance en un clic, avec
> un squelette `HELLO WORLD` par cible. Copie `dev/projects/_template/` pour un
> point de départ minimal. En C → [`Programming_Apple1_C.md`](Programming_Apple1_C.md).

### Commandes standard

Les sources vivent sous `dev/projects/<name>/` (chacun a son `Makefile`). Workflow manuel si besoin :

```bash
# Assemblage
ca65 -o build/MyGame.o dev/projects/<name>/MyGame.asm

# Linker avec config spécifique
ld65 -C dev/cc65/apple1_4k.cfg -o build/MyGame.bin build/MyGame.o

# Conversion binaire → Woz Monitor (hex dump 16 octets par ligne, adresses en hex)
python3 -c "
data = open('build/MyGame.bin','rb').read()
base = 0x0280
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    print(f'{base+i:04X}: ' + ' '.join(f'{b:02X}' for b in chunk))
" > software/<dir>/MyGame.txt
```

Le binaire compilé et son `.txt` Woz hex sont déposés sous `software/<dir>/` — c'est là que POM1 va les lire (les hooks d'auto-activation de carte sont câblés sur `software/Graphic HGR/`, `software/Graphic TMS9918/`, etc.).

### Chargement dans POM1

1. Choisir un preset avec la carte voulue — tableau **Machine Presets** dans [`README.md`](../README.md) (ex. TMS9918 + CodeTank → **preset 6**, GEN2 HGR → **12**).
2. (Ou auto-enable : placer le livrable sous `software/hgr/`, `software/tms9918/`, etc. — voir [`APPLE1DEV.md`](APPLE1DEV.md) §8.)
3. **File > Load Memory** → sélectionner le `.txt`
4. Dans le Woz Monitor, taper `280R` (ou l’adresse de démarrage du linker)

### Configs linker disponibles

| Config | CODE range | Taille | Usage typique |
|--------|-----------|--------|---------------|
| `dev/cc65/apple1_4k.cfg` | `$0280-$127F` | 4 096 B | Jeux texte ou TMS9918 (VRAM hors bus) — config par défaut |
| `dev/cc65/apple1_gen2.cfg` | `$E000-$EFFF` | 4 096 B | Jeux HGR : code dans la banque haute (lancer avec `E000R`), le framebuffer `$2000-$3FFF` reste réservé (writes directs). Programmes > 4 Ko : cfg split-bank dédié (cf. `hgr_chess/apple1_chess_hgr.cfg`, `games_sokoban/apple1_sok_hgr.cfg`) |
| `dev/cc65/pom1_fantasy.cfg` | configurable | — | Preset Multiplexing Fantasy (POM1-only) |

La syntaxe minimale d'un `.cfg` :

```
MEMORY {
    ZP:     start = $0000, size = $0023, type = rw, define = yes;
    CODE:   start = $0280, size = $1000, type = ro, define = yes, file = %O;
}
SEGMENTS {
    ZEROPAGE: load = ZP,   type = zp;
    CODE:     load = CODE, type = ro;
}
```

---

## 2. Matériel Apple 1 commun à tous les modes

### Adresses I/O

| Adresse | Rôle |
|---------|------|
| `$D010` | `KBD` — données clavier (bit 7 = 1 quand touche pressée, reste ASCII sur 7 bits) |
| `$D011` | `KBDCR` — registre de contrôle clavier, bit 7 = 1 quand touche disponible |
| `$D012` | `DSP` — afficher un caractère, bit 7 doit être à 1 ; bit 7 de la lecture = 0 quand prêt |
| `$FFEF` | `ECHO` — routine Woz Monitor pour imprimer A à l'écran (utilise DSP) |
| `$FFFC-$FFFF` | Vecteurs Reset/IRQ (en ROM Woz Monitor) |

### Convention PIA bit 7

L'Apple 1 utilise le **bit 7 comme "data valid"** sur la PIA 6821 :
- **Écriture vers ECHO** : le caractère doit avoir bit 7 à 1 → `ORA #$80` avant `JSR ECHO`
- **Lecture depuis KBD** : le bit 7 est toujours à 1, on le retire avec `AND #$7F`
- Le caractère `CR` envoyé à ECHO est donc `$8D` (`$0D | $80`)

### Majuscules forcées

Le clavier Apple 1 force les majuscules par défaut (matériel). Ne comparez qu'aux majuscules :

```asm
CMP #'W'          ; OK, toujours vrai si l'utilisateur tape 'w' ou 'W'
; CMP #'w'        ; jamais vrai
```

### Boucle d'attente touche standard

```asm
wait_key:
@wk:    LDA KBDCR
        BPL @wk           ; bit 7 = 0 → pas de touche, on attend
        LDA KBD
        AND #$7F          ; retire le bit 7 PIA
        RTS
```

### Routine d'impression de chaîne

```asm
print_str_ax:
        STA str_lo
        STX str_hi
        LDY #$00
@lp:    LDA (str_lo),Y
        BEQ @dn
        ORA #$80          ; bit 7 pour ECHO
        JSR ECHO
        INY
        BNE @lp
@dn:    RTS
```

Appel : `LDA #<str_title; LDX #>str_title; JSR print_str_ax`.

---

## 3. Pièges 6502 qui m'ont mordu

### Portée de branche ±127 octets

Les instructions de branchement (`BEQ`, `BNE`, `BCC`, `BCS`, `BPL`, `BMI`, etc.) ne peuvent atteindre qu'une cible à ±127 octets de l'instruction suivante. Les routines longues (check_win, execute_move) sortent rapidement de cette portée.

**Solution 1** : inverser la condition et `JMP` :
```asm
        BCC @skip
        JMP @target_too_far
@skip:
```

**Solution 2** : trampoline au milieu de la routine :
```asm
@blk_tr:
        JMP @blocked       ; visible depuis les deux moitiés
```

### ADC sans CLC

`ADC` ajoute le carry en plus de l'opérande. Si le carry n'est pas maîtrisé, on additionne `0` ou `1` aléatoirement. **Règle** : `CLC` avant chaque premier `ADC` d'une somme. Dans une chaîne multi-précision (16-bit +, 16-bit +), on laisse le carry se propager naturellement pour les ADCs suivants.

```asm
        CLC               ; indispensable
        LDA lo1
        ADC lo2
        STA result_lo
        LDA hi1
        ADC hi2           ; pas de CLC ici : on propage le carry
        STA result_hi
```

### TAX clobber X — règle d'or

Quand un helper retourne une valeur en A via une lookup table, la tentation est d'écrire :

```asm
helper:
        TAX
        LDA tbl,X
        RTS
```

**Ça casse silencieusement** l'appelant qui tenait un index dans X. Le `STA ARR,X` qui suit va écrire n'importe où. Dans Sokoban, ce bug a fait "disparaître" le joueur au retour sur sa case de départ.

**Règle** : toujours utiliser `TAY` pour la lookup dans un helper, jamais `TAX` :

```asm
helper:
        TAY
        LDA tbl,Y
        RTS
```

Y est rarement utilisé par l'appelant pour des index d'array (c'est plutôt X), donc on le clobbère sans risque.

### Accès aux tableaux > 256 octets

`LDA $4000,Y` avec Y=0..255 couvre une page. Au-delà, deux approches :

**Tableau aligné page** à `$XX00` : utiliser `(zp),Y` avec un pointeur dont le high byte est incrémenté par index :
```asm
        LDA #>ARRAY        ; = $40
        CLC
        ADC index_hi
        STA ptr_hi
        LDA #<ARRAY        ; = $00 si page-aligned
        STA ptr_lo
        LDY index_lo
        LDA (ptr_lo),Y
```

**Tables lo/hi parallèles** : pratique pour ~20 entrées 16-bit (ex. HGR scanlines, row*128 lookup) :
```asm
        LDX row
        LDA offset_lo,X
        STA ptr_lo
        LDA offset_hi,X
        STA ptr_hi
```

### `.include` relatif

En ca65, `.include "fichier.inc"` résout par rapport au fichier source. Donc placez le `.inc` dans le même dossier.

---

## 4. Mode texte — terminal 40×24

### Philosophie

L'écran texte Apple 1 est **append-only** : pas d'adressage direct du curseur, les caractères s'affichent à la position courante et `$0D` passe à la ligne suivante (en scrollant si nécessaire).

Conséquence : **pas de refresh partiel**. Pour "redessiner", on réimprime toute la scène et on laisse le scroll faire son travail — les frames précédentes sortent naturellement par le haut.

### Structure d'une frame minimaliste

```asm
render_screen:
        ; Ligne blanche en entrée (sépare de la frame précédente)
        LDA #$8D
        JSR ECHO

        ; Contenu du jeu (ex: 12 rangées de grille)
        ; ... loop d'impression ...

        ; Pied de page avec touches dispo
        LDA #<str_footer
        LDX #>str_footer
        JSR print_str_ax
        RTS
```

**Ce qu'il ne faut pas faire** :
- Réimprimer le titre `* SOKOBAN *` à chaque coup (pollution de l'historique, user voit défiler du texte inutile)
- Effacer avec 24 `CR` à chaque frame (gaspille du temps et fait clignoter)

### Alignement texte avec séparateurs

Attention à la cohérence des largeurs de cellule entre les lignes de séparation et les lignes de données :

```
       +---+---+---+    ← 4 chars par segment (1 '+' + 3 '-')
       | X | Y | Z |    ← doit aussi faire 4 chars par cellule ( ' X |' )
       +---+---+---+
```

Si on imprime seulement `X |` (3 chars), les `|` se décalent visiblement des `+`. Bug classique du Connect 4 premier jet.

### Exemple d'implémentation

`dev/projects/games_sokoban/Sokoban.asm` — grille ASCII 20×12, redraw plein à chaque coup, ~4 KB binaire avec 47 niveaux.

---

## 5. Mode HGR — GEN2 Color Graphics Card

### Caractéristiques

- **Résolution** : 280×192 pixels
- **Framebuffer** : `$2000-$3FFF` (8 KB)
- **Disposition scanline** : non-linéaire Apple II (3 groupes de 64 lignes, interleave par 8)
- **Format** : 7 pixels par octet, **bit 7 = sélecteur de groupe NTSC** (pas un pixel !)

### Couleurs NTSC par artéfact

Le bit 7 de chaque octet détermine le "groupe" :
- Groupe 0 (bit 7 = 0) : violet/vert
- Groupe 2 (bit 7 = 1) : bleu/orange

Le pixel est au final :
- **Blanc** si le pixel allumé a un voisin allumé (à gauche ou à droite — `resolveColor` : `prevOn || nextOn`)
- **Couleur** si le pixel est isolé :
  - Position screenX paire, groupe 0 → violet
  - Position screenX impaire, groupe 0 → vert
  - Paire, groupe 2 → bleu
  - Impaire, groupe 2 → orange

### Pour avoir du blanc propre

**Règle** : chaque pixel allumé doit avoir un voisin allumé à côté, dans le même octet ou à la frontière. Un trait vertical 1-pixel d'épaisseur rendra toujours en violet/vert/bleu/orange selon la position. **Un trait 2 pixels d'épaisseur rendra blanc**.

### Table de lookup hgr_tables.inc

Le fichier `dev/lib/hgr/hgr_tables.inc` (inclus via `-I ../../lib/hgr` dans les Makefiles projet) fournit (896 octets + 30 d'code) :
- `hgr_lo[192]`, `hgr_hi[192]` : adresse de chaque scanline (gère l'interleave Apple II) — via `hgr_scanline.inc`
- `hgr_col[256]`, `hgr_mask[256]` : pour un pixel à screenX x, donne la colonne d'octet et le bitmask — via `hgr_plot_tables.inc`
- `plot_pixel` : routine ~45 cycles qui pose un pixel à (cur_x, cur_y) — via `hgr_plot.asm`
- `clear_hgr` : zéro `$2000-$3FFF` — via `hgr_clear.asm`

C'est désormais un *bundle ombrelle* qui `.include` ces quatre modules ; pour n'en prendre qu'une partie, inclure les modules directement. `umul8` (multiplication 8×8 → 16-bit) **n'est plus dans ce bundle** : il vit dans `dev/lib/m6502/multiply.asm`, à inclure séparément si besoin.

**Piège** : même si vous n'utilisez pas `plot_pixel`, ses variables ZP (`cur_x`, `cur_y`, `ptr_lo`, `ptr_hi`) doivent être déclarées, sinon l'assembleur échoue.

### Tuiles alignées sur les octets

Pour simplifier, utilisez des tuiles dont la largeur est un multiple de 7 : **7, 14, 21, 28 pixels**. Chaque ligne de la tuile s'écrit en 1, 2, 3 ou 4 octets entiers.

Exemple 14×16 : 2 octets × 16 scanlines = 32 octets par tuile.

### Tuiles non-alignées — sub-byte rendering

Si la largeur n'est pas multiple de 7 (ex. maze avec murs 4 pixels), il faut des **tables de lookup répétant un motif de 7**. Pour un bloc à la grille gx :

| gx%7 | col_byte offset | col_mask1 | col_mask2 |
|------|-----------------|-----------|-----------|
| 0 | +0 | $0F | $00 |
| 1 | +0 | $70 | $01 |
| 2 | +1 | $1E | $00 |
| 3 | +1 | $60 | $03 |
| 4 | +2 | $3C | $00 |
| 5 | +2 | $40 | $07 |
| 6 | +3 | $78 | $00 |

`fill_block` fait un read-modify-write : `byte |= mask1`, et si `mask2 ≠ 0`, `byte+1 |= mask2`. Voir `dev/projects/hgr_maze/HGR_Maze.asm`.

### Astuce scanline stride +$0400

Dans un groupe de 8 scanlines consécutives (Apple II HGR), la suivante est à `+$0400`. Utilisable pour écrire une tuile de 8 scanlines sans lookup :

```asm
        LDA #$04
        STA stride_counter
@loop:
        ; écrire la scanline
        LDA ptr_hi
        CLC
        ADC #$04          ; +$0400
        STA ptr_hi
        DEC stride_counter
        BNE @loop
```

**Attention** : ça casse quand on franchit une frontière de groupe (scanline 8, 16, 24…). Pour des tuiles de plus de 8 scanlines, utilisez `hgr_lo`/`hgr_hi` à chaque ligne.

### Initialisation et effacement

```asm
.include "hgr_tables.inc"  ; à la fin du fichier

; au début du programme
JSR clear_hgr              ; zéro le framebuffer
```

### Exemple d'implémentation

- `dev/projects/hgr_maze/HGR_Maze.asm` — maze sub-byte rendering (murs 4 pixels)
- `dev/projects/hgr_sokoban/HGR_Sokoban.asm` — jeu complet 72 niveaux, 14×16 tuiles, delta rendering
- `dev/projects/hgr_mandelbrot/HGR_Mandelbrot.asm` — calcul + pixel plotting
- `dev/projects/hgr_house/HGR_House.asm` — dessin de formes

---

## 6. Mode TMS9918 — P-LAB Graphic Card

### Caractéristiques

- **Résolution** : 256×192 pixels (Graphics I : 32×24 caractères 8×8)
- **VRAM** : 16 KB **séparée** de la RAM principale (communication par I/O seulement)
- **I/O** : `$CC00` (données) et `$CC01` (contrôle)

### Mode Graphics I (le sweet spot pour les jeux tuile)

Tables en VRAM :
| Table | Adresse VRAM | Taille | Contenu |
|-------|-------------|--------|---------|
| Pattern table | `$0000-$07FF` | 2048 B | 256 glyphes 8×8 (8 octets chacun) |
| Name table | `$1800-$1AFF` | 768 B | 32×24 codes de caractère |
| Colour table | `$2000-$201F` | 32 B | **Une entrée par groupe de 8 caractères** |
| Sprite attr | `$1B00-$1B7F` | 128 B | 32 sprites × 4 octets |
| Sprite pattern | `$3800-$3FFF` | 2048 B | 256 patterns sprites |

### Séquence d'initialisation

```asm
init_vdp:
        ; 1. Programmer les 8 registres
        LDX #$00
@rl:    LDA vdp_regs,X
        STA $CC01             ; valeur
        TXA
        ORA #$80              ; flag d'écriture registre
        STA $CC01             ; numéro de registre + $80
        INX
        CPX #$08
        BNE @rl

        ; 2. Uploader les patterns (voir ci-dessous)
        ; 3. Uploader la colour table
        ; 4. Effacer la name table
        ; 5. Désactiver les sprites (IMPORTANT)

vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01
        ; R0=mode, R1=16K+display on+GfxI, R2=name@$1800, R3=colour@$2000,
        ; R4=pattern@$0000, R5=sprite attr@$1B00, R6=sprite pattern@$3800, R7=backdrop black
```

### Latch d'adresse VRAM sur $CC01 (deux écritures)

```asm
; Pour écrire à l'adresse V en VRAM :
        LDA #<V
        STA $CC01             ; low byte
        LDA #>V
        ORA #$40              ; flag write (sans bit $40 = read)
        STA $CC01             ; high byte | $40

; Puis écrire les données (auto-incrémentation) :
        LDA data
        STA $CC00
        LDA data2
        STA $CC00             ; va à V+1 automatiquement
```

### Désactiver les sprites — obligatoire

Graphics I active toujours les sprites. Par défaut, la sprite attribute table a des valeurs aléatoires → des sprites poubelle apparaissent. Écrire `$D0` au premier octet Y (adresse `$1B00`) arrête la chaîne de sprites :

```asm
        LDA #$00
        STA $CC01
        LDA #$5B              ; $1B | $40
        STA $CC01
        LDA #$D0
        STA $CC00
```

### Astuce colour-group pour les couleurs gratuites

La colour table a **une couleur fg/bg par groupe de 8 caractères**. Astuce : placez chaque type de tuile au premier caractère de son groupe :

| Tuile | Char code | Groupe | Couleur |
|-------|-----------|--------|---------|
| Tuile 0 | 0 | 0 (chars 0-7) | Couleur A |
| Tuile 1 | 8 | 1 (chars 8-15) | Couleur B |
| Tuile 2 | 16 | 2 (chars 16-23) | Couleur C |
| Tuile 3 | 24 | 3 (chars 24-31) | Couleur D |
| etc. | | | |

Chaque tuile a alors sa propre couleur sans effort. Les chars intermédiaires (1-7, 9-15, ...) restent inutilisés.

Sokoban TMS utilise cette technique pour 7 tuiles colorées (mur gris, cible rouge, caisse jaune, caisse-sur-cible verte, joueur bleu, joueur-sur-cible vert médium).

### Grosses tuiles — pièces 4×4 cases

Pour des sprites plus gros, utilisez plusieurs caractères par pièce :

- **2×2 chars** (16×16 px) : 4 glyphes par pièce, tient dans 1 groupe couleur
- **3×3 chars** (24×24 px) : 9 glyphes, s'étale sur 2 groupes — forcez les deux groupes à la même couleur
- **4×4 chars** (32×32 px) : 16 glyphes = **2 groupes exactement** → 1 triplet de groupes par type de pièce

Connect 4 TMS utilise 4×4 (32×32 px par pion) : plateau 28×24 cases = 224×192 pixels, remplit l'écran presque entièrement. Trois triplets de groupes (empty, red, yellow) × 16 glyphes = 48 chars, 7 entrées couleur.

### Format octet couleur

`(fg << 4) | bg` — fg et bg sont des index palette (0-15).

Palette TMS9918 :
| # | Couleur |
|---|---------|
| 0 | Transparent (voir backdrop R7) |
| 1 | Noir |
| 2 | Vert moyen |
| 3 | Vert clair |
| 4 | Bleu foncé |
| 5 | Bleu clair |
| 6 | Rouge foncé |
| 7 | Cyan |
| 8 | Rouge moyen |
| 9 | Rouge clair |
| 10 | Jaune foncé (bois) |
| 11 | Jaune clair |
| 12 | Vert foncé |
| 13 | Magenta |
| 14 | Gris |
| 15 | Blanc |

### Delta rendering sur TMS

Trivial comparé à HGR : calculez l'adresse name table `$1800 + row*32 + col`, latchez sur `$CC01`, écrivez un seul code caractère sur `$CC00`. Pas besoin d'effacer, pas besoin de redessiner les voisins.

### Indépendance des écrans

L'écran texte Apple 1 et la fenêtre TMS9918 sont **deux afficheurs indépendants**. Convention : utilisez le texte pour le titre, le prompt clavier (QWERTY/AZERTY), les messages de victoire ; utilisez le TMS pour le jeu lui-même.

### Synchronisation frame — POLLING recommandé (l'IRQ marche aussi)

**Règle d'or P-LAB** : la carte P-LAB Apple-1 **câble** la broche `/INT` du TMS9918 vers `/IRQ` du 6502 (trace vérifiée sur le vrai matériel par Parmigiani). Le soft Nippur72 ne s'en sert pas : la convention est de synchroniser les frames par **polling** du status register — c'est plus simple, portable, et indépendant du flag I. L'IRQ-on-VBlank fonctionne néanmoins (R1 bit 5 + `CLI` + handler au vecteur `$FFFE` lisant `$CC01` atomiquement), mais reste l'option à éviter sauf besoin précis.

C'est le pattern que ciblent les libs Nippur72 et que tous les jeux POM1 (Galaga, Sokoban, Snake, Life, Rogue, Asteroids, Connect4, etc.) utilisent.

#### Idiome standard

```asm
        ; Wait for next VBlank — silicon-correct polling pattern
        BIT $CC01             ; drain stale F flag (clears bits 5/6/7)
@v_wait:
        BIT $CC01
        BPL @v_wait           ; bit 7 = 0 → pas encore VBlank
        ; ici : on est dans VBlank, ~4 554 cycles de bande passante VRAM
        ; "gate 2c" disponibles avant l'active display de la prochaine frame
```

Ou avec la macro `WAIT_VBLANK` partagée dans `dev/lib/tms9918/tms9918.inc` :

```asm
.include "tms9918.inc"

        WAIT_VBLANK           ; expanse au pattern ci-dessus, 7 octets
        ; ... upload SAT, sprites, animations …
```

#### Pourquoi pas d'IRQ

Cas qui **deadlocke sur silicon P-LAB et sur POM1 par défaut** :

```asm
        LDA #$E0              ; R1 = display ON + IRQ enable + 16K
        STA $CC01
        LDA #$81              ; reg 1
        STA $CC01
        CLI                   ; autoriser /IRQ
@loop:  JMP @loop             ; attendre IRQ frame en $FFFE
```

Le 6502 attendra éternellement parce que la broche `/INT` du chip n'est pas connectée à `/IRQ`. POM1 émule ce comportement fidèlement (`TMS9918::irqAsserted()` retourne `false` tant que `setIrqStrapped(true)` n'a pas été appelé — et personne ne l'appelle dans la chaîne par défaut).

#### Side effect du polling sur les bits 5/6

Lire `$CC01` efface bits 5 (collision), 6 (5S overflow) **et** 7 (F flag) en bloc. Si tu utilises ces flags, lis-les **avant** le `WAIT_VBLANK`, ou snapshot-les dans une variable :

```asm
        LDA $CC01             ; snapshot complet
        STA last_status
        AND #$80              ; isole F
        BEQ @nope             ; pas de VBlank cette fois
        LDA last_status
        AND #$20              ; bit 5 collision
        ; …
```

Pour les jeux qui ne lisent que F (cas majoritaire), le clobber de 5/6 n'a aucune conséquence visible.

#### Cas particulier : strap FPGA community

Certains utilisateurs ont **modifié leur réplica Apple-1 / replica-1 P-LAB** pour bridger `int_n_o` (VDP) → `irq_n` (6502) avec inverseur. Cette modif **n'est pas P-LAB d'origine**. POM1 expose `TMS9918::setIrqStrapped(true)` pour émuler ce mod. Tant que tu écris du code qui doit tourner sur **P-LAB stock**, ignore cette branche : poll, c'est tout. (Détails dans [`SILICONBUGS.md`](SILICONBUGS.md) Bug N°2.)

#### Strict silicon — écritures VRAM trop rapides

Quand **Silicon Strict** est actif (défaut hors presets Multiplexing Fantasy), POM1 peut **ignorer** des octets si les accès `$CC00`/`$CC01` arrivent plus vite que le modèle slot-table — même comportement que le trop rapide sur puce réelle. Voir [`SILICONBUGS.md`](SILICONBUGS.md) §2 (Bug N°1), helpers `tms9918_pad12`, patcher `tools/silicon_strict_patch.py`. Toggle : menu Hardware ou [`doc/CLI.md`](../doc/CLI.md) (`--silicon-strict` / `--no-silicon-strict`).

### Exemples d'implémentation

- `dev/projects/tms9918_sokoban/TMS_Sokoban.asm` — 47 niveaux, tuiles 8×8 avec 7 couleurs
- `dev/projects/tms9918_connect4/TMS_Connect4.asm` — pions 32×32 sur plateau bleu plein écran
- `dev/projects/tms9918_galaga/TMS_Galaga.asm:2593-2600` — pattern de polling commenté en place

---

## 7. Patterns partagés pour les jeux

### Grille d'état à `$4000`

Sur les presets **Fantasy** (RAM plate 64 Ko) ou quand la fenêtre `$4000-$7FFF` n’est pas occupée par la ROM CodeTank / Juke-Box, `$4000+` peut tenir une grille ou des buffers volumineux. Sur les presets **dual-bank 8+8 Ko**, la même zone peut être ROM ou hors périmètre selon la carte — vérifier le preset dans [`README.md`](../README.md) ou tester sur la cible. Pour une grille **20×12** (240 octets), une page dans la RAM utilisateur basse ou **`GRID_BASE = $4000`** (quand disponible) suffisent :

```asm
GRID_BASE = $4000

; Accès rapide :
        LDY cell_index              ; 0..239
        LDA GRID_BASE,Y             ; indexed absolute
```

Pour calculer l'index d'une cellule `(row, col)`, utilisez une lookup au lieu de multiplier :

```asm
row_x20:
        .byte 0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200, 220

; idx = row*20 + col :
        LDX row
        LDA row_x20,X
        CLC
        ADC col
        TAX                         ; ou TAY pour lookup ensuite
```

### Format niveau portable

Header 4 octets puis ASCII :

```asm
level1:
        .byte 5, 3, 4, 7            ; w, h, row_offset, col_offset
        .byte "#####"
        .byte "#.$@#"
        .byte "#####"
```

L'offset permet de centrer les petits niveaux dans une grille plus grande (20×12 par exemple).

Convention Sokoban : `#` mur, `.` cible, `$` caisse, `@` joueur, `*` caisse sur cible, `+` joueur sur cible, ` ` sol.

Pour des niveaux non-rectangulaires (ex. Microban), padder les lignes courtes avec des espaces avant de compiler.

### Prompt clavier QWERTY/AZERTY

Les touches `1` et `2` sont à la même position physique sur les deux layouts, idéales comme prompt. Stockez les touches variables en zero-page :

```asm
.zeropage
key_up_code:   .res 1   ; 'W' (QWERTY) ou 'Z' (AZERTY)
key_left_code: .res 1   ; 'A' ou 'Q'

.code
        LDA #<str_layout
        LDX #>str_layout
        JSR print_str_ax
@wait:
        JSR wait_key
        CMP #'1'
        BEQ @qwerty
        CMP #'2'
        BEQ @azerty
        JMP @wait
@qwerty:
        LDA #'W'
        STA key_up_code
        LDA #'A'
        STA key_left_code
        JMP @start
@azerty:
        LDA #'Z'
        STA key_up_code
        LDA #'Q'
        STA key_left_code
@start:
        ; ...

; Dans move_loop :
        CMP key_up_code
        BEQ key_up
        CMP #'S'                    ; S et D partagent la position physique
        BEQ key_down
        CMP key_left_code
        BEQ key_left
        CMP #'D'
        BEQ key_right
```

### Séparation logique / rendu

La logique de jeu (collision, règles, condition de victoire, etc.) est **100% partageable** entre les trois modes. Seules changent :

- `init_display` : `clear_hgr` (HGR) vs `init_vdp` (TMS) vs rien (texte)
- `render_all` : boucle qui dessine la grille complète
- `draw_cell` : dessine une seule cellule à une position grille donnée
- Les données visuelles : bitmap HGR, patterns TMS, caractères ASCII

**Modèle** :
```
main → init_display → game_loop → {
    init_level             ; logique commune
    render_all             ; spécifique mode
    move_loop → {
        wait_key
        execute_move       ; logique commune ; modifie la grille
        if moved: draw_cell(s)  ; spécifique mode ; redessine 1-4 tuiles
        check_win          ; logique commune
    }
}
```

### Delta rendering

Après un coup, le nombre de cellules modifiées est borné (2-4 pour Sokoban : ancienne et nouvelle position du joueur, + éventuellement la caisse poussée). **Ne redessiner que ces cellules** fait passer le temps de rendu de ~80 ms (frame complète) à < 1 ms. Gameplay fluide garanti.

### Condition de victoire : attention aux cases indirectes

Sokoban : une cible est "vide" si elle contient encore `TILE_TARGET` (2) **ou** `TILE_PLAYER_TARGET` (6, joueur debout dessus sans caisse). Vérifier seulement la valeur 2 cause un faux positif quand le joueur marche sur la dernière cible.

---

## 8. Zero page — rappel

La zero page Apple 1 va de `$0000` à `$00FF`, mais les configs cc65 en réservent typiquement 32 à 35 octets pour l'utilisateur (`$0000-$001F` ou `$0000-$0022`). Le monitor Woz et BASIC utilisent le reste.

Usage typique pour un jeu :
```asm
.zeropage
temp:           .res 1
temp2:          .res 1
ptr_lo:         .res 1  ; pointeur générique 1
ptr_hi:         .res 1
src_lo:         .res 1  ; pointeur source (patterns, strings)
src_hi:         .res 1
current_player: .res 1
move_count:     .res 1
winner:         .res 1
row_cnt:        .res 1
col_cnt:        .res 1
draw_row:       .res 1
draw_col:       .res 1
; ... variables spécifiques jeu
```

---

## 9. Implémentations de référence

Les trois ports de **Sokoban** partagent la même grille, la même logique de coup (`execute_move`, `leave_tile`, `enter_player`, `check_win`), le même format de niveau. Seul le rendu diffère :

| Fichier | Mode | Taille | Niveaux |
|---------|------|--------|---------|
| `dev/projects/games_sokoban/Sokoban.asm` | Texte | 4054 B | 47 |
| `dev/projects/hgr_sokoban/HGR_Sokoban.asm` | HGR GEN2 | 7399 B | 72 |
| `dev/projects/tms9918_sokoban/TMS_Sokoban.asm` | TMS9918 | 4354 B | 47 |

Les trois ports de **Connect 4** de même :

| Fichier | Mode | Taille |
|---------|------|--------|
| `dev/projects/games_connect4/Connect4.asm` | Texte | 1021 B |
| `dev/projects/hgr_connect4/HGR_Connect4.asm` | HGR GEN2 | 2003 B |
| `dev/projects/tms9918_connect4/TMS_Connect4.asm` | TMS9918 | 1230 B |

Autres programmes GEN2 utiles comme modèle :
- `dev/projects/hgr_maze/HGR_Maze.asm` : maze sub-byte rendering (4-px walls)
- `dev/projects/hgr_mandelbrot/HGR_Mandelbrot.asm` : calcul + pixel plotting
- `dev/projects/hgr_house/HGR_House.asm` : dessin de formes

Bibliothèques réutilisables (`dev/lib/`) :
- `dev/lib/apple1/apple1.inc` — équates Wozmon + PIA
- `dev/lib/m6502/math.asm` — trig point-fixe, RNG LFSR, impression décimale
- `dev/lib/tms9918/{tms9918.inc,tms9918m2.asm}` — équates VDP + driver Mode 2
- `dev/lib/hgr/{hgr_tables.inc,smiley.inc}` — tables HGR
- `dev/lib/games/sokoban/sokoban_*.inc` — données niveaux Sokoban partagées
- `dev/lib/games/chess/{chess_engine.asm,chess_text_io.asm,chess_*.inc}` — moteur d'échecs partagé (text/HGR/TMS9918)

---

## 10. Ressources externes

- **[`SILICONBUGS.md`](SILICONBUGS.md)** / **[`APPLE1DEV.md`](APPLE1DEV.md)** — pièges TMS9918 et déploiement POM1 (préférer ces fichiers aux résumés dispersés).
- **Microban I (David W. Skinner, 2000)** — 155 niveaux Sokoban progressifs, petits et pédagogiques.
  Sources brutes : `https://github.com/martin-t/sokoban-solver/tree/master/levels/microban1/N.txt`
- **Sokoban Wiki (format des niveaux)** : http://sokobano.de/wiki/index.php?title=Level_format
- **cc65 documentation** : https://cc65.github.io/doc/
- **TMS9918 datasheet / reference** : pour les détails de registres et timings
- **Apple II HGR memory layout** : documentation Apple II (le GEN2 est Apple II-compatible)
- **P-LAB cards** : documentation PDF dans `doc/` (Graphic Card ENG, SID, microSD, etc.)

---

## 11. Checklist avant de commencer un nouveau jeu

1. Choisir le(s) mode(s) cible(s) — texte, HGR, TMS, ou les trois
2. Dimensionner la grille de jeu (tient dans 256 octets ? page-alignée à `$4000` ?)
3. Choisir la config linker selon le budget code
4. Partager la logique pure (pas d'I/O) d'un mode à l'autre
5. Implémenter `draw_cell` avant `render_all` — pour tester visuellement
6. Ajouter le delta rendering après avoir validé le rendu complet
7. Pour HGR : dessiner les tuiles byte-alignées (largeur multiple de 7) si possible
8. Pour TMS : utiliser l'astuce colour-group dès le début si besoin de multi-couleurs
9. Gérer le layout clavier (prompt `1`/`2`, stocker les touches en ZP)
10. Tester les cas limites : grille pleine, bordures, condition de victoire exotique

---

*Document rédigé à partir des mémoires agent et du code des jeux Sokoban et Connect 4 (3 ports chacun).*
