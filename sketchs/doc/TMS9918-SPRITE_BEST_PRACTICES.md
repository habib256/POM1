## TMS9918 — Sprite Best Practices (cc65 / 6502 ASM checklist)

Cette checklist est le complément opérationnel de [`TMS9918-SPRITE_INIT.md`](TMS9918-SPRITE_INIT.md)
(qui couvre la théorie : matériel, terminator `$D0`, EC, limite 4/ligne, etc.).
Elle énumère les règles que **tout** projet TMS9918 du dépôt (Galaga, Rogue,
Sokoban, Snake, Chess, Asteroids, Connect4, Life, OrbitalPool, démos
CodeTank) **doit** appliquer pour éviter les classes de bugs déjà
diagnostiquées sur silicium et sur POM1 silicon-strict.

> **Référence canonique pour la sémantique** : [`TMS9918-SPRITE_INIT.md`](TMS9918-SPRITE_INIT.md).
> **Référence canonique pour le timing** : `Programming_TMS9918.md` §17 et §25,
> et `dev/lib/tms9918/tms9918_pad.asm`.
> **Validation silicium réel** : Replica-1 1:1 Parmigiani, 2026-05-12 (vidéo).

---

### 1. Initialisation — séquence canonique

Toujours dans cet ordre, **sans exception** :

1. **Forcer R1 display=OFF** (bit 6 = 0) avant tout upload VRAM.
   En Mode I + sprites, le slot CPU descend à ~6 µs ; en display-OFF il
   passe à ~2 cycles. Les bursts (pattern table, name table, SAT) coûtent
   ~10× moins de drops `[TMS9918 DROP]`.

2. **Programmer les 8 registres** VR0..VR7. R1 doit être écrit en
   display-OFF (`AND #$BF`) pendant la boucle ; on le ré-arme à la fin.

3. **Pré-conditionner la SAT** avec le pattern « gold-standard » :
   `SAT[0].Y = $D0` (chain terminator) + `SAT[1..127] = $D1`
   (Y off-screen, NON terminator). Si SAT[0] est plus tard écrasé par
   un sprite réel, SAT[1].Y = $D1 garantit qu'aucun résidu de VRAM
   non initialisée ne s'affiche.
   Voir `dev/lib/tms9918/tms9918m1.asm::disable_sprites` ($D0 + 127×$D1).

4. **Uploader la pattern table, color table, SPGT, name table** —
   tout cela display-OFF.

5. **Ré-armer R1 display=ON** avec la valeur table (souvent `$C0` pour
   8×8, `$C2` pour 16×16). Le frame visible est désormais propre — pas
   de flash de bruit VRAM.

Anti-pattern à proscrire :
- `SAT[0].Y = $D0` seul, sans pré-remplir SAT[1..127] de `$D1`. Marche
  tant que le premier `render_sprites` rejoint un terminator avant un
  slot d'index élevé, mais casse silencieusement si une routine partielle
  s'arrête tôt et expose le bruit boot.
- Display ON pendant les uploads. Génère des drops `[TMS9918 DROP]` en
  mode strict POM1 et un flash de bruit sur silicium réel.

Routine canonique partagée : `init_vdp_g1` (lib `tms9918m1.asm`) — fait
1 → 5 sauf l'étape 4 qui est project-specific. Tout nouveau projet
**doit** l'appeler plutôt que rouler sa propre boucle de registres.
Mode 2 : `init_vdp_g2` (lib `tms9918m2.asm`).

### 2. SAT — terminator et sentinelles

- **`Y = $D0` (208)** est le **chain terminator** : le chip arrête le
  scan SAT, les entrées suivantes sont ignorées **quel que soit leur
  contenu**. C'est la seule valeur Y qui ait ce rôle.
- **`Y ∈ [$C0..$CF]` ou `Y ≥ $D1`** = off-screen mais le scan continue.
  Choix conventionnel du dépôt :
  - `HIDDEN_Y = $C0` (192) — slot scanné mais invisible (sprite slot
    masquable dans une chaîne).
  - `$D1` — sentinelle de pré-remplissage (toutes slots initiales hors
    code utile).
- Toute routine `render_*` qui rebuild la SAT **doit** se terminer par
  l'écriture de `Y = $D0`. Pas de `RTS` sans terminator.
- Optimisation : on n'a **pas** besoin d'écrire X / motif / couleur après
  le $D0 — le chip n'y touche pas. Galaga et Rogue exploitent cela
  (économie de 3 writes/frame).

### 3. Couleur — masque défensif

- Octet 3 de la SAT : bits 0..3 = couleur (0 = transparent), bit 7 =
  Early Clock, bits 4..6 ignorés.
- **Toute couleur > 15 active EC** et fait sauter le sprite de −32 px
  → effet « fantôme à droite » classique.
- Sources sûres : constantes `COL_*` 0..15 (toutes celles du dépôt
  passent l'audit 2026-05).
- **Best practice défensive** : si la couleur sort d'une table runtime
  modifiable (RAM, ZP), masquer `AND #$0F` juste avant `STA VDP_DATA`.
  Coût : 2c, négligeable. Élimine définitivement la régression « bit 7
  fuit dans la couleur » (cf. §5.3 de SPRITE_INIT.md).

### 4. Motif — multiple de 4 pour 16×16

- En 16×16 (VR1 bit 1 = 1), le motif occupe **4 blocs 8×8 contigus**.
  Les 2 bits bas du numéro de motif sont **ignorés** par le silicium :
  motifs valides = 0, 4, 8, 12, …
- Tous les `SPRITE_NAME_*` / `P_*` du dépôt respectent déjà cette
  contrainte. Tout nouvel ajout doit aussi.
- **Best practice défensive** : pour un nom de motif issu d'une table
  runtime, `AND #$FC` juste avant `STA VDP_DATA` documente
  l'invariant et neutralise toute corruption. Coût : 2c.

### 5. 4 sprites par ligne

- Le chip ne prépare que **4 sprites par scanline** ; le 5e + ne sont
  pas dessinés sur cette ligne et **5S** (bit 6 du status) se lève.
- Layout HUD : ne **jamais** mettre 5+ sprites au même Y. Si la HUD a
  des icônes (Rogue: WPN/ARM/RNG/TRC à `y=176`), c'est exactement 4.
  Une 5e icône doit aller à un autre Y (Rogue: dagger ammo à `y=160`,
  rangée séparée).
- L'ordre SAT compte : indices bas = priorité écran + priorité ligne.
  Le 5e+ sur une ligne est l'entrée SAT d'index le plus élevé.
  Multiplexer en VBlank pour 5+ sprites mobiles sur la même ligne, ne
  **pas** essayer de l'absorber côté émulateur.

### 6. Reconstruction SAT — VBlank gating

Toute routine qui réécrit la SAT en cours de jeu doit :
1. `WAIT_VBLANK` (macro `tms9918.inc`) — drain stale F flag puis
   attendre F=1.
2. Set write addr `$1B00` (`STA VDP_CTRL` × 2 avec padding strict).
3. Écrire **tous** les slots utiles dans l'ordre (la priorité = ordre).
4. Terminer par `Y = $D0`.

Budget VBlank Mode I NTSC 60 Hz : ~4 554 cycles CPU. Largement
suffisant pour ~22-32 entrées même avec `JSR tms9918_pad18` entre
chaque écriture.

Anti-pattern : SAT rebuild en active-display sans gating. Génère du
*tearing* + des drops + des sprites half-rendered sur une frame.

### 7. Silicon-strict padding (POM1 + silicium réel)

- **8 cycles minimum** entre deux STA VDP_*. POM1 silicon-strict
  applique le contrat ; le silicium TI le respecte aussi sous certains
  phase alignments.
- Pads canoniques : `JSR tms9918_pad18` (12c, 3 octets, plus dense que
  6× NOP). Disponible dans `dev/lib/tms9918/tms9918_pad.asm`.
- Macros prêtes : `WRT_DATA_REG` / `WRT_DATA_VAL` (`tms9918.inc`) —
  encapsulent `STA VDP_DATA` + `JSR tms9918_pad18`.
- Cushion cross-routine : avant la **première** `STA VDP_CTRL/DATA`
  d'une routine (caller-gap), poser un `JSR tms9918_pad18` défensif.
  L'auto-patcher ne voit pas les frontières JSR/RTS.

### 8. Polling F flag — pas d'IRQ

- Sur P-LAB la broche `/INT` du TMS9918 **est câblée** au `/IRQ` du 6502
  (trace vérifiée par Parmigiani), mais le polling `BIT $CC01 / BPL` reste
  le pattern recommandé (plus simple, indépendant du flag I).
- L'IRQ-on-VBlank fonctionne (R1 bit 5 = 1 + `CLI` + handler au vecteur
  `$FFFE` lisant `$CC01` atomiquement), mais le polling l'évite.
- Effet collatéral : lire `$CC01` clear F, 5S, C — donc lire le status
  **avant** la macro `WAIT_VBLANK` si on dépend de 5S ou C.

### 9. Audit avant merge

Pour tout nouveau code touchant à des sprites, vérifier :
- [ ] R1 bit 6 = 0 pendant les uploads VRAM.
- [ ] `disable_sprites` (ou équivalent inline `$D0 + 127×$D1`) appelé
      avant la première frame visible.
- [ ] Toutes les valeurs `MON_COLOR` / `enemy_color` / `popup_color`
      bornées 0..15 par construction (ou masquées `AND #$0F`).
- [ ] Tous les `SPRITE_NAME_*` / `P_*` 16×16 multiples de 4.
- [ ] Routine `render_*` se termine sur `Y = $D0`.
- [ ] HUD : pas de groupe 5+ sprites au même Y.
- [ ] `render_*` body wrappé par `WAIT_VBLANK`.
- [ ] Pads `JSR tms9918_pad18` entre `STA VDP_*` consécutifs (ou via
      macros `WRT_DATA_*`).
- [ ] Pas de `CLI` + `WAI` en attente d'IRQ TMS9918.

### 10. Projets de référence

- **Init canonique** : `sketchs/tms9918/game_rogue/TMS_Rogue.asm`
  (utilise `init_vdp_g1` + `vdp_display_off` + uploads + `override_r1_16x16`).
- **SAT rebuild VBlank-gated** : `TMS_Rogue.asm::place_all_sprites`,
  `TMS_Galaga.asm::render_sprites`.
- **Defensive SAT fill** : `dev/lib/tms9918/tms9918m1.asm::disable_sprites`
  et `tms9918m2.asm::disable_sprites`.
- **HUD 4-par-ligne** : `TMS_Rogue.asm` timers (y=176) vs dagger ammo
  (y=160).

### 11. Workflow Claude

Cette checklist est l'objet d'une mémoire `feedback_tms9918_sprite_practices`.
Avant d'écrire ou de modifier du code sprite TMS9918, relire cette page
et vérifier la conformité point-par-point. Toute déviation doit être
documentée dans un commentaire bloc au-dessus du code concerné.
