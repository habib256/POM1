# Sprites TMS9918A — architecture, émulation et pratiques (POM1)

Document technique : sous-système de sprites du VDP TMS9918A dans le contexte de l’émulation Apple‑1 et du projet **POM1** (moteur C++, précision cyclique, module `TMS9918.cpp`).

---

## 1. Contexte : POM1 et précision d’émulation

La préservation des micro-ordinateurs historiques impose une fidélité croissante au matériel. L’**Apple‑1** (Steve Wozniak) est une architecture fondatrice ; le projet **POM1** (GitHub, compte habib256) en est une référence courante.

Après une phase Java, POM1 a été réécrit en **C++ moderne** (≥ 1.8.6), notamment pour lever des goulots d’étranglement et permettre une archéologie logicielle sérieuse. Le cœur **M6502** modélise les accès mémoire et les pénalités de cycles ; l’horloge CPU est dérivée de la sous-porteuse couleur NTSC : **14,31818 MHz ÷ 14 ≈ 1,022727 MHz**, ce qui stabilise les boucles de synchro en assembleur.

Parmi les extensions émulées figure la **carte graphique P‑LAB Apple‑1** avec le VDP **TMS9918A** : 256×192, 16 couleurs utiles, **sprites matériels**. Leur rendu correct est délicat : *ghosting*, clignotement, sprites invisibles viennent souvent d’une lecture partielle des specs, d’une désynchro CPU/VDP ou des particularités du silicium TI.

---

## 2. Architecture du TMS9918A

### 2.1 VRAM isolée

Contrairement à l’Apple II ou au C64, le TMS9918A utilise une **VRAM dédiée** (~16 Ko), sur un bus séparé. Le 6502 n’y accède que via les ports du VDP (adresse puis données séquentielles).

Pendant l’**affichage actif**, le VDP monopolise la VRAM ; les fenêtres CPU sont rares (en pratique, accès typiquement espacés — l’émulateur doit modéliser la **contention** si l’objectif est la précision au cycle).

### 2.2 Composition en « plans »

L’image n’est pas un simple framebuffer plat : le VDP **superpose** plusieurs couches lors du balayage :

1. Entrée vidéo externe (souvent inutilisée)
2. Couleur de fond (*backdrop*)
3. Plan des motifs (tuiles / texte)
4. **32 plans de sprites** — un par entrée de la SAT

**Priorité** : le sprite d’**index 0** est au-dessus, le sprite **31** en dessous. Les pixels non transparents des indices faibles masquent ceux des indices élevés (*z-order* matériel).

### 2.3 Registres de contrôle (VR0–VR7, écriture seule)

Le CPU **ne peut pas lire** VR0–VR7 : pas de read-modify-write matériel ; les pratiques MSX / bibliothèques modernes utilisent une **copie logicielle** (*shadow*) en RAM hôte.

| Registre | Rôle utile pour les sprites |
|----------|-----------------------------|
| **VR0** | Mode d’affichage global, entrée vidéo externe |
| **VR1** | Bit **1** (0x02) : taille de base **0 = 8×8**, **1 = 16×16**. Bit **0** (0x01) : **grossissement** (chaque pixel sprite → bloc 2×2) |
| **VR5** | Base de la **SAT** en VRAM (partie haute de l’adresse 14 bits) |
| **VR6** | Base de la **table des motifs sprites** (SPGT) : valeur × **2048** (0x0800) |
| **VR7** | Couleur de fond (*backdrop*) et (en mode texte) couleur du texte |

L’émulateur doit appliquer masques et offsets de façon cohérente sur tout le pipeline de rendu.

---

## 3. Tables VRAM : SPGT et SAT

### 3.1 SPGT (motifs des sprites)

- Un sprite = **une couleur** + transparence sur le reste.
- **8×8** (VR1 bit 1 = 0) : **8 octets** par motif → jusqu’à **256** motifs dans 2048 octets.
- **16×16** (VR1 bit 1 = 1) : **32 octets** par motif (quatre blocs 8×8). L’index dans la SAT doit être un **multiple de 4** : les **2 bits bas** du numéro de motif sont **ignorés** par le matériel. Une émulation qui oublie ce masque affiche mal la moitié droite / basse du sprite.

### 3.2 SAT (Sprite Attribute Table)

**128 octets** = **32 × 4 octets**. Le bloc *i* correspond au **sprite i** (0 = priorité max).

| Octet | Champ | Rôle |
|-------|--------|------|
| **0** | **Y** | Position verticale avec décalage **−1** vs la ligne écran ; sert aussi au **terminateur** (voir § 4) |
| **1** | **X** | 0…255 : bord gauche du sprite |
| **2** | **Motif** | Index 0…255 dans la SPGT |
| **3** | **Couleur + EC** | Bits **0–3** : couleur ( **0 = transparent** ). Bit **7** : **Early Clock**. Bits **4–6** : ignorés |

Une mauvaise interprétation de ces octets explique une grande partie des bugs « invisibles » ou « fantômes ».

---

## 4. Diagnostic I — Sprites invisibles et terminateur **0xD0**

### 4.1 Décalage vertical (Y−1)

- **Y = 0** : la première ligne du sprite est sur la **ligne 1** de l’écran (index 1).
- **Y = 0xFF** : avec −1, la première ligne est en **ligne −1** (hors écran) ; le corps peut entrer par le **haut** — usage classique pour faire glisser un sprite depuis le bord supérieur.
- **Y = 0xFE** : les deux premières lignes du sprite sont invisibles en tête.

Au-delà d’environ **0xBE (190)** selon la taille, le sprite peut sortir du cadre **192** lignes ; l’arithmétique doit rester **sûre** pour le framebuffer (wrap / clipping), sinon risque de corruption ou crash si on indexe naïvement.

### 4.2 Terminateur matériel : **Y = 0xD0 (208)**

Pendant le **HBLANK**, le VDP parcourt la SAT pour la ligne suivante. Pour ne pas lire systématiquement 128 octets, TI a défini un **arrêt anticipé** :

> Si le champ **Y** d’une entrée vaut **0xD0**, le VDP **arrête** le scan de la SAT. Cette entrée n’est **pas** dessinée ; **toutes les entrées suivantes** (indices plus élevés) sont **ignorées**, quelles que soient leurs coordonnées.

**Conséquences :**

1. **Émulation** : une boucle `for (i = 0; i < 32; i++)` **sans** test `if (y == 0xD0) break` peut lire au-delà du marqueur et afficher des **résidus** (*fantômes*) alors qu’un jeu MSX a écrit **0xD0** pour masquer une série de sprites.
2. **Programme / VRAM non initialisée** : au reset, la VRAM peut contenir n’importe quoi. Si un **Y** aléatoire vaut **0xD0** parmi les premières entrées, le matériel (ou l’émulateur fidèle) **n’affiche aucun sprite** — symptôme fréquent « tout marche sauf les sprites ».

**Bonnes pratiques** : avant la première image utile, initialiser la zone SAT (ou toute la VRAM) de façon à **ne pas** placer **0xD0** en Y tant qu’on ne veut pas couper la liste — par exemple remplir les Y avec une valeur **≥ 0xD1** ou hors zone visible selon la stratégie du moteur (cf. bibliothèques type *apple1-videocard-lib*).

---

## 5. Diagnostic II — « Sprites fantômes » et **Early Clock**

### 5.1 Rôle du bit 7 (EC) de l’octet couleur

**X** est sur 8 bits (0…255). Pour faire **entrer** un sprite par la **gauche** sans coordonnée négative, le bit **Early Clock** (bit **7** = **0x80**) décale le dessin de **32 pixels vers la gauche** par rapport au **X** stocké : position effective pensée comme **X − 32** (avec clipping au bord gauche).

### 5.2 Piège émulateur : sous-flow **uint8_t**

En C++, un schéma du type :

```cpp
uint8_t physical_x = sprite_x - 32;  // DANGER si EC actif
```

Si `sprite_x == 10` et EC = 1, **10 − 32** en non signé 8 bits donne **234** : le sprite **réapparaît à droite** — effet « fantôme » classique. Il faut calculer en **signé élargi** :

```cpp
int px = static_cast<int>(sprite_x) - (ec ? 32 : 0);  // puis clip sur [0, largeur)
```

### 5.3 Piège programmeur : couleur > 15

Seuls les **4 bits bas** portent la couleur. Écrire par exemple **143** (128 + 15) active **bit 7** → **EC** involontaire → décalage de 32 px. D’où l’intérêt d’API du type `tms_set_sprite` qui **masque** la couleur correctement.

---

## 6. Diagnostic III — Limite **4 sprites par ligne** et multiplexage

### 6.1 Contrainte matérielle

Pendant le **HBLANK**, le temps d’accès VRAM est fini : le VDP ne peut préparer que **4 sprites** par **ligne de balayage**. Les sprites sont examinés dans l’**ordre SAT (0 → 31)** ; au **5e** sprite intersectant la ligne, le matériel **s’arrête** pour cette ligne (les suivants ne sont pas dessinés pour cette ligne).

### 6.2 Multiplexage (scène MSX / démos avancées)

Pour donner l’illusion de plus de 4 sprites sur une ligne, les jeux **rotent** l’ordre en VRAM (souvent en **VBLANK**) : frames alternées affichent des ensembles différents ; sur **CRT** à persistance, l’œil fusionne ; sur **LCD** le résultat peut ressembler à un **clignotement** fort — parfois appelé « ghosting » par les utilisateurs.

### 6.3 Ne pas « corriger » l’émulateur en affichant 32 sprites / ligne

Retirer la limite des 4 sprites **casse** la compatibilité : le **bit 5S** du statut et l’**index du 5e sprite** (bits 0–4) ne reflètent plus le matériel ; les routines qui **pollent** le statut pour décider quelle entrée SAT déplacer se comportent alors de façon erratique.

### 6.4 Padding des écritures pendant l’affichage actif (mode strict POM1)

Sous **mode strict POM1** (modèle slot-table porté d’openMSX, `TMS9918.cpp`), les fenêtres d’accès CPU en mode Gfx12 actif sont cadencées à **~19 slots / ligne**, soit environ un slot toutes les 12 cycles CPU. Une boucle assembleur dense (`LDA #imm` + `STA $CC00` + `DEX` + `BNE` ≈ 9–11 cycles par octet) **dépasse** ce débit : POM1 émet alors une trace `[TMS9918 DROP]` sur chaque écriture hors créneau — exactement comme le silicium TMS9918A laisse tomber l’octet quand l’automate n’est pas prêt.

Signature typique dans le log POM1 :

```
[TMS9918 DROP #N] D val=FF vramAddr=11xx drain=1c..4c linePos=Y nextSlot=Z tbl=Gfx12 vblank=0 R1=C0 PC=$xxxx
```

`linePos < nextSlot` : le CPU écrit alors que la chip est encore occupée et que le prochain créneau est devant. Trois remèdes, classés du plus simple au plus structuré :

1. **`vdp_display_off` autour de la boucle.** Désactiver le bit *blank* de VR1 (`REG1_BLANK_MASK = 0x40`) avant le burst, le rétablir après. En blank, POM1 bascule sur la table `slotsMsx1ScreenOff` (~107 slots / ligne) : la contention disparaît. C’est exactement la stratégie adoptée par les setups SAT de TMS_SilBench (tests T14 / T15) — l’étendre aux setups SPGT (motifs sprites) et à toute init de table volumineuse.

2. **`BIT $CC01` (ou `LDA $CC01`) entre chaque écriture.** Macro `TMS_IO_DELAY()` côté nippur72 (`dev/apple1-videocard-lib/lib/utils.h:14`). Coût : 4 cycles CPU + un accès bus qui passe par la machine à slots du VDP, donc « cale » la boucle sur la cadence du chip. Effet collatéral : **efface F/5S/C et reset le flip-flop** (cf. *Bug N°9* dans `dev/SILICONBUGS.md`). À éviter quand le code surveille le statut, sûr partout ailleurs.

3. **Burst chunké en VBLANK.** Pattern complet : `tms_wait_end_of_frame()` puis ≤ 128 octets dans une boucle serrée, recommencer à la trame suivante au besoin. Référence : `dev/apple1-videocard-lib/lib/screen1.c:47-90` (scroll vertical), commentaire explicite sur la fenêtre strict-mode (~4 500 cycles de slots `slotsMsx1ScreenOff` disponibles par trame). Adapté aux gros transferts (scroll, refresh complet d’écran).

Diagnostic concret : aussi longtemps que des `[TMS9918 DROP]` apparaissent, appliquer un des trois remèdes au PC incriminé jusqu’à disparition des drops. La trace donne `PC=$xxxx` qui pointe directement la boucle fautive.

---

## 7. Registre de **statut** (lecture)

| Bit(s) | Nom / rôle |
|--------|------------|
| **7** | **F** — fin de trame active (VBLANK) ; peut piloter **INT\*** si activé dans VR1 |
| **6** | **5S** — « 5e sprite » sur la **ligne courante** |
| **5** | **C** — **collision** sprite–sprite (pixels non transparents superposés) |
| **4–0** | Index dans la SAT du **5e** sprite (celui qui déclenche **5S**) |

**Lecture destructive** : lire le statut **efface** typiquement **F** et **C** (selon doc / implémentation fidèle). Un polling agressif sur **F** peut **manger** un **C** récent — le jeu doit orchestrer lectures et sauvegardes.

**Collision** : le matériel signale **qu’il y a eu** chevauchement pixel, **sans** dire quels sprites. Usage courant : **C** comme **trigger**, puis détection **bounding box** (ou équivalent) en RAM programme.

---

## 8. Pratiques logicielles — *apple1-videocard-lib* (nippur72)

Trois idées récurrentes :

### 8.1 Shadow des registres VR0–VR7

Tableau du type `tms_regs_latch[]` mis à jour par un wrapper `tms_write_reg` ; jamais de RMW matériel sur les registres.

```c
// Schéma conceptuel (pratique MSX / enveloppe bibliothèque)
void tms_write_reg(uint8_t reg, uint8_t value) {
    tms_regs_latch[reg] = value;
    /* écriture matérielle vers le port TMS9918 */
}
```

### 8.2 Structure logique + `tms_set_sprite`

Séparer **X, Y, motif, couleur** dans une struct applicative, puis assembler masques + limites dans une fonction dédiée — évite EC et couleurs fantômes.

```c
tms_sprite spr;
spr.x = 100;
spr.y = 50;
spr.name = 0;
spr.color = COLOR_BLACK;
tms_set_sprite(0, &spr);
```

### 8.3 `tms_wait_end_of_frame`

Reporter les gros transferts VRAM / SAT au **VBLANK** (poll du bit **F**) limite tearing et collisions d’accès avec le raster.

---

## 9. Spécificité Apple‑1 P‑LAB : ROM **CodeTank / Juke‑Box** et RAM **$0280**

Une ROM embarquée (ex. **$4000**) peut contenir du code **KickC** avec variables globales / latchs TMS. Ces **données modifiables** doivent vivre en **RAM** (souvent **$0280+** sur Apple‑1), pas en ROM.

Si on branche une ROM et qu’on saute l’**init des données** (copie des valeurs initiales ROM → RAM), les pointeurs VRAM et shadows contiennent du **bruit** → sprites absents ou erratiques. Les toolchains documentent souvent une macro du type **`apple1_eprom_init()`** en tout début de logique utilisateur.

---

## 10. Synthèse implémentation émulateur (POM1 / `TMS9918.cpp`)

Pour coller au matériel et aux jeux qui comptent sur le statut :

1. Moteur **orienté ligne** (ou plus fin) : à chaque fin de ligne simulée, phase **HBLANK** → parcours SAT dans la **VRAM émulée**.
2. **`if (Y == 0xD0) break`** sur le scan SAT (terminateur).
3. Test d’intersection ligne courante avec **Y−1**, taille 8/16 et grossissement VR1.
4. **Buffer de ligne** : au plus **4** sprites ; au 5e intersecté, lever **5S** et renseigner l’**index**.
5. Rendu : couleur = octet 3 **& 0x0F** ; si transparent (0), ne pas peindre ; **EC** : position X en **arithmétique signée élargie** avant clip.
6. Collision **C** : si deux pixels non transparents de sprites différents même (x,y) ligne — selon modèle choisi, aligné sur le comportement documenté du VDP.

En respectant **contention**, **terminateur 0xD0**, **EC signé**, **limite 4/ ligne** et **statut + clear-on-read**, le même code assembleur tourne de façon cohérente sur **POM1** et sur silicium **TMS9918A** réel (carte P‑LAB, MSX, etc.).

---

## 11. Validation sur silicium réel (mai 2026)

Une vidéo de validation enregistrée par **Claudio Parmigiani** (designer P‑LAB) sur sa **Replica‑1 1:1** réelle, **12 mai 2026**, confirme que les comportements sprites de POM1 — corrects et fautifs — collent au silicium TMS9918A authentique :

- Les correctifs accumulés passent sur silicium : scan ligne‑par‑ligne, terminateur **0xD0**, **Early Clock** en arithmétique signée, plafond **4 / ligne** avec **5S** *sticky*, collision en arithmétique pixel hors couleur.
- Les échecs sont partagés : **Galaga**, les démos **Logo**, **Rogue**, **Mandelbrot** s’affichent de la même façon défectueuse sur silicium et sous POM1. Les correctifs doivent donc viser les sources cc65 / ASM (§ 6.4 ci‑dessus, § 9 pour l’init des données), **pas** l’émulateur.
- **/INT câblé mais jamais asservi** : à l’oscilloscope la ligne reste à +5 V solide pendant Tetris, Plasma, Galaga — non parce qu’elle est déconnectée (Parmigiani a vérifié le câblage /INT → /IRQ), mais parce que ces programmes pollent `$CC01` sans jamais démasquer l’IRQ (`CLI`), donc /INT n’est jamais tiré bas durablement. Cohérent avec § 8 (pattern de polling `LDA $CC01 / BPL`) et `dev/SILICONBUGS.md` Bug N°2.

---

## 12. Note d’horloge — Apple‑1 1:1 vs Briel Replica‑1

Les Apple‑1 d’origine et les Replicas 1:1 (basés DRAM) perdent **4 cycles sur 65** au refresh DRAM matériel : la logique de rafraîchissement « hoquette » l’horloge du 6502, soit un débit CPU effectif ≈ **959 920 Hz** au lieu des **1 022 727 Hz** nominaux. Briel’s Replica‑1 utilise du SRAM, ne déclenche aucun hoquet, et bénéficie des 65/65 cycles.

POM1 modélise actuellement le cadencement **Briel** (horloge continue). Cela n’influe pas sur le sous‑système TMS9918 lui‑même — la machine à slots du VDP est cadencée sur la sous‑porteuse couleur NTSC, indépendante du CPU. Le seul cas où l’écart se voit est dans les programmes qui mesurent le temps **depuis** le 6502 (VIACLOCK et dérivés sur 65C22).

Si un mode 1:1 venait à être ajouté (toggle préset `MachineFamily { ApplecComputer1, Replica1Briel }`), le hoquet doit s’appliquer **au CPU seulement** — les périphériques tournent sur leurs propres timebases et ne voient pas le refresh. Implémentation propre : *pacing* dans le slice loop d’`EmulationController` (4 cycles stallés tous les 65 cycles CPU), pas une mise à l’échelle globale de `POM1_CPU_CLOCK_HZ`.

Référence : fil applefritter *RAM refresh cycle details* (Antonino Porcino + Uncle Bernie). Discussion par e‑mail Parmigiani ↔ équipe POM1 du **12 mai 2026**.

---

## Références implicites dans ce document

- Projet **POM1** (émulation Apple‑1, C++).
- VDP **Texas Instruments TMS9918A** (docs d’ingénierie, comportements *silicon*).
- Bibliothèque **apple1-videocard-lib** (**nippur72** / Antonino Porcino), pratiques **MSX1**.
- Fil **applefritter** « RAM refresh cycle details » (Porcino + Uncle Bernie) — voir § 12.
- Validation silicium **Replica‑1 1:1** par **Claudio Parmigiani** (design P‑LAB), 12 mai 2026 — voir § 11.

Pour le détail des bugs silicon et tests de régression dans POM1, voir aussi `dev/SILICONBUGS.md` et les tests `tms9918_*` listés dans `CLAUDE.md`.
