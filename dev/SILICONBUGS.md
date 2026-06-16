# SILICONBUGS.md — Coder pour vrai silicium Apple-1 + TMS9918

Ce document recense les **divergences POM1 ↔ silicium** observées (ou suspectées) sur le sous-système TMS9918, en particulier le **moteur sprites**. Il est conçu comme un carnet de bord pratique : chaque bug indique ce que fait le silicium, ce que fait POM1, l'impact sur le code, et le pattern de fix.

**Voir aussi**

| Doc | Usage |
|-----|--------|
| [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) §6 | Tutorial jeu TMS9918 (polling VBlank, init sprites) |
| [`APPLE1DEV.md`](APPLE1DEV.md) §4 TMS9918 | Résumé agent + liens pad / strict |
| [`doc/CLI.md`](../doc/CLI.md) | `--silicon-strict` / `--no-silicon-strict` |
| [`CLAUDE.md`](../CLAUDE.md) *Testing* | Liste `ctest` TMS9918 (`tms9918_*`) |

L'émulation POM1 est suffisamment fidèle pour développer en text/bitmap (Modes 1 et 2). Le moteur sprites présente des écarts qui ne se voient qu'en silicium réel — ce doc les documente pour qu'on n'y retombe plus.

---

## 0. Tableau de rigueur — état de modélisation par bug

POM1 silicon-strict modélise les comportements TMS9918 documentés dans Nouspikel + openMSX. **Tous les modèles ne sont pas également vérifiables** : certains sont des ports verbatim de code de référence (très solides), d'autres sont des approximations plausibles dont le motif silicon réel reste incertain. Ce tableau donne le statut honnête de chaque bug pour informer la confiance qu'on peut accorder à POM1 comme outil de validation pré-déploiement.

| Bug | Statut | Justification |
|---|---|---|
| **N°1** Slot-table timing | 🟢 SOLIDE | Tables verbatim copiées d'`openMSX VDPAccessSlots.cc`, algorithme `getTab` + Delta D28 identique. Test `tms9918_silicon_strict_runtime` (35 assertions). |
| **N°2** /INT → /IRQ | 🟢 SOLIDE | La carte P-LAB **câble** /INT → /IRQ (trace vérifiée sur le vrai matériel par Parmigiani) ; le soft Nippur72 ne s'en sert pas (polling $CC01). POM1 default = `irqStrapped=true` → `irqAsserted()` = R1.5 ∧ status.7 ; l'IRQ reste inerte tant que le programme ne fait pas `CLI`. Toggle `setIrqStrapped(false)` pour une carte hypothétique non câblée. |
| **N°3** R1.7 4K/16K | 🟢 SOLIDE | Mask `0x0FFF` vs `0x3FFF` selon R1.7. Datasheet confirme. Pinned par T06. |
| **N°4** Collision range | 🟢 SOLIDE | **Visible-only `[0, kScreenWidth)`** par openMSX `SpriteChecker.cc:187-191` (*"Sprites that are partially off-screen position can collide, but only on the in-screen pixels. Sprites cannot collide in the left or right border, only in the visible screen area"*). Confirmé par meisei vdp.c:587-589 (guard bytes à 0x80 hors 256). Corrigé mai 2026 (était `[-32, 288)` per Nouspikel — qui contredisait les deux refs canoniques). Pinné par T07 + ctest. |
| **N°5** Per-scanline scan | 🟢 SOLIDE | Port openMSX `SpriteChecker::checkSprites1` line-major (`SpriteChecker.cc:87`). Sub-cycle silicon details (ordre exact des fetches SAT, dummy reads après $D0) **non modélisés par openMSX non plus** — POM1 a la même résolution que la référence canonique. Pinné par tests T07-T11, T18-T20 + ctest. |
| **N°6** Status bits 0..4 | 🟢 SOLIDE | "Last sprite walked" — logique simple confirmée par Nouspikel. T08 + ctest test 6. |
| **N°7** Sprite scan in blank | 🟢 SOLIDE | POM1 skip le sprite scan en blank (cohérent avec meisei `vdp.c:437` `mode=9` → blank case = `memset bd 256` sans sprite). **Mai 2026** : POM1 force aussi status bits 0..4 à `$1F` en blank (per meisei vdp.c:437 `statuslow=0x1f`) au lieu de préserver la dernière valeur. T12. |
| **N°8** Sprite cloning | 🟢 SOLIDE | **Port verbatim de meisei `vdp.c:591-670`** (mai 2026, hap — *"the only known correct implementation, tested side-by-side against real MSX1"*). Condition trigger = `M3 set ∧ (R4 & 3) ≠ 3` (R6 ne joue **pas**, contrairement à l'ancienne approximation POM1). Algorithme par 4 blocs de 64 lignes avec `clonemask[6]` modulant `yc = (line & cm[0]) - ((y ^ cm[1]) | cm[2])`. Sprites 0..7 jamais clonés (preprocessed in HBlank). |
| **N°9** Flipflop reset | 🟢 SOLIDE | `latchIsSecond = false` dans readControl. Datasheet + Nouspikel confirment. T13 + ctest. |
| **N°10** Raster split 5S | 🟢 SOLIDE | Mécanisme via per-scanline scan (Bug N°5). Granularité ligne-major = openMSX. Timing exact sub-scanline n'est pas modélisé par openMSX non plus → POM1 atteint la limite de la référence canonique. Pour des splits cycle-précis il faudrait étendre le modèle au-delà de l'état de l'art emulator. T15. |
| **N°11** NTSC 59.94 Hz | 🟢 SOLIDE | Constante exacte 17062c = floor(1.022727 MHz / 59.94). Math vérifié. T14. |
| **HBlank précis** | 🟢 SOLIDE | Couvert par le slot-table porté d'openMSX (Bug N°1) **et** API publique `TMS9918::inHBlank()` (TMS9918.cpp, port verbatim de openMSX `VDP::getHR()` VDP.hh:948-961). Constantes TMS9918A NTSC : `HBLANK_LEN_TXT=404`, `HBLANK_LEN_GFX=312` ticks ; `getLeftSprites` 282/258 ticks (text/gfx) ; `getRightBorder` = leftSprites + 960/1024. Permet aux callers de tester si le faisceau est en HBlank. |
| **Dérive thermique N°8** | 🔴 NON MODÉLISÉ | Silicon NMOS chaud → clones disparaissent. **Consensus emulator** : ni openMSX ni meisei ni POM1 ne modélisent. Hors scope. |
| **Tick→cycle drift** | 🟢 SOLIDE | 21 ticks/cycle. openMSX `TICKS_PER_SECOND = 3579545×6 = 21477270`, 6502 = 1022727 → **ratio exact 21.0000029** (drift = 3 ticks/sec). Imperceptible. (L'ancien doc claim de 20.97 était une erreur — corrigé mai 2026.) |
| **Chip type dispatch** | 🟢 SOLIDE | `enum class TMS9918::ChipType` couvre TMS9918A (default), TMS9929A, TMS9118/9128/9129, T7937A, T6950 (TMS9918.h). Setter `setChipType()` runtime. Toshiba (T7937A/T6950) court-circuit le sprite cloning (`isCloningActive` retourne false) — match meisei `!toshiba` guard et openMSX TMS-vs-Toshiba dispatch. V99x8 non modélisé (POM1 n'a pas la cible MSX2). Default reste TMS9918A pour la cible Apple-1 + P-LAB Graphic Card. |
| **Color-0 sprite collision** | 🟢 SOLIDE | POM1 = collide. openMSX MSX1 (`isMSX1VDP()`) = collide always (`VDP.hh:201-208`). meisei = collide. **Match consensus** sur le comportement TMS9918A — la "???" mention dans les commentaires openMSX réfère uniquement aux V99x8 (qui ont un toggle). |
| **Hybrid mode rendering** | 🟢 SOLIDE | **Port meisei dispatch (mai 2026)**. M3+M1 → fallback text (M3 ignored). M3+M2 → fallback multicolor. M1+M2 (et all-three après M3-XOR) → "static vertical bars" glitch (4 px text-color + 2 px backdrop, ×40, indépendant de VRAM). Programs affectés : Lotus F3 (MSX1 palette), Illusions demo, dvik/joyrex `scr5.rom`. Sprites OFF en mode 1/5 (text-derived) per meisei `if (~mode&1)`. |
| **Text mode borders** | 🟢 SOLIDE | **6 px left / 10 px right** asymétrique per datasheet TMS9918A + meisei vdp.c:475-510 (corrigé mai 2026 — était 8/8 symétrique avant). |
| **VRAM power-on init** | 🟢 SOLIDE | **`$FF` even / `$00` odd** bistable per meisei vdp.c:212-217 (corrigé mai 2026 — était all-zero avant). MSX1 silicon. MSX2 settle à all-`$FF`, programs MSX2-targeted (e.g. *Universe: Unknown* final) légèrement glitchy au boot — comportement consistent avec MSX1 hardware. |

**Légende** :
- 🟢 SOLIDE — implémentation déterministe basée sur référence canonique (datasheet, openMSX source)
- 🟡 NON-VÉRIFIÉ — modèle plausible reproduisant le comportement documenté, mais non confronté à du silicon réel
- ⚠️ APPROXIMATION — modèle imparfait qui produit un effet visible/observable mais qui peut diverger de silicon dans les détails
- 🔑 OPEN — comportement silicon réellement inconnu, POM1 prend un parti
- 🔴 NON MODÉLISÉ — comportement silicon connu mais POM1 ne le simule pas

**Couverture par tests** (chiffres = état 2026-04-30) :
- tests ctest dédiés TMS9918 : `tms9918_sprite_status`, `tms9918_silicon_strict_runtime`, `tms9918_per_scanline`, `tms9918_advanced_silicium` (les ~19 ctest de l'époque couvraient aussi les autres sous-systèmes — pas seulement le VDP)
- ~20 sous-tests 6502-asm dans la ROM TMS_SilBench, auto-vérifiables
- tests interactifs visuels standalone (`tms9918_clone` T12, `tms9918_split`)
- Stress benchmark Galaga-class (~30 sec)
- Démo finale 6 phases (~30 sec) avec sprites fauna réels

---

## 1. Contexte & cible matérielle

- **Plateforme cible** : Apple-1 + carte TMS9918 (P-LAB Graphic Card) + daughterboard CodeTank (piggyback).
- **Preset POM1 équivalent** : **#7** — *P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)* (voir [`README.md`](../README.md) § Machine Presets).
- **Référence "OK partout" (silicium + POM1)** : `TMS_Logo v1.7` — interpréteur LOGO en Mode 2 bitmap, pas de sprites. Validation du sous-système VRAM/registres/modes graphiques.
- **Référence "OK POM1, KO silicium"** : `A1Galaga` — Mode 1 + sprites animés à 60 Hz. Sur silicium, sprites-artefacts et damiers parasites apparaissent autour des sprites légitimes dès l'écran de titre.

### Map mémoire utilisable (preset **#7**)

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

1. **Délai physique de préparation** : ~28 ticks VDP (≈ 1,3 µs, openMSX `Delta::D28`).
2. **Attente du prochain slot libre** : variable selon la position dans la scanline et le mode actif.

Si le CPU envoie le prochain octet **avant que le latch précédent n'ait été drainé**, le chip écrase silencieusement (openMSX `tooFastCallback` avec `allowTooFastAccess=false`). Aucune erreur signalée. C'est la cause primaire des damiers Galaga sur silicium réel.

### Modèle slot-table openMSX (source de vérité)

POM1 v6.x a remplacé le seuil min-distance par un **port verbatim du modèle openMSX** (`src/video/VDPAccessSlots.cc`). Une scanline TMS9918 NTSC fait 1368 ticks VDP (≈ 63,7 µs, `TICKS_PER_LINE` openMSX). Pour chaque mode, openMSX publie la liste exacte des positions de slot CPU dans la ligne :

- `slotsMsx1ScreenOff` — display blanké (R1.6=0). Slots tous les ~8 ticks pour la majeure partie de la ligne.
- `slotsMsx1Gfx12` — Mode I (Graphic 1) + Mode II (Graphic 2). 19 slots/ligne ; pattern bursty `4, 12, 20, 28, 116, 124, 132, 140, 220, 348, 476, 604, 732, 860, 988, 1116, 1236, 1244, 1364`.
- `slotsMsx1Gfx3` — Multicolor (Mode 3). 51 slots/ligne, intermédiaire entre Gfx12 et Text.
- `slotsMsx1Text` — Text (Mode 1). 91 slots/ligne, dense pour la première moitié.

À chaque accès CPU à `$CC00/$CC01` :
1. POM1 calcule `linePosTicks = (frameCycleCounter * 21) % 1368` (1 cycle 6502 ≈ 21 ticks à 1.022727 MHz).
2. Recherche dans la table active : `slotTick = smallest slot ≥ linePosTicks + 28`.
3. `pendingDrainCycles = ceil((slotTick - linePosTicks) / 21)` cycles 6502.
4. Pendant ce drain, tout autre accès est silencieusement écrasé.

**Important** : openMSX **ne distingue pas sprites-on/off** sur MSX1 — `slotsMsx1Gfx12` agrège les deux cas (le pattern de slots reflète déjà le pire cas avec sprites). On supprime donc le scan SAT[0]=$D0 historique.

### Divergence POM1 vs openMSX : VBlank libre

openMSX choisit la table active uniquement selon `(R1.6, mode)` — la position verticale dans la frame n'influe pas. Cela fait que pendant les ~70 lignes de VBlank NTSC avec display ON, openMSX impose la table Gfx12 alors que sur silicium **le sprite-scan + pixel-fetch n'ont pas lieu** : la bande passante VRAM est libre.

POM1 corrige ça côté `selectSlotTable()` : si `frameCycleCounter >= kActiveDisplayCycles` (i.e. en VBlank), on bascule sur `slotsMsx1ScreenOff` quel que soit R1.6. Cela autorise l'idiome silicon-correct :

```asm
@wait_vbl: BIT $CC01
           BPL @wait_vbl     ; spin until F flag set
           ; ~4554 cycles 6502 de bande passante CPU-libre disponibles
```

Sans cette divergence, des programmes comme Rogue (qui empilent les uploads en VBlank) auraient des drops fantômes sous POM1 alors qu'ils tournent sur silicium réel.

### Rendu progressif par scanline (rainbow demos) ✅ IMPLÉMENTÉ

POM1 conserve une `framebuffer[320×240]` persistante côté `TMS9918`, peinte ligne par ligne dans `advanceCycles` au moment où le faisceau franchit chaque scanline. Conséquences silicon-correctes :

- **R7 (backdrop) changé en milieu de frame** → le L/R border bands des scanlines suivantes utilisent la nouvelle couleur. Lignes déjà rasterisées gardent l'ancienne. Effet "rainbow" possible.
- **R1.6 (display blank) toggled en milieu de frame** → lignes "active" = pixels rendus, lignes "blank" = backdrop seul.
- **VRAM modifiée en milieu de frame** → seules les lignes rendues APRÈS la modification voient le nouveau pattern/SAT/color. Lignes antérieures sont figées.
- **R5/R6/R4 changés mid-frame** → next-line render utilise les nouveaux pointeurs (split-screen attribute changes).

Cf. `TMS9918::renderActiveLine`, `paintLeftRightBorderForActiveLine`, `paintTopBorder`, `paintBottomBorder`. Les helpers `renderGfxILineRaw` / `renderGfxIILineRaw` / `renderTextLineRaw` / `renderMulticolorLineRaw` / `renderSpritesLineRaw` font le travail per-scanline raw, partageable entre live (progressive) et legacy (snapshot).

Test pin : `tests/tms9918_per_scanline_test.cpp` Phase F valide qu'un changement R7 mid-frame produit deux couleurs de bordure différentes selon la zone verticale.

### Pire-cas silicon par mode (mesuré via slot-table)

| Mode | Pire gap inter-slots (ticks) | Cycles 6502 | Pire-cas total (D28 prep + gap) |
|---|--:|--:|--:|
| Display blanké | 16 | 0,8c | 2c (D28 dominé) |
| Texte (M1) | 16 | 0,8c | 2c |
| Multicolor (M2) | 88 | 4,2c | 5c |
| **Graphic I/II** | **128** | **6,1c** | **7,5c** |

Le **plancher empirique Tetris (gap 11c)** se situe ~50% au-dessus du pire silicon Gfx12 (7,5c) — confortable. Galaga (4c bursts) est sous le plancher → drops attendus, comme sur silicium.

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

### Impact sur le code (silicon worst-case Gfx12 = ~7,5c, pad12 OK)

À 1,022 MHz Apple-1 : **1 cycle ≈ 0,978 µs**. Le modèle slot-table donne un pire-cas Gfx12 de ~7,5c (D28 prep + 128 ticks worst gap). Tous les patterns ci-dessous sont jugés contre ce floor — Tetris (11c) passe largement, Galaga (4c bursts) drop systématiquement.

| Pattern 6502 | Gap entre writes | Verdict slot-model | Note |
|---|---|---|---|
| `STA $CC00` ; `STA $CC00` (back-to-back) | 4c | **KO** | sous le floor |
| `LDA #x` ; `STA $CC00` ; `LDA #y` ; `STA $CC00` | 6c | **KO** | sous le floor |
| `STA $CC00` ; `NOP×2` ; `STA $CC00` (ancien strict 8c) | 8c | **borderline** | dépend de la phase |
| `STA $CC00` ; `JSR tms9918_pad12` ; `STA $CC00` | 16c | **OK** (3 octets, gagnant ratio) |
| `STA $CC00` ; `NOP×6` ; `STA $CC00` | 16c | **OK** (6 NOPs = 6 octets) |
| Boucle Tetris (`LDA / STA $CC00 / DEX / BNE`) | 11c | **OK** (silicon-validated) |
| Boucle `LDA tab,X / STA $CC00,X / DEX / BNE` | 14c | **OK** confortable |

Le choix de **`JSR tms9918_pad12`** (16c gap, 3 octets au site) reste optimal : ratio 4c/octet, deux fois plus dense que NOP×6 (2c/octet). `pad24` et `pad40` sont des cushions plus larges utiles pour les routines callées depuis des call-sites multiples (Phase 7 du test runtime).

### Diagnostiquer un programme qui drop

POM1 expose une infrastructure de diagnostic complète quand `Silicon Strict` est ON :

- **Status bar** : compteur live `STRICT — drops:N` à côté du tag STRICT/FANTASY.
- **Trace stderr** : les 60 premiers drops sont tracés une ligne par drop avec PC, valeur, vramAddr, drain restant, position dans la ligne, table active, port (D=$CC00 / C=$CC01), phase (active/vblank). Format :
  ```
  [TMS9918 DROP #N] D val=B0 vramAddr=1100 latch2=0 drain=4c linePos=1152
  nextSlot=1236 tbl=Gfx12 vblank=0 frameCycle=120 R1=C0 PC=$5A04
  ```
- **Menu Hardware → Dump TMS9918 drop diagnostics** : produit sur stderr le histogramme complet :
  - total + breakdown port/phase/table
  - top-16 PC (mid-instruction ; le `STA` est à `PC-3` pour `STA abs` / `STA abs,X`)
  - reset via `Reset TMS9918 drop counter` adjacent
- **CLI** : voir [`doc/CLI.md`](../doc/CLI.md) — `--silicon-strict` / `--no-silicon-strict` au boot.

Pour cibler un programme silicon-safe, le workflow est : strict ON → lancer le programme → `Dump diagnostics` → corriger la PC en tête de liste (probablement un STA dans une boucle serrée) → re-tester → itérer.

### Padding dense — `JSR tms9918_pad12` (preferred)

Le helper `tms9918_pad12: rts` (1 octet) coûte 12 cycles via JSR (3 octets au site). **Ratio 4 c/B au site**, deux fois plus dense que NOP (2 c/B). Pour 16c gap STA-STA, le JSR remplace 6 NOPs (économie : 3 octets/site).

`tms9918_pad24: jsr pad12 / rts` chaîne pour les pads exotiques (24c en 3 octets au site, helper 4 octets). Les deux sont définis dans `dev/lib/tms9918/tms9918_pad.asm` et exportés via `.export` ; le linker config de chaque projet TMS9918 charge la lib via `EXTRA_ASM` (Makefile.common) ou directement via `tools/build_codetank_rom.py` qui auto-détecte la dépendance.

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

**Option A — Macros `WRT_DATA_REG` / `WRT_DATA_VAL` côté lib (recommandé)**

`dev/lib/tms9918/tms9918.inc` fournit (paranoid 16c) :
```asm
.import tms9918_pad12, tms9918_pad24

.macro WRT_DATA_REG             ; A déjà chargé, gap 16c en sortie
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad12   ; 12c, 3 bytes — next write lands 16c later
.endmacro

.macro WRT_DATA_VAL val          ; val immédiat, gap 16c en sortie
        LDA     #val            ; 2c
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad12   ; 12c, 3 bytes
.endmacro
```

**Option B — Indexed addressing (bonus 1 cycle gratuit)**

Remplacer `STA $CC00` par `STA $CC00,X` (X = 0). Coût : +1 cycle (4c → 5c) sans NOP. Deux STA indexées back-to-back = 10 cycles → OK.

**Option C — Réécriture en boucle table-driven**

Précalculer chaque frame une table SAT en RAM (40 octets), puis l'uploader avec une boucle `LDA tab,X / STA $CC00 / NOP / NOP / INX / BNE` (4+4+2+2+2+3 = 17c entre writes). Plus simple à raisonner que les writes en ligne.

### Validation

Sur émulateur, ces 3 options ne changent rien (POM1 est tolérant). Sur silicium, l'option A ou C **doit faire disparaître les sprites-artefacts**. Si ce n'est pas le cas, le bug primaire est ailleurs et il faut creuser les bugs N°2 à N°7.

---

## 3. BUG N°2 — Ligne /INT câblée sur la carte P-LAB

### Ce que fait le silicium TMS9918

Quand R1 bit 5 = 1 (interrupt enable) et bit 7 du status register = 1 (frame flag), le TMS9918 tire la broche `/INT` au niveau bas (active-low). Reading $CC01 clears bit 7 → /INT relâché.

### Ce que fait la carte P-LAB

**P-LAB câble /INT → /IRQ.** La broche `/INT` du VDP est connectée à la ligne `/IRQ` du 6502 — Parmigiani a vérifié la trace sur le vrai matériel. Le soft de Nino Porcino (libs **Nippur72**) ne l'utilise jamais : l'usage canonique reste le **polling via $CC01 bit 7** (plus simple, portable, indépendant du flag I) :

```asm
@vblank_wait:
    LDA VDP_CTRL    ; lecture status
    BPL @vblank_wait ; bit 7 = 0 → pas encore VBlank
    ; ici on est dans VBlank, status auto-cleared par la lecture
```

Mais la ligne étant bien présente, un handler IRQ-on-VBlank (`CLI` + vecteur `$FFFE`) fonctionne aussi sur le vrai matériel.

### Ce que fait POM1

✅ **Conforme P-LAB par défaut** : `irqStrapped == true` (état d'origine). `TMS9918::irqAsserted()` reflète le silicium — R1.5 ∧ status.7 — et l'IRQ aggregator de `Memory::advanceCycles` tire le `/IRQ` 6502 en conséquence ; la lecture de $CC01 efface le frame flag → l'IRQ se relâche au tick suivant. Comme sur le vrai matériel, la requête reste **inerte tant que le programme n'a pas fait `CLI`** (le flag I masque l'IRQ au reset) : le code Nippur72 polling-only n'est donc pas affecté.

### Désactiver le câblage (carte hypothétique non câblée)

`TMS9918::setIrqStrapped(false)` modélise une carte où /INT serait laissé flottant. `irqAsserted()` renvoie alors `false` sans condition (le VDP ne peut plus déclencher d'IRQ frame), quelle que soit la valeur de R1.5.

### Impact

- Du code Apple-1 / Nippur72 **stock P-LAB** = polling-only → marche identiquement sur silicium et POM1 (l'IRQ matérielle est présente mais masquée, sans effet).
- Du code style MSX-port avec `LDA #$E0 / STA VDP_CTRL / CLI / loop` qui dépend de l'IRQ-on-VBlank → **fonctionne** sur P-LAB comme sur POM1 default, à condition d'installer un handler valide au vecteur `$FFFE` qui lit `$CC01` de façon atomique (cf. la convention flip-flop $CC01 ci-dessous). Le polling reste néanmoins recommandé.

### Workaround portable

**Poller bit 7 du status register** reste le pattern recommandé (indépendant du flag I et du contenu du vecteur IRQ, sans piège de réentrance du flip-flop $CC01). L'IRQ-on-VBlank est désormais une option valide puisque la ligne est câblée.

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

## 5. BUG N°4 — Collision sprite en overscan ✅ IMPLÉMENTÉ

### Ce que fait le silicium

Le sprite engine du TMS9918 raster une zone interne **plus large que l'écran visible** : ~32 pixels d'overscan à gauche (x = -32..-1) et 32 à droite (x = 256..287). Deux sprites qui se chevauchent dans cette zone hors-écran déclenchent le **bit 5 (collision) du status register** même si rien n'est visible.

### Ce que fait POM1

**`scanSpritesForLine()` dans `TMS9918.cpp` étend la collision à `[-32, 288)`** depuis le passage au modèle slot-table openMSX (mai 2026). Le test `tms9918_per_scanline` Phase D pinne ce comportement. Le rendu (`renderSprites`) reste clippé à `[0, 256)` — l'overscan n'apparaît pas visuellement, seulement dans la détection de collision (silicon-correct).

### Pin du comportement

`tests/tms9918_per_scanline_test.cpp` Phase D : 2 sprites en early-clock (color bit 7 = 1, X=10 → réel X=-22), pattern $FF, Y=50. Lecture du status après une frame → bit 5 doit être set.

---

## 6. BUG N°5 — Sprite scan 1×/frame (au VBlank) vs par scanline ✅ IMPLÉMENTÉ

### Ce que fait le silicium

Les flags collision (bit 5) et 5S (bit 6) sont mis à jour **scanline par scanline** pendant le rendu, en temps réel pendant que le faisceau trace l'image. Un poll du status register **au milieu d'une frame** voit l'état exact à la position courante du faisceau.

### Ce que fait POM1

**`advanceCycles()` invoque `scanSpritesForLine(line)` à chaque scanline franchie** (mai 2026). Port verbatim de la logique openMSX `SpriteChecker::checkSprites1` (line-major variant — POM1 walk la SAT une fois par scanline, openMSX la sprite-major loop optim n'apporte rien à 60 Hz). `scanSpritesForStatus` (1×/frame) reste comme fallback VBlank pour les frames où le display est resté blanké.

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

## 7. BUG N°6 — Status bits 0..4 ne reflètent pas le "dernier sprite scanné" ✅ IMPLÉMENTÉ

### Ce que fait le silicium

Sur le TMS9918A standard, bits 0..4 du status register contiennent l'index SAT du **dernier sprite scanné**, mis à jour ligne par ligne. Si bit 6 (5S) est latché, c'est l'index du 5e sprite. Sinon, c'est l'index du dernier sprite traité (souvent le terminator $D0 ou le sprite 31).

### Ce que fait POM1

**`scanSpritesForLine()` met à jour bits 0..4 = lastSpriteIdx** à chaque scanline (mai 2026), où `lastSpriteIdx` est l'index SAT du dernier sprite walké pour cette ligne (terminator, ou sprite 31, ou n'importe quel index trouvé). Quand 5S latche, bits 0..4 freezent à l'index du 5e sprite (silicon-correct).

### Pin du comportement

`tests/tms9918_per_scanline_test.cpp` Phase C : 4 sprites à Y=50, terminator à SAT[4]. Après une frame complète, status doit avoir bits 0..4 = 4 (l'index du terminator) et bit 6 = 0.

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

## 9. BUG N°8 — Sprite Cloning ✅ MODÉLISÉ (port meisei verbatim, mai 2026)

### Ce que fait le silicium

Le TMS9918 expose 8 combinaisons valides des bits M1/M2/M3 (4 modes documentés + 4 réservés/non-utilisés). Quand le code force des **combinaisons illégales** (M1+M2 actifs simultanément, ou M3 + flags d'interférence), le silicium ne signale aucune erreur mais entre dans une zone de **comportement chaotique reproductible** :

- L'**axe Y** des sprites d'index 8 à 31 devient pollué par les bits faibles de l'offset SPGT (R6) et les bits 5-6 de la Color Table (R3).
- Conséquence visible : **clones verticaux fantômes** des sprites originaux apparaissent dans la strate haute de l'écran (Y = 0..63), formant un motif d'écho en tuiles déformées.
- Ces clones **consomment des slots** dans la limite 4-sprites-par-scanline et **déclenchent le bit C (collision)** comme des sprites légitimes.
- La démo MSX **"Alankomaat"** (groupe Bandwagon) exploite volontairement cet effet pour afficher un nombre de sprites impossible.

### Dérive thermique (cas limite)

Sur silicium TMS9918A original (TI / NMOS), les clones **s'estompent progressivement quand la puce chauffe** (effet "sèche-cheveux" documenté). Les clones de l'index Block 1 disparaissent en premier, puis Block 2. Les clones produits par Toshiba ou les Yamaha V9938 ont les chemins d'adressage corrigés en usine — **aucun cloning, le code qui le sollicite ne marche pas**.

### Ce que fait POM1

✅ **MODÉLISÉ — port verbatim de meisei** (`src/vdp.c:591-670`, auteur **hap**, mai 2026). hap a publié dans [openMSX issue #593](https://github.com/openMSX/openMSX/issues/593) que c'est *"the only known correct implementation, tested side-by-side to my MSX1, with all possible sprite Y and addressmasks"* — donc **la** référence de fait.

POM1 expose 2 prédicats indépendants désormais :

- **`isIllegalModeRegs(regs)`** — true si ≥ 2 des bits M1/M2/M3 sont actifs (combinaisons hybrides réservées). Sert à décider du **bypass du BG playfield** (backdrop-only) — comportement non lié au cloning.
- **`isCloningActive(regs)`** — true ssi `(R0 & 2) ∧ ((R4 & 3) ≠ 3)` (condition meisei). C'est la vraie condition de déclenchement du cloning silicon. **R6 ne joue aucun rôle** (contrairement à l'ancienne approximation POM1).

Le cloning peut donc s'enclencher en **Mode II parfaitement légal** (M3 set, M1=M2=0) si R4 a au moins un bit 0-1 cleared — c'est exactement ce que fait le programme BASIC de référence de hap (issue #593) pour exhiber le bug. Réciproquement, M1+M2 hybride (sans M3) est illegal mais **ne clone pas**.

L'algorithme `renderCloneSpritesLineRaw` (`TMS9918.cpp`) divise la frame en 4 blocs de 64 scanlines :

| Block | Lignes | Condition | Effet sur `cm[0..5]` |
|---|---|---|---|
| 0 | 0..63 | jamais (sprites 0..7 preprocessed in HBlank) | — |
| 1 | 64..127 | `R4 & 1 == 0` | `cm[0]=$3F`, `cm[5]=(~R3 << 1) & $40` |
| 2 | 128..191 | `R4 & 2 == 0` | `cm[1]=cm[4]=$80`, `cm[5]=(R3 << 1) & $80` |
| 3 | 192..255 | always (off-screen pour POM1, omis) | — |

Pour chaque sprite slot 8..31 (les 0..7 sont **non affectés**) :

```
yc = (line & cm[0]) - ((y ^ cm[1]) | cm[2])
if (yc >= spriteH) yc = (line & cm[3]) - ((y ^ cm[4]) | cm[5])
if (yc < spriteH)  → render le sprite à yc avec le pattern fetch + color path standard
```

C'est l'algo silicon exact (ou l'inversion mathématique la plus proche qu'hap a pu déduire après comparaison side-by-side avec hardware réel).

### Limites résiduelles

- **Block 3 (lignes 192..255) omis** — kScreenHeight=192, donc on ne rend jamais ces scanlines. Les sprites placés en Y_attr=192..254 qui devraient wrapper sur la frame visible sont perdus côté cloning. Impact : démos qui exploitent le cloning de sprites tout-en-haut via Y wrap. **TODO si nécessaire** : étendre la pipeline pour traiter le cloning des Y_attr supérieurs à 192 et les remapper sur les scanlines 0..63 visibles.
- **Dérive thermique** non modélisée (les clones disparaissent par bloc quand le VDP chauffe — block 1 d'abord puis block 2). hap décrit le test "blow dryer" dans le commentaire meisei. POM1 = comportement "froid" permanent.
- **Toshiba / Yamaha V9938+** ont l'addressing factory-fixed et **ne clonent pas**. POM1 hard-code "TI silicon" (pas de dispatch sur kind de chip).

⚠️ **Conséquence pour T16 du `tms9918_siltest`** (mai 2026) : T16 utilise R0=$02 + R1=$D8, ce qui set M1+M2+M3 simultanément. Avec le nouveau dispatch meisei :
- mode = 7 → après XOR M3 → mode 5 → "vertical bars glitch" + **sprites OFF** (M1 set).
- isCloningActive gate = `!M1 && M3 && (R4&3)≠3` → false (M1 set).
- Donc T16 affiche désormais : barres verticales statiques, **pas de sprites, pas de clones**.

L'opérateur doit donc répondre **N** à la question "saw ghost sprites?" — c'est le comportement silicon correct. T16 devient un test du **mode hybride 5** plutôt que du cloning. Le vrai test cloning est désormais `dev/projects/tms9918_clone/` (Mode II legal + R4=0 + sprite #31, hap-style — fires cloning sans M1 pour respecter le gate sprite-engine).

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

Note : sur Apple-1 + TMS9918, **la ligne /INT est câblée** au /IRQ 6502 (Bug N°2, default `irqStrapped=true`). Dès qu'un programme fait `CLI` avec R1.5=1, cette IRQ se produit réellement et la convention ci-dessus devient critique — le handler DOIT lire $CC01 de façon atomique. Le soft Nippur72 reste à l'abri car il polle sans jamais démasquer l'IRQ (`CLI`).

---

## 11. BUG N°10 — Hack raster split via 5S (effets mid-scanline) ✅ IMPLÉMENTÉ

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

✅ **IMPLÉMENTÉ** : `scanSpritesForLine` est invoqué à chaque scanline franchie par le frameCycleCounter (mai 2026). Bit 6 s'arme exactement quand le faisceau croise la 5e ligne — la boucle d'attente du CPU sort à la même position qu'en silicium.

### Pin du comportement

`tests/tms9918_per_scanline_test.cpp` Phase B : 5 sprites à Y=95, polling bit 6 simulé. Avant le franchissement (line ~50) bit 6 = 0 ; après (line ~110) bit 6 = 1 + low 5 bits = 4 (l'index du 5e sprite).

---

## 12. BUG N°11 — Frame rate 59,94 Hz NTSC vs 60 Hz POM1 ✅ IMPLÉMENTÉ

### Ce que fait le silicium

NTSC analogique = **59,94 Hz** exactement (60 × 1000/1001), pas 60 Hz rond. PAL = 50 Hz exact. Cette fréquence non entière entraîne, sur des routines de multiplexage SAT (cf. Bug N°6 flicker authentique), une **dérive de phase** progressive dans les inversions de groupes.

### Ce que fait POM1

`POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` dans `CpuClock.h` cale désormais POM1 à **17062 cycles/frame ≈ 59.94 Hz** (mai 2026, était 17045 = 60 Hz round). Formule : `(1001 × 1022727 + 30000) / 60000`. Drift de ~0.1% éliminé pour les démos audio fines et les multiplexages SAT timing-critiques.

### Pin du comportement

`tests/tms9918_per_scanline_test.cpp` Phase E : asserte `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ == 17062`.

---

## 13. Protocole de bringup émulateur → silicium

Checklist en 6 étapes pour porter un programme POM1 vers silicium sans surprise :

1. **Mesurer la RAM utilisable réelle** — Confirmer `$0200-$1FFF` libre (devrait l'être). Tester `$2000-$3FFF` (libre seulement sans HGR). Tester `$8000-$BFFF` (libre sans microSD/CFFA1/JukeBox).

2. **Vérifier l'init VDP** — R1 doit avoir bit 7 = 1 (16K). R0/R2/R3/R4/R5/R6/R7 selon mode. Re-checker les noms de registres dans `dev/lib/tms9918/tms9918.inc`.

3. **Auditer toutes les boucles d'écriture VRAM** — Chaque paire `STA VDP_DATA` consécutive doit avoir **≥ 8 cycles** entre les writes pendant l'affichage actif. Préférer `STA $CC00,X` ou macro `WRT_DATA_NOP`. Boucles d'init VRAM en début de programme peuvent ignorer cette règle si l'affichage est blanké (`R1 bit 6 = 0`) pendant le chargement.

4. **Préférer le polling à /INT** — Poller bit 7 du status register reste le pattern recommandé (simple, indépendant du flag I, sans piège de réentrance du flip-flop $CC01). La ligne /INT est bien câblée sur P-LAB (Bug N°2), donc un handler IRQ-on-VBlank marche aussi — mais seulement avec un vecteur `$FFFE` valide et une lecture $CC01 atomique.

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

1. ~~**Mode "silicon timing strict"** — option qui drop des octets si écriture VRAM trop rapide pour le mode actif (perfectionne Bug N°1).~~ **✅ IMPLÉMENTÉ** : `TMS9918::siliconStrictMode` — si activé, `canAcceptAccess()` bloque les accès tant que `pendingDrainCycles` > 0 ; chaque accès accepté recalcule le délai jusqu’au prochain slot CPU via les tables openMSX portées en §2 (Bug N°1). Le comportement est **phase-dépendant** (mode d’affichage, sprites, VBlank, position dans la scanline), pas un unique seuil « N cycles » fixe pour tout le frame. Contrat pratique : *strict ON ⇒ alignement avec ce modèle slot-table*. Défaut ON pour tous les presets sauf Multiplexing Fantasy. Toggle : menu *Hardware → Silicon Strict (TMS9918 timing)* et CLI [`doc/CLI.md`](../doc/CLI.md) (`--silicon-strict` / `--no-silicon-strict`). Status bar `STRICT` / `FANTASY`. Snapshot : bit `kFlagSiliconStrict` (bit 14). Pin principal : `tests/tms9918_silicon_strict_runtime_test.cpp` (+ famille `tms9918_*` dans `CLAUDE.md` / `tests/CMakeLists.txt`).
2. **Scan sprite scanline-by-scanline** — appel de `scanSpritesForStatus()` en cours de frame, pas seulement au VBlank (résout Bugs N°5 et N°10).
3. **Frame rate 59,94 Hz** — option NTSC exact (résout Bug N°11).
4. **Sprite cloning — détection hybrides** — le cloning (Bug N°8) est déjà modélisé (meisei) ; une voie restante serait la dérive thermique NMOS (hors scope) et le raffinement des modes hybrides illégaux hors couverture test.
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

Script réutilisable : **`tools/silicon_strict_patch.py`** (insertion `JSR tms9918_pad12` aux sites détectés — cf. §2 patterns 6502 / gap STA–STA). Idempotent — `--unpatch` strippe les marqueurs v1 (NOPs) et v2 (JSR) avant ré-insertion fraîche.

```bash
# Patch in place
python3 tools/silicon_strict_patch.py path/to/Game.asm

# Dry-run (compte sans écrire)
python3 tools/silicon_strict_patch.py path/to/Game.asm --dry-run

# Strip-only (revert sans réinsertion)
python3 tools/silicon_strict_patch.py path/to/Game.asm --unpatch
```

Règles appliquées (cumulatives, ordre déterministe) :

| Cas | Pattern détecté | Insertion v2 (`JSR tms9918_pad12`) | Octets ajoutés |
|---|---|---|--:|
| **A** | `ST? VDP_*` adjacent à `ST? VDP_*` | `JSR tms9918_pad12` entre | 3 |
| **B** | `ST? VDP_* / LDA #imm / ST? VDP_*` | `JSR tms9918_pad12` AVANT le LDA | 3 |
| **C** | `ST? VDP_* / LDA <zp/abs/zp,X> / ST? VDP_*` | `JSR tms9918_pad12` AVANT le LDA | 3 |

Le patcher injecte aussi `.import tms9918_pad12` une fois en haut de chaque fichier patché (pour les projets qui n'incluent pas `tms9918.inc`). Le helper `tms9918_pad12 / pad24` vit dans `dev/lib/tms9918/tms9918_pad.asm` et est lié automatiquement par `Makefile.common` (via `EXTRA_ASM`), `emit_woz.py` (auto-détection), et `build_codetank_rom.py` (auto-détection).

`ST?` couvre `STA` / `STX` / `STY` (Galaga utilise les trois).
Cross-port (`VDP_DATA → VDP_CTRL` ou inverse) : la fenêtre est unique pour
les deux ports — le matcher couvre `VDP_(DATA|CTRL)` indistinctement.

**Pas de skip annotation — strict means strict.** Les versions antérieures
du patcher honoraient un commentaire `; SILICON_STRICT_SKIP` pour
exempter une routine de l'injection de pads. Cet *escape hatch* a été
retiré (mai 2026) pour deux raisons :
1. Substring-match footgun : un commentaire mentionnant le nom de la
   directive (par ex. *« do not add SILICON_STRICT_SKIP here »*)
   désactivait silencieusement l'injection sur toute la routine —
   incident hide_slot_4 dans Galaga où des heures ont été perdues à
   chercher une régression cycle alors que les pads n'avaient simplement
   pas été émis.
2. Une « strict mode » avec exemptions par routine est une promesse
   creuse : un build qui passe strict ne garantit plus le contrat
   silicium, parce que l'auditeur ne peut plus distinguer les routines
   auditées des routines exemptées.

Les routines qui ont besoin d'un padding particulier (cushions cross-JSR,
sync VBlank entry pad, cross-caller cushion en début d'`init_vdp_g*`)
doivent inliner explicitement leur `JSR tms9918_pad{12,40}`. Le patcher
détecte ces pads manuels via `is_existing_pad` et n'injecte pas par-dessus.

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

POM1 `--preset 7 --terminal --silicon-strict`, `4000R`, choisir QWERTY :
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
