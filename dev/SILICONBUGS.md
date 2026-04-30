# SILICONBUGS.md — Coder pour vrai silicium Apple-1 + TMS9918

Ce document recense les **divergences POM1 ↔ silicium** observées (ou suspectées) sur le sous-système TMS9918, en particulier le **moteur sprites**. Il est conçu comme un carnet de bord pratique : chaque bug indique ce que fait le silicium, ce que fait POM1, l'impact sur le code, et le pattern de fix.

L'émulation POM1 est suffisamment fidèle pour développer en text/bitmap (Modes 1 et 2). Le moteur sprites présente des écarts qui ne se voient qu'en silicium réel — ce doc les documente pour qu'on n'y retombe plus.

---

## 1. Contexte & cible matérielle

- **Plateforme cible** : Apple-1 + carte TMS9918 (P-LAB Graphic Card) + CodeTank daughterboard.
- **Preset POM1 équivalent** : preset #2 — *P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)*.
- **Référence "OK partout" (silicium + POM1)** : `TMS_Logo v1.7` — interpréteur LOGO en Mode 2 bitmap, pas de sprites. Validation du sous-système VRAM/registres/modes graphiques.
- **Référence "OK POM1, KO silicium"** : `A1Galaga` — Mode 1 + sprites animés à 60 Hz. Sur silicium, sprites-artefacts et damiers parasites apparaissent autour des sprites légitimes dès l'écran de titre.

### Map mémoire utilisable (preset #2)

```
$0000-$00FF   Zero page (Apple-1 standard)
$0100-$01FF   Stack
$0200-$1FFF   User RAM (~3,5 Ko utiles après stack/zp)            ← seul bloc GARANTI
$2000-$3FFF   Libre côté CodeTank (mutex GEN2 HGR si carte présente) — TODO mesurer silicium
$4000-$7FFF   ROM CodeTank 16 Ko (jumper Lower OU Upper)
$CC00 / $CC01 TMS9918 DATA / CTRL
$E000-$EFFF   User RAM Hi 4k
$FF00-$FFFF   Wozmon + vecteurs
```

> **TODO silicium** : `$2000-$3FFF` théoriquement libre sur silicium si pas de carte HGR — à confirmer sur la cible. `$8000-$BFFF` est libre sans carte microSD/CFFA1/JukeBox.

---

## 2. BUG N°1 — Timing VRAM (cause primaire des artefacts Galaga)

### Ce que fait le silicium

Le TMS9918A est **esclave de son propre signal vidéo** : le scan pixel + le rafraîchissement DRAM sont prioritaires absolus. Le CPU ne reçoit que des **fenêtres d'accès VRAM** ouvertes par le séquenceur interne. Quand le CPU initie un accès à `$CC00` ou `$CC01` :

1. **Délai physique de préparation** : ~2 µs incompressibles.
2. **Attente de fenêtre libre** : variable, selon le mode actif et l'usage des sprites.

Si le CPU envoie le prochain octet **avant que la fenêtre précédente n'ait été consommée**, le pipeline interne déborde et **l'octet est perdu ou corrompu** dans la VRAM. Aucune erreur signalée.

### Les latences réelles dépendent du mode actif

Un cycle mémoire VDP ≈ **372 ns**. La fenêtre d'accès CPU s'ouvre :

| Mode actif | Fréquence fenêtre | Délai pire cas | Notes |
|---|---|---|---|
| **Affichage blanké** (R1 bit 6 = 0) ou **VBLANK** | continu | ~2 µs (préparation seule) | Bande passante libre, init VRAM rapide possible |
| **Mode Texte** (M1=1) | 1 sur 3 cycles | ~1,1 µs | Le plus tolérant — pas de sprites |
| **Mode Multicolore** (M2=1) | 1 sur 4 cycles | ~1,5 µs | |
| **Mode Graphic I / II avec sprites actifs** | 1 sur 16 cycles | **~6 µs** | Cas de Galaga |
| Mode Graphic I / II avec sprites désactivés (Y=$D0 dès SAT[0]) | 1 sur 6 cycles | ~2,2 µs | Astuce d'init si pas besoin de sprites |

**Pour Galaga (Mode 0 Graphic I + 10 sprites actifs)**, le pire cas est **~6 µs** — soit ~6,1 cycles 6502 à 1,022 MHz. Avec la marge de préparation 2 µs, viser **8 cycles entre accès** reste la règle prudente.

### Ce que fait POM1

`writeData()` dans `TMS9918.cpp:60-67` exécute l'écriture instantanément, sans contrainte de bande passante :
```cpp
void TMS9918::writeData(uint8_t value) {
    latchIsSecond = false;
    vram[vramAddr & 0x3FFF] = value;
    readAheadBuffer = value;
    vramAddr = (vramAddr + 1) & 0x3FFF;
    snapshotDirty = true;
}
```
Tous les octets passent, quel que soit le rythme. **Le code peut spammer `STA $CC00` à 1 cycle d'intervalle, POM1 dira "OK".**

### Impact sur le code (Galaga = pire cas ~6-8 µs)

À 1,022 MHz Apple-1 : **1 cycle ≈ 0,978 µs**. Cible prudente : **≥ 8 cycles entre deux accès VRAM** en Mode Graphic + sprites.

| Pattern 6502 | Cycles entre 2 writes VRAM | Verdict silicium |
|---|---|---|
| `STA $CC00` ; `STA $CC00` (back-to-back) | 4 | **KO** (≈ 3,9 µs) |
| `LDA #x` ; `STA $CC00` ; `LDA #y` ; `STA $CC00` | 6 | KO en Graphic+sprites (≈ 5,9 µs) |
| `STA $CC00` ; `LDA zp,X` ; `STA $CC00` | 7 | Limite Graphic, OK Text |
| `STA $CC00,X` ; `STA $CC00,X` | 5+5 = 10 | **OK** (≈ 9,8 µs) tous modes |
| `STA $CC00` ; `NOP` ; `NOP` ; `STA $CC00` | 4+2+2 = 8 | Limite Graphic |
| `STA $CC00` ; `NOP` ; `NOP` ; `NOP` ; `STA $CC00` | 4+2+2+2 = 10 | **OK** confortable |
| `STA $CC00` ; `INX` ; `LDA tab,X` ; `STA $CC00` | 4+2+4 = 10 | **OK** (loop typique) |

### Cas réel — `render_sprites` dans Galaga

Extrait de `dev/projects/tms9918_galaga/TMS_Galaga.asm:2551-2557` (slot joueur "caché") :

```asm
LDA #HIDDEN_Y      ; 2c
STA VDP_DATA       ; 4c  → écriture #1
LDA #$00           ; 2c   → 2 cycles écoulés
STA VDP_DATA       ; 4c  → écriture #2 (gap = 6c ≈ 5,9 µs)  KO
STA VDP_DATA       ; 4c  → écriture #3 (gap = 4c ≈ 3,9 µs)  KO
STA VDP_DATA       ; 4c  → écriture #4 (gap = 4c ≈ 3,9 µs)  KO
```

Ce pattern est répété pour chaque slot SAT (10 slots × 4 octets = 40 octets / frame, 60 Hz). Sur silicium, plusieurs octets sont perdus à chaque frame → SAT corrompue → **sprites fantômes à des positions/patterns aléatoires** (les fameux damiers parasites).

### Pourquoi TMS_Logo passe sur silicium

L'interpréteur LOGO calcule un pixel à la fois via une boucle bytecode → entre deux writes VRAM, il y a ~50 à 200 cycles d'arithmétique 6502. Le timing est respecté **par accident** grâce à la lenteur de l'interpréteur.

### Pourquoi le texte de Galaga passe

Galaga utilise en réalité le **Mode 0 (Graphic I)** — pas le Mode Texte (les deux sont incompatibles avec les sprites). Le rendu "texte" de l'écran titre est fait avec des **tuiles** dans la Pattern Table, écrite **une seule fois** au démarrage via une boucle lente. Les éventuels octets perdus sont noyés dans 768 entrées de Name Table, peu visibles.

À l'inverse, la **SAT (40 octets)** est réécrite à **60 Hz dans une boucle serrée** sans NOP → chaque frame perd plusieurs octets → SAT corrompue → sprites fantômes immédiatement visibles.

### Astuce d'init : blanker l'affichage pendant les uploads massifs

Pour charger Pattern Table, Name Table, Color Table en début de programme **sans contrainte de timing**, mettre `R1` bit 6 = 0 (display off) avant l'upload, puis le rallumer ensuite. Pendant le blank, la bande passante VRAM est entièrement disponible (seul le délai 2 µs de préparation s'applique) → boucles serrées OK.

```asm
; Avant init VRAM
LDA #$80          ; R1 = 16K + display OFF + tout reste à 0
STA VDP_CTRL
LDA #$81          ; reg 1
STA VDP_CTRL

; ... boucles de chargement VRAM serrées (Pattern, Name, Color tables) ...

; Affichage final
LDA #$E2          ; R1 = 16K + display ON + IRQ + sprites 16x16
STA VDP_CTRL
LDA #$81
STA VDP_CTRL
```

### Fix recommandés (par ordre de préférence)

**Option A — Macro `WRT_DATA_NOP` côté lib (recommandé)**

Ajouter dans `dev/lib/tms9918/tms9918.inc` :
```asm
.macro WRT_DATA_NOP val
    LDA #val           ; 2c
    STA VDP_DATA       ; 4c
    NOP                ; 2c
    NOP                ; 2c   total avant prochain write : 10c minimum
.endmacro

.macro WRT_DATA_REG    ; pour A déjà chargé
    STA VDP_DATA       ; 4c
    NOP                ; 2c
    NOP                ; 2c
.endmacro
```

**Option B — Indexed addressing (bonus 1 cycle gratuit)**

Remplacer `STA $CC00` par `STA $CC00,X` (X = 0). Coût : +1 cycle (4c → 5c) sans NOP. Deux STA indexées back-to-back = 10 cycles → OK.

**Option C — Réécriture en boucle table-driven**

Précalculer chaque frame une table SAT en RAM (40 octets), puis l'uploader avec une boucle `LDA tab,X / STA $CC00 / NOP / NOP / INX / BNE` (4+4+2+2+2+3 = 17c entre writes). Plus simple à raisonner que les writes en ligne.

### Validation

Sur émulateur, ces 3 options ne changent rien (POM1 est tolérant). Sur silicium, l'option A ou C **doit faire disparaître les sprites-artefacts**. Si ce n'est pas le cas, le bug primaire est ailleurs et il faut creuser les bugs N°2 à N°7.

---

## 3. BUG N°2 — Ligne /INT non câblée sur POM1

### Ce que fait le silicium

Quand R1 bit 5 = 1 (interrupt enable) et bit 7 du status register = 1 (frame flag), le TMS9918 tire la ligne `/INT` au niveau bas. Sur la carte P-LAB Apple-1, cette ligne est (en principe) câblée à `/IRQ` du 6502 → IRQ vector `$FFFE-$FFFF` exécuté à chaque VBlank.

### Ce que fait POM1

POM1 émule le bit 7 du status register correctement (`TMS9918.cpp:127-139`) mais **ne propage pas /INT vers le bus 6502**. Le CPU ne reçoit jamais d'interruption matérielle du VDP.

### Impact

Du code style :
```asm
LDA #$E0       ; display on + IRQ enable + 16K
STA VDP_CTRL
LDA #$81       ; reg 1
STA VDP_CTRL
CLI            ; autoriser IRQ
@wait: BRA @wait  ; attendre IRQ frame, traitée par handler en $FFFE
```
**fonctionnera sur silicium** (l'IRQ frappe à chaque VBlank) mais **deadlock sur POM1** (le CPU tourne en rond, jamais interrompu).

### Workaround portable

Toujours **poller bit 7 du status register** plutôt que dépendre de l'IRQ matérielle :
```asm
@vblank_wait:
    LDA VDP_CTRL    ; lecture status
    BPL @vblank_wait ; bit 7 = 0 → pas encore VBlank
    ; ici on est dans VBlank, status auto-cleared par la lecture
```
Marche identiquement sur les deux cibles. **C'est le pattern à utiliser par défaut.**

---

## 4. BUG N°3 — R1 bit 7 (mode 4K/16K) ignoré par POM1

### Ce que fait le silicium

R1 bit 7 sélectionne le mode mémoire :
- 0 → mode 4K (compatible TMS9928 ancien) — seuls les 4 premiers Ko de VRAM sont accessibles, l'addresse VRAM est tronquée à 12 bits.
- 1 → mode 16K — les 16 Ko sont accessibles (mode standard TMS9918A).

Au reset, **R1 = $00 → mode 4K**. Si le code initialise R1 sans positionner bit 7, le silicium reste en 4K et tout adressage VRAM ≥ $1000 est rebouclé sur les 4 premiers Ko. Conséquence : Pattern Table à $1800, Name Table à $1B00, etc. → **tout est corrompu**.

### Ce que fait POM1

`TMS9918.cpp` traite la VRAM comme un buffer 16 Ko inconditionnel. Bit 7 de R1 est ignoré dans tous les paths. Code qui oublie bit 7 → marche sur POM1, plante sur silicium.

### Impact code

Toujours initialiser R1 avec **bit 7 = 1**. Valeurs typiques :
- `$E0` = display on + IRQ enable + 16K (Mode 0)
- `$D0` = display on + 16K + sprites 16×16 (Mode 0, sprites large)
- `$F0` = display on + IRQ + 16K + Mode 1 (texte 40 col)
- `$C0` = display on + 16K (le bit 7 absent ici) → **WRONG**, à éviter

### Recommandation

Adopter une convention dans `dev/lib/tms9918/tms9918.inc` :
```asm
VDP_R1_BASE = $80        ; bit 7 = 16K, OBLIGATOIRE en silicium
VDP_R1_DISP = $40        ; display on
VDP_R1_IRQ  = $20        ; IRQ enable (sans effet POM1)
VDP_R1_M1   = $10        ; Mode 1 (text)
VDP_R1_M2   = $08        ; Mode 2 (bitmap) — combiné M3 avec R0 bit 1
VDP_R1_LRG  = $02        ; sprites 16×16
VDP_R1_MAG  = $01        ; sprites magnifiés ×2
```
Et **toujours** OR-er `VDP_R1_BASE` quand on écrit R1.

---

## 5. BUG N°4 — Collision sprite en overscan

### Ce que fait le silicium

Le sprite engine du TMS9918 raster une zone interne **plus large que l'écran visible** : ~32 pixels d'overscan à gauche (x = -32..-1) et 32 à droite (x = 256..287). Deux sprites qui se chevauchent dans cette zone hors-écran déclenchent le **bit 5 (collision) du status register** même si rien n'est visible.

### Ce que fait POM1

`scanSpritesForStatus()` dans `TMS9918.cpp:434-533` clipe à `[0, 256)` :
```cpp
if (sx < 0 || sx >= kScreenWidth) continue;
```
Les pixels en overscan ne sont pas comptés. **Aucune collision détectée hors écran.**

### Impact

Code qui spawn un sprite ennemi à `X = -30` (avec early-clock bit 7 du color byte) en attendant que la collision le détruise au passage du joueur **ne déclenchera jamais bit 5 sur POM1** mais le fera sur silicium. Risque de "ennemis fantômes" qui ne meurent que sur silicium.

### Recommandation

Ne pas s'appuyer sur la collision en zone overscan. Si le gameplay nécessite "collision off-screen", la calculer en software (bounding-box sur les coordonnées sprites en RAM) plutôt que via le status register.

---

## 6. BUG N°5 — Sprite scan 1×/frame (au VBlank) vs par scanline

### Ce que fait le silicium

Les flags collision (bit 5) et 5S (bit 6) sont mis à jour **scanline par scanline** pendant le rendu, en temps réel pendant que le faisceau trace l'image. Un poll du status register **au milieu d'une frame** voit l'état exact à la position courante du faisceau.

### Ce que fait POM1

`advanceCycles()` (`TMS9918.cpp:127-139`) appelle `scanSpritesForStatus()` **une seule fois** par frame, juste avant de lever bit 7. Pendant la frame, bits 5 et 6 sont **figés à leur valeur de la frame précédente**.

### Impact 1 — Détection de collision retardée

Du code qui poll status au milieu d'un mouvement sprite verra :
- **Silicium** : collision détectée dès qu'elle survient (latence < 1 ms).
- **POM1** : collision détectée seulement après le VBlank suivant, jusqu'à 16 ms plus tard.

Pour la majorité des jeux, ce n'est pas critique (on lit le status après VBlank). Mais le **bug N°10 ci-dessous (raster split via 5S)** dépend totalement de cette mise à jour scanline-by-scanline.

### Impact 2 — Lecture destructive du status

Le silicium **efface bit 7 (F), bit 6 (5S) ET bit 5 (C) à chaque lecture** du status register. POM1 reproduit ce comportement (`TMS9918.cpp:115-122` : `statusReg &= ~0xE0`).

**Conséquence : ne jamais lire status en milieu de frame "juste pour voir la collision"** — le bit 7 (VBlank flag) sera effacé avant que la routine de sync ne le capture, le jeu rate sa frame, désync musique/animation. Toujours regrouper la lecture du status à un seul endroit, après VBlank :

```asm
@wait_vbl: LDA VDP_CTRL    ; UNE SEULE lecture par frame
           BPL @wait_vbl   ; attend bit 7 (auto-cleared ici)
           TAY             ; sauvegarde
           AND #$20        ; bit 5 ?
           BNE @collision  ; collision détectée pendant la frame
           TYA
           AND #$40        ; bit 6 ?
           BNE @overflow_5s
```

### Recommandation

**Toujours lire le status après VBlank**, **jamais** en milieu de frame, sauf si le code exploite intentionnellement le hack raster split (voir Bug N°10).

---

## 7. BUG N°6 — Status bits 0..4 ne reflètent pas le "dernier sprite scanné"

### Ce que fait le silicium

Sur le TMS9918A standard, bits 0..4 du status register contiennent l'index SAT du **dernier sprite scanné**, mis à jour ligne par ligne. Si bit 6 (5S) est latché, c'est l'index du 5e sprite. Sinon, c'est l'index du dernier sprite traité (souvent le terminator $D0 ou le sprite 31).

### Ce que fait POM1

`scanSpritesForStatus()` (`TMS9918.cpp:467-484`) ne met à jour les bits 0..4 **que** quand bit 6 est latché :
```cpp
if (visible == 4) {
    if (!fiveAlreadyLatched) {
        statusOut = (statusOut & 0xE0) | (uint8_t)(i & 0x1F);
        statusOut |= 0x40;
        ...
    }
}
```
Sans overflow, les bits 0..4 restent à 0.

### Impact

Du code qui lit le status sans vérifier bit 6 et utilise les bits 0..4 comme "compteur sprites actifs" verra **0 en POM1** vs **valeur dynamique en silicium**. Divergence directe.

### Recommandation

**Toujours masquer avec bit 6 avant d'interpréter les bits 0..4** :
```asm
LDA VDP_CTRL
TAX             ; sauvegarde
AND #$40        ; bit 6 ?
BEQ @no_5s      ; pas d'overflow → ignorer bits 0..4
TXA
AND #$1F        ; index du 5e sprite
@no_5s:
```

---

## 8. BUG N°7 — Sprite engine pendant blank (R1 bit 6 = 0)

### Ce que fait le silicium

Comportement non trivial — la datasheet est ambigüe. Référence MSX (BiFi) suggère que le sprite engine **continue de scanner** pendant le blank (donc collision et 5S peuvent latcher) mais le compositeur vidéo n'émet rien. **À mesurer sur silicium**.

### Ce que fait POM1

`advanceCycles()` (`TMS9918.cpp:133-136`) skippe **complètement** le scan sprite quand R1 bit 6 = 0 :
```cpp
if (regs[1] & 0x40) {
    scanSpritesForStatus(vram.data(), regs.data(), statusReg);
}
```
Aucune mise à jour des bits 5/6 pendant blank.

### Impact

Du code qui blank l'affichage (`R1 &= ~$40`) puis attend un latch collision via status **ne verra jamais le bit 5 sur POM1**. Sur silicium, à confirmer.

### TODO test silicium

Programme court : afficher 2 sprites superposés, blanker l'écran, lire status après une frame. Si bit 5 est latché → silicium scanne pendant blank → POM1 diverge.

---

## 9. BUG N°8 — Sprite Cloning sous modes hybrides illégaux

### Ce que fait le silicium

Le TMS9918 expose 8 combinaisons valides des bits M1/M2/M3 (4 modes documentés + 4 réservés/non-utilisés). Quand le code force des **combinaisons illégales** (M1+M2 actifs simultanément, ou M3 + flags d'interférence), le silicium ne signale aucune erreur mais entre dans une zone de **comportement chaotique reproductible** :

- L'**axe Y** des sprites d'index 8 à 31 devient pollué par les bits faibles de l'offset SPGT (R6) et les bits 5-6 de la Color Table (R3).
- Conséquence visible : **clones verticaux fantômes** des sprites originaux apparaissent dans la strate haute de l'écran (Y = 0..63), formant un motif d'écho en tuiles déformées.
- Ces clones **consomment des slots** dans la limite 4-sprites-par-scanline et **déclenchent le bit C (collision)** comme des sprites légitimes.
- La démo MSX **"Alankomaat"** (groupe Bandwagon) exploite volontairement cet effet pour afficher un nombre de sprites impossible.

### Dérive thermique (cas limite)

Sur silicium TMS9918A original (TI / NMOS), les clones **s'estompent progressivement quand la puce chauffe** (effet "sèche-cheveux" documenté). Les clones de l'index Block 1 disparaissent en premier, puis Block 2. Les clones produits par Toshiba ou les Yamaha V9938 ont les chemins d'adressage corrigés en usine — **aucun cloning, le code qui le sollicite ne marche pas**.

### Ce que fait POM1

Le dispatcher de mode dans `TMS9918.cpp:163-191` ne traite que les combinaisons légales et tombe sur "backdrop only" pour tout le reste :
```cpp
// else: undefined mode combination — backdrop only
```
**Aucune simulation du sprite cloning.** Code qui exploite ce hack marche en théorie sur silicium, écran noir sur POM1.

### Impact

Très peu probable que Galaga utilise ce hack. À garder en tête uniquement si tu portes une démo avancée ou si tu vois des sprites apparaître "de nulle part" sur silicium dans la zone Y=0..63 et que tu utilises des combinaisons de bits R0/R1 inhabituelles.

### Recommandation

**Ne pas exploiter ce comportement** pour du code portable. Toujours rester sur les 4 modes documentés (Mode 0, 1, 2, 3). Si tu veux dépasser les 32 sprites, utiliser le **multiplexage classique par swap SAT au VBlank** (cf. flicker authentique).

---

## 10. BUG N°9 — Flip-flop d'écriture du port contrôle vs IRQ

### Ce que fait le silicium

L'écriture d'un registre VDP via `$CC01` est en **deux octets** : le 1er = data, le 2e = `$80 | regnum`. Le VDP utilise un **flip-flop interne** (latch) pour suivre l'état (1er ou 2e octet attendu).

Si une **interruption matérielle** survient entre les deux octets ET que la routine d'IRQ écrit elle aussi sur `$CC01`, le flip-flop se désynchronise → le 1er octet de la routine principale est interprété comme un 2e octet de la routine IRQ → **corruption de registre VDP**.

**Solution silicium documentée** : la routine IRQ doit **lire `$CC01` (status register) en premier**, ce qui RESET le flip-flop de manière atomique. Toute écriture suivante repart proprement avec un 1er octet.

### Ce que fait POM1

POM1 simule correctement ce reset : `readControl()` (`TMS9918.cpp:115-122`) fait `latchIsSecond = false`, et `writeData()` / `readData()` (`TMS9918.cpp:60-76`) idem. **POM1 est fidèle ici.**

### Impact

Aucune divergence comportementale : un code qui suit la convention "lire status au début de la routine IRQ" marche sur les deux cibles. Mais un code qui **omet** cette lecture sera buggué sur les **deux** cibles dès qu'une IRQ tape entre les 2 octets.

### Recommandation

**Convention obligatoire pour toute routine IRQ qui touche au VDP** :
```asm
vdp_irq_handler:
    PHA               ; sauve A
    LDA VDP_CTRL      ; lecture status — RESET flip-flop ATOMIQUE
                      ;                  ET clear bit 7 frame flag
    ; ... traitement IRQ, écritures VDP libres ...
    PLA
    RTI
```

Note : sur Apple-1 + TMS9918, **la ligne /INT n'est pas câblée** (Bug N°2), donc cette IRQ ne se produit pas. Le bug est dormant. Mais si un futur portage TMS9918 connecte /INT au /IRQ 6502, la convention devient critique.

---

## 11. BUG N°10 — Hack raster split via 5S (effets mid-scanline)

### Ce que fait le silicium

Le TMS9918 ne fournit **pas d'IRQ raster** standard, mais les programmeurs d'époque ont contourné cette limitation via une technique connue sous le nom de **"5S raster split"** :

1. Placer 5 sprites **invisibles** (color = 0) alignés à une coordonnée Y cible — par exemple Y = 95 (milieu d'écran).
2. Le CPU lance une **boucle d'attente serrée** sur le bit 6 du status register :
   ```asm
   @wait_split: LDA VDP_CTRL
                AND #$40
                BEQ @wait_split
   ```
3. À l'instant exact où le faisceau atteint Y = 95, le silicium détecte 5 sprites sur la scanline → **bit 6 (5S) s'arme**.
4. Le CPU sort de la boucle et peut alors :
   - Modifier R7 (couleur backdrop) → effet "rainbow / dégradé".
   - Modifier R5 (base SAT) → second jeu de 32 sprites pour la moitié basse.
   - Modifier R4 (base Pattern Table) → set graphique différent par moitié d'écran.

### Ce que fait POM1

`scanSpritesForStatus()` est appelé **une seule fois par frame au VBlank** (`TMS9918.cpp:127-139`). Le bit 6 ne s'arme qu'à `T = fin de frame`, jamais à `T = milieu de frame`. La boucle d'attente du CPU **boucle sans fin pendant 16 ms** puis voit bit 6 d'un coup à la frame suivante → l'effet visuel s'applique au mauvais moment (ou pas du tout, si le code attend que bit 6 soit clear avant de re-armer).

**Code utilisant le hack 5S split = visuel cassé sur POM1, OK sur silicium.**

### Impact

Galaga n'utilise probablement **pas** ce hack (jeu simple, pas d'effet rainbow). Mais c'est l'un des hacks les plus répandus en homebrew MSX — toute portage de démo MSX vers Apple-1 + TMS9918 buggera sur POM1 et marchera en silicium.

### Recommandation

**Ne pas utiliser ce hack tant que POM1 ne fait pas du scan scanline-by-scanline**. C'est un futur ajout possible côté émulateur (cf. section "Roadmap émulateur" ci-dessous). En attendant, pour des effets multi-section, utiliser le polling VBlank classique et accepter qu'un effet entier s'applique par frame.

---

## 12. BUG N°11 — Frame rate 59,94 Hz NTSC vs 60 Hz POM1

### Ce que fait le silicium

NTSC analogique = **59,94 Hz** exactement (60 × 1000/1001), pas 60 Hz rond. PAL = 50 Hz exact. Cette fréquence non entière entraîne, sur des routines de multiplexage SAT (cf. Bug N°6 flicker authentique), une **dérive de phase** progressive dans les inversions de groupes.

### Ce que fait POM1

`POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` dans `CpuClock.h` cale POM1 à **60 Hz fixe**. La VBlank tombe tous les 17045 cycles à 1× (1 022 727 / 60).

### Impact

Pour un jeu type Galaga qui fait du multiplexage SAT à chaque frame, la différence 59,94 vs 60 Hz est **imperceptible**. Pour des effets de précision musicale (sync audio sur VBlank) ou des démos avec timing analogique fin, **un drift cumulatif de ~0,1 % apparaît**.

### Recommandation

Ne pas se baser sur "exactement 60 frames par seconde". Compter en cycles CPU, pas en frames. Si une démo MSX d'origine tourne à 59,94, accepter un léger drift en POM1.

---

## 13. Protocole de bringup émulateur → silicium

Checklist en 6 étapes pour porter un programme POM1 vers silicium sans surprise :

1. **Mesurer la RAM utilisable réelle** — Confirmer `$0200-$1FFF` libre (devrait l'être). Tester `$2000-$3FFF` (libre seulement sans HGR). Tester `$8000-$BFFF` (libre sans microSD/CFFA1/JukeBox).

2. **Vérifier l'init VDP** — R1 doit avoir bit 7 = 1 (16K). R0/R2/R3/R4/R5/R6/R7 selon mode. Re-checker les noms de registres dans `dev/lib/tms9918/tms9918.inc`.

3. **Auditer toutes les boucles d'écriture VRAM** — Chaque paire `STA VDP_DATA` consécutive doit avoir **≥ 8 cycles** entre les writes pendant l'affichage actif. Préférer `STA $CC00,X` ou macro `WRT_DATA_NOP`. Boucles d'init VRAM en début de programme peuvent ignorer cette règle si l'affichage est blanké (`R1 bit 6 = 0`) pendant le chargement.

4. **Pas de dépendance à /INT** — Toujours poller bit 7 du status register. Pas de `CLI` + handler IRQ pour le VBlank.

5. **Pas de collision en overscan** — Bounding-box software pour les sprites hors écran.

6. **Test progressif** — texte uniquement → bitmap statique → 1 sprite immobile → SAT animée → SAT pleine 60 Hz. À chaque étape, valider sur silicium avant d'ajouter de la complexité.

---

## 14. Tests reproductibles à exécuter sur les deux cibles

Suite minimale pour caractériser un nouveau setup silicium. Tous les programmes sont à charger via Wozmon et lancer manuellement.

### Test A — Boucle SAT serrée (doit échouer en silicium)

Écrit 32 entrées SAT sans NOP :
```asm
;     LDA #$00 / STA $CC01 / LDA #$5B / STA $CC01     ; addr=$1B00 write
; @lp LDA #$50 / STA $CC00 / LDA #$00 / STA $CC00 / STA $CC00 / STA $CC00 / DEX / BNE @lp
```
**Attendu silicium** : SAT corrompue, sprites visibles à des positions aléatoires.
**Attendu POM1** : 32 sprites alignés à Y=$50, X=$00, name=$00.

### Test B — Même boucle avec NOPs (doit passer partout)

Idem Test A en intercalant `NOP / NOP` entre chaque `STA $CC00`.
**Attendu** : identique sur les deux cibles.

### Test C — Sprite à Y=$D0 (terminator)

SAT[0] = (Y=$50, X=$00, name=1, color=$0F), SAT[1] = (Y=$D0, ...), SAT[2] = (Y=$50, X=$20, name=1, color=$0F). Sprite #2 ne doit **pas** apparaître.
**Attendu** : 1 seul sprite visible (le slot 0). Identique sur les deux cibles si silicium OK.

### Test D — 5S overflow

5 sprites alignés Y=50, X=0,16,32,48,64. Lire status après VBlank.
**Attendu** : bit 6 set, low 5 bits = 4. Identique sur les deux cibles.

### Test E — Collision overscan (différence attendue)

2 sprites avec early-clock (color bit 7 = 1), X=10 → réel X=-22, Y=50. Pattern solid. Lire status après VBlank.
**Attendu silicium** : bit 5 set (collision détectée hors écran).
**Attendu POM1** : bit 5 = 0 (collision clippée).

Ce test sert à valider le bug N°4 sur ta cible silicium — utile pour calibrer.

---

## 15. Checklist défensive (carte de référence à imprimer)

À garder sous la main pour tout nouveau code TMS9918 destiné au silicium :

- [ ] `R1` initialisé avec **bit 7 = 1** (= 16K mode). Valeur typique : `$E0` ou `$F0`.
- [ ] **≥ 8 cycles entre 2× accès VRAM** en Mode Graphic + sprites actifs (NOPs, indexed addressing, ou loop avec 6+ cycles d'instructions intercalées).
- [ ] Pour les uploads massifs (init Pattern/Name/Color Table) : **blanker l'affichage** (`R1` bit 6 = 0) pendant l'upload puis le ré-activer.
- [ ] VBlank attendu via `LDA $CC01 ; BPL ...` (polling), **pas** via IRQ.
- [ ] Pas de `WAI` ni de `CLI` + handler IRQ pour synchronisation VBlank.
- [ ] **UNE SEULE lecture du status par frame** (la lecture clear bits 5, 6 et 7 atomiquement).
- [ ] Toute routine IRQ qui touche au VDP doit **lire status en premier** (reset flip-flop 1er/2e octet).
- [ ] Collision testée mais **jamais en zone overscan** (utiliser bounding-box software hors écran).
- [ ] **Terminator `$D0`** posé après le dernier slot SAT actif (sinon 32 sprites scannés à chaque frame, perf et 5S spurieux).
- [ ] Avant de masquer bits 0..4 du status comme "index 5e sprite", vérifier que **bit 6 est set** (sinon valeur indéterminée).
- [ ] Ne pas exploiter le **hack raster split via 5S** (Bug N°10) — visuel cassé sur POM1.
- [ ] Ne pas exploiter le **sprite cloning** des modes hybrides illégaux (Bug N°8) — non simulé sur POM1, refusé par les clones Toshiba/Yamaha.
- [ ] Modes documentés uniquement : Mode 0 (Graphic I), Mode 1 (Text), Mode 2 (Graphic II / Bitmap), Mode 3 (Multicolor). **Aucune combinaison hybride.**

---

## Annexe A — Référence rapide registres TMS9918

| Reg | Bits | Rôle |
|---|---|---|
| R0 | bit 1 = M3 (Mode bit 3) ; bit 0 = External VDP (inutilisé) | Sélection mode |
| R1 | bit 7 = **16K** ; bit 6 = Display ON ; bit 5 = IRQ Enable ; bit 4 = M1 ; bit 3 = M2 ; bit 1 = sprites 16×16 ; bit 0 = sprites ×2 mag | Mode + display |
| R2 | bits 3-0 = Name Table base × $400 | Adresse Name Table |
| R3 | Color Table base (Mode 0 : ×$40 ; Mode 2 : bit 7 + mask) | Adresse Color Table |
| R4 | bits 2-0 = Pattern Table base × $800 (Mode 0) ; bit 2 + mask (Mode 2) | Adresse Pattern Table |
| R5 | bits 6-0 = Sprite Attr Table base × $80 | Adresse SAT |
| R6 | bits 2-0 = Sprite Pattern Table base × $800 | Adresse Sprite Patterns |
| R7 | bits 7-4 = FG (text) ; bits 3-0 = BG / Backdrop | Couleurs |

Status register (lecture `$CC01`, **destructif** : clear F/5S/C à chaque lecture) :

| Bit | Nom | Rôle |
|---|---|---|
| 7 | F | Frame flag — set en début de VBlank, déclenche /INT si R1 bit 5=1 |
| 6 | 5S | Cinquième sprite overflow — set quand >4 sprites sur une scanline |
| 5 | C | Coïncidence/collision — set quand 2 sprites se chevauchent (bit-pattern, color=0 inclus) |
| 4-0 | 5S index | Index SAT du **premier** 5e sprite identifié — valide **seulement si bit 6=1** |

Modes graphiques **documentés** (M1, M2, M3) :

| M1 | M2 | M3 | Mode | Description | Sprites |
|---|---|---|---|---|---|
| 0 | 0 | 0 | **Mode 0** Graphic I | 32×24 tiles, 32 color groups (8 patterns/group) | Oui |
| 0 | 0 | 1 | **Mode 2** Graphic II | Bitmap full 256×192, color/8 pixels | Oui |
| 1 | 0 | 0 | **Mode 1** Text | 40×24 chars, glyph 6×8, FG/BG via R7 | **Non** |
| 0 | 1 | 0 | **Mode 3** Multicolor | 64×48 blocs colorés | Oui |

Toute autre combinaison (M1+M2, M2+M3, M1+M3, M1+M2+M3) = **mode hybride illégal**, comportement chaotique non émulé par POM1 (cf. Bug N°8 sprite cloning).

## Annexe B — Timing pixel et scanline

| Paramètre | Valeur | Notes |
|---|---|---|
| Pixel clock | ~5,37 MHz (NTSC) | Cycle pixel = 186 ns |
| Cycle mémoire VDP | ~372 ns (= 2 cycles pixel) | Unité de fenêtre d'accès CPU |
| Scanline complète | 342 cycles pixel | Total horizontal |
| ↳ zone active visible | 256 cycles pixel | 256 px d'affichage |
| ↳ marges + sync | ~28 cycles pixel | Bordures gauche/droite |
| ↳ HBLANK | ~58 cycles pixel | Suppression horizontale |
| Frame complète | 262 scanlines (NTSC) / 313 (PAL) | |
| ↳ visibles | 192 scanlines | Active display |
| ↳ VBLANK | ~70 lignes (NTSC) / ~121 (PAL) | ~4,3 ms NTSC |
| Frame rate exact | 59,94 Hz (NTSC) / 50 Hz (PAL) | POM1 = 60 Hz fixe (cf. Bug N°11) |

## Annexe C — Roadmap émulateur (futurs ajouts POM1)

Améliorations qui **rapprocheraient POM1 du silicium** mais ne sont pas implémentées :

1. ~~**Mode "silicon timing strict"** — option qui drop des octets si écriture VRAM trop rapide pour le mode actif (perfectionne Bug N°1).~~ **✅ IMPLÉMENTÉ** : `TMS9918::siliconStrictMode` drop l'octet (`canAcceptAccess()` dans `TMS9918.cpp:98-101`) avec les fenêtres exactes de la table §2 (8c en Mode I+sprites, 4c en multicolor, 3c en text, 2c en blank/VBlank). Activé par défaut pour tous les presets sauf les Multiplexing Fantasy. Toggle runtime exposé via Hardware menu → "Silicon Strict (TMS9918 timing)" et CLI `--silicon-strict` / `--no-silicon-strict`. Status bar `STRICT` / `FANTASY`. Persiste dans le snapshot via `kFlagSiliconStrict` bit 14. Test : `tests/tms9918_silicon_strict_runtime_test.cpp`.
2. **Scan sprite scanline-by-scanline** — appel de `scanSpritesForStatus()` en cours de frame, pas seulement au VBlank (résout Bugs N°5 et N°10).
3. **Frame rate 59,94 Hz** — option NTSC exact (résout Bug N°11).
4. **Sprite cloning émulé** — détection des modes hybrides illégaux et reproduction du cross-talk d'adressage (résout Bug N°8 partiellement, sans la dérive thermique).
5. **Câblage /INT → /IRQ 6502** — option dans le preset pour propager l'IRQ matérielle (résout Bug N°2).
6. **R1 bit 7 (4K/16K)** strict — option qui mask l'adressage VRAM à 12 bits si bit 7=0 (résout Bug N°3 par detection précoce).

Ces ajouts ne sont pas planifiés à court terme — ce doc sert d'abord à **éviter** ces pièges côté code utilisateur.

---

## 16. Annexe D — Sous-bug du Bug N°1 : `statusReg` bit 7 sticky comme proxy VBlank (corrigé)

### Symptôme
Avant 2026-04-30, lancer Galaga via CodeTank en `Silicon Strict` ON ne déclenchait
**aucun** drop d'octet alors que les patterns `STA $CC00` à 4-6 cycles d'intervalle
sont visiblement KO sur silicium réel (cf. Bug N°1).

### Cause racine
`TMS9918::requiredAccessCycles()` (TMS9918.cpp:74-77) utilisait
`(statusReg & 0x80) != 0` comme proxy *« on est en VBlank »* pour relâcher la
fenêtre d'accès à 2 cycles. Mais ce bit est **sticky-until-`readControl`** :
il s'arme à chaque VBlank et reste latché jusqu'à un `LDA $CC01`. Galaga ne lit
**jamais** `$CC01` (0 occurrence dans `dev/projects/tms9918_galaga/TMS_Galaga.asm`)
→ dès la 1re frame le bit reste à 1 pour de bon → `requiredAccessCycles()`
retourne 2 cycles toute la frame → tous les writes Galaga passent.

### Fix (TMS9918.cpp + TMS9918.h)
La fenêtre dépend de la position physique du beam, pas du flag latché.
Remplacé par :

```cpp
if ((regs[1] & 0x40) == 0 || frameCycleCounter >= kActiveDisplayCycles) return 2;
```

avec `kActiveDisplayCycles = (kCyclesPerFrame * 192) / 262` (≈ 12 490 cycles à
1× — 192 scanlines actives sur les 262 du frame NTSC). Pendant l'affichage actif
→ fenêtre 8c stricte, pendant VBlank physique → fenêtre 2c relaxée.

### Conséquence pour le code utilisateur
**Aucune** : la nouvelle gating reflète mieux le silicium et n'introduit pas de
faux positifs. Les programmes qui pollaient `$CC01` par discipline continuent
de fonctionner exactement pareil (la fenêtre n'a jamais dépendu du bit 7 sur
silicium).

---

## 17. Annexe E — Case study : porter Galaga en silicon-strict

`dev/projects/tms9918_galaga/TMS_Galaga.asm` est la première référence de jeu
TMS9918 portée intégralement sous `Silicon Strict` ON. Dossier de bringup
réutilisable pour les autres jeux du repo (Sokoban, Snake, Connect4, Maze3D).

### Outillage de patching

Script réutilisable : **`tools/silicon_strict_patch.py`**. Idempotent
(re-running strippe ses propres NOPs marqués puis les ré-insère).

```bash
# Patch in place
python3 tools/silicon_strict_patch.py path/to/Game.asm

# Dry-run (compte sans écrire)
python3 tools/silicon_strict_patch.py path/to/Game.asm --dry-run

# Strip-only (revert sans réinsertion)
python3 tools/silicon_strict_patch.py path/to/Game.asm --unpatch
```

Règles appliquées (cumulatives, ordre déterministe) :

| Cas | Pattern détecté | NOPs ajoutés |
|---|---|---|
| **A** | `ST? VDP_*` adjacent à `ST? VDP_*` | 2 entre |
| **B** | `ST? VDP_* / LDA #imm / ST? VDP_*` | 1 entre LDA et 2e ST? |
| **C** | `ST? VDP_* / LDA <zp/abs/zp,X> / ST? VDP_*` | 1 idem |

`ST?` couvre `STA` / `STX` / `STY` (Galaga utilise les trois).
Cross-port (`VDP_DATA → VDP_CTRL` ou inverse) : la fenêtre est unique pour
les deux ports — le matcher couvre `VDP_(DATA|CTRL)` indistinctement.

**Skip annotation** : pour exclure une routine déjà optimisée à la main
(ou qui blanke explicitement l'affichage avant ses uploads), insérer un
commentaire `; SILICON_STRICT_SKIP` n'importe où dans le corps de la
fonction (avant le `RTS` ou le label suivant). Le tool saute toute la
routine en bloc.

### Inventaire des projets patchés (état au 2026-04-30)

Tous les programmes TMS9918 du repo + les drivers `lib/tms9918/` sont
passés sous le tool. Compte des NOPs insérés :

| Projet | Cas A | Cas B | Cas C | Total |
|---|--:|--:|--:|--:|
| `tms9918_galaga` (refactor `hide_slot_4` inclus) | 62 | 193 | 14 | **269** |
| `tms9918_sokoban` | 2 | 16 | 0 | 18 |
| `tms9918_snake` | 2 | 14 | 1 | 17 |
| `tms9918_orbital_pool` | 0 | 9 | 1 | 10 |
| `tms9918_connect4` | 2 | 7 | 0 | 9 |
| `tms9918_logo` (Mode 2) | 0 | 7 | 0 | 7 |
| `tms9918_life` | 0 | 6 | 0 | 6 |
| `tms9918_maze3d` | 0 | 3 | 1 | 4 |
| `tms9918_chess` | 0 | 1 | 0 | 1 |
| `tms9918_codetank_menu` | 0 | 0 | 0 | 0 |
| `tms9918_codetank_menu_upper` | 0 | 0 | 0 | 0 |
| `tms9918_tetris_loader` | 0 | 0 | 0 | 0 |
| `lib/tms9918/tms9918m1.asm` | 0 | 3 | 1 | 4 |
| `lib/tms9918/tms9918m2.asm` | 0 | 5 | 1 | 6 |
| **Total** | **68** | **264** | **19** | **351** |

### Refactor d'économie de bytes : `hide_slot_4`

Galaga avait 10 occurrences inline du pattern « cacher un slot SAT » (16 B
chacun = 160 B total) :

```asm
LDA #HIDDEN_Y
STA VDP_DATA
LDA #$00
STA VDP_DATA
STA VDP_DATA
STA VDP_DATA
```

Refactorisé en `hide_slot_4` (helper 22 B avec NOP padding intégré) + 10×
`JSR hide_slot_4` (3 B chacun). Économie nette ≈ 100 B sur le slot ROM,
indispensable pour faire rentrer la couverture NOP complète.

### Bridges `LDA <zp 3c>` cachés

Sites typiques manqués si on ne couvre pas le case C :

| Site asm | Pattern | Symptôme silicon-strict |
|---|---|---|
| `render_sprites @show_p` | `STA VDP_DATA / LDA player_x / STA VDP_DATA` | Player ship clignote 1/2 frame |
| `plot_star` final write | `STA VDP_CTRL / LDA temp3 / STA VDP_DATA` | Étoile non plottée → starfield clairsemé |
| `render_sprites @en_paint X` | `STA VDP_DATA / LDA enemy_x / STA VDP_DATA` | Enemy clignote (mais `enemy_x,X` 4c → OK) |

`player_x`, `temp3` etc. sont en zero page (`.res 1` dans le ZEROPAGE segment).
`LDA zp` = 3 cycles → gap 4+3+4 = **11c** depuis le début du précédent STA, soit
**7c** entre les deux *latches* VDP. Ajouter 1 NOP (2c) suffit pour atteindre 9c.

### Bridges `STA VDP_CTRL / STX VDP_CTRL` (address-write 2-byte)

`draw_str_tms` (et clones) faisait `STA VDP_CTRL / STX VDP_CTRL` direct (gap 4c).
Le matcher initial ne ciblait que `STA VDP_CTRL` — `STX` et `STY` étaient
ignorés → 2e moitié de l'address-write droppée → **toutes** les strings écrites
par `draw_str_tms` finissaient à un offset VRAM aléatoire (texte splatté).
Résolu en élargissant le matcher à `ST[AXY] VDP_(DATA|CTRL)`.

### Slot ROM : pourquoi le layout `dualslot8k`

La couverture NOP exhaustive sur Galaga = **219 NOPs** = 219 octets ajoutés.
Le layout menu-bank historique (`build_codetank_rom.py --layout=menu`) ne
réservait que **7 424 B** au slot Galaga (`$4100-$5DFF`) — Galaga avec patches
faisait 7 419 B → 5 B de marge, pas tenable une fois qu'on couvre tous les cas.

Solution : layout `--layout=dualslot8k`, qui offre **8 192 B** par slot et
sacrifie le menu interactif + Snake/Life :

```
Lower bank ($4000-$7FFF) :
  $4000-$5FFF  Galaga  (8 kB, 760 B padding avec patch full)
  $6000-$7FFF  Sokoban (8 kB, 3 410 B padding)
Upper bank :
  Tetris launcher + payload (inchangé)
```

Pas de menu — Wozmon `4000R` lance Galaga, `6000R` lance Sokoban. ROM publiée
sous `roms/codetank/Codetank_GAME1.rom`.

### Builder

```bash
python3 tools/build_codetank_rom.py --layout=dualslot8k -o roms/codetank/Codetank_GAME1.rom
```

Cfgs : `apple1_galaga_codetank_8k.cfg` (link à `$4000`, slot 8 kB) et
`apple1_sokoban_codetank_8k.cfg` (link à `$6000`).

### Validation visuelle

POM1 `--preset 8 --terminal --silicon-strict`, `4000R`, choisir QWERTY :
- Page de garde `A1GALAGA / APPLE-1 TMS9918 / BY VERHILLE ARNAUD` complète.
- 3 alien sprites SCOUT/FIGHTER/BOSS avec labels HP.
- Menu keyboard `1 QWERTY (A D S) / 2 AZERTY (Q D S) / SPACE FIRE` propre.
- Gameplay : HUD `SCORE/LIVES/W:01`, starfield 6-8 étoiles défilant en douceur,
  ship player + ennemis sans clignotement.

Screenshots de référence dans `screenshots/pom1_latest.png` (capturé via
TerminalCard ESC S après `--terminal`).

---

*Dernière mise à jour : 2026-04-30. Bug N°1 (timing) confirmé en silicium via
Galaga. Bug du sous-§16 corrigé dans POM1. Annexe E ajoutée avec le bringup
Galaga complet (toolchain + cfgs `dualslot8k`). Bugs N°2 à N°11 issus de
l'analyse statique de `TMS9918.cpp` croisée avec les références TI / Texas
Instruments / BiFi MSX / openMSX — à valider sur silicium au cas par cas
(Tests A à E ci-dessus).*
