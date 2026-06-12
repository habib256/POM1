# A-1-CrazyCycle — démo beam-raced de la carte GEN2 *release* d'Uncle Bernie

Démo de validation des soft switches `$C250-$C257` + drapeau HST0 de la carte
release (spec : `doc/ColorGraphicsCard_doc_for_Arnaud.pdf`, transcrite dans
`doc/GEN2_RELEASE_questions.md`), avec **musique 2 voix par l'ACI**. C'est le
livrable « démo de validation Bernie » du TODO Phase 5.

## Déroulé

1. **Init du latch** — l'état power-on des switches est *indéterminé* sur les
   vrais PLD et le RESET Apple-1 n'y touche jamais (Q8) : le logiciel
   initialise MIX_OFF, PAGE_ONE, HIRES par **lectures** (les switches sont
   read-only : une lecture bascule + retourne HST0 en D7, une écriture est
   ignorée).
2. **TEXT** — la page texte 1 ($0400) est remplie avec
   `Uncle Bernie HGR COLOR CARD` répété sur les 40×24, puis révélée
   (`$C251`) pendant ~3 s.
3. **Image UBERNIE** — l'image HGR `sdcard/NONO/HGR/UBERNIE#062000` est
   intégrée au build comme **seconde zone hex à $2000** du `.txt`
   (dump Wozmon multi-zones via `emit_woz.py extra_zones`) : elle est déjà
   dans le framebuffer au lancement. Révélée par `$C250` (TEXT off → HIRES),
   ~3 s. (Le `.bin` seul affiche le bruit DRAM power-on de la carte.)
4. **Fenêtre beam-raced rebondissante + musique** — un rectangle de
   168×64 px au travers duquel on voit la page TEXT, qui se déplace en
   rebondissant sur les quatre bords : sur chaque scanline de la bande,
   `LDA $C251` est lue au cycle exact où le faisceau entre dans la fenêtre
   et `LDA $C250` au cycle exact où il en sort (split horizontal
   mid-scanline — la fonctionnalité phare de la carte release) — pendant
   qu'un chiptune 2 voix sort du TAPE OUT de l'ACI. Une touche → Wozmon.

## Synchronisation cycle-exacte (60 Hz : 262 lignes × 65 cycles = 17030)

Trois étages, détaillés en commentaires dans le source :

1. **WAITVBL** (grossier) — détection du VBL par double échantillonnage ORé
   de HST0 (le notch burst de 3 cycles, hcnt 13-15, ne peut pas mettre à
   zéro deux lectures espacées de 4 cycles — Listing 1 de Bernie) ; un blank
   qui persiste au-delà des 25 cycles d'un H-blank est le V-blank.
2. **Scan de phase** (exact à ±0 cycle) — un échantillon HST0 tous les
   66 cycles (une ligne + 1) glisse de +1 cycle par ligne le long du
   scanline ; le 4ᵉ zéro consécutif identifie le front H-blank→live :
   son accès bus a touché hcnt 28 exactement (zéros à 25,26,27,28). Le
   compteur de zéros démarre empoisonné ($80) pour qu'un démarrage en plein
   scan vivant ne déclenche jamais de faux verrouillage, et le scan tourne
   lignes ~5-80, loin du V-blank.
3. **Verrouillage ligne** — phase horizontale connue, on échantillonne
   hcnt 45 (jamais blanké, jamais dans le burst) toutes les 65 cycles :
   0 → lignes vivantes, 1 → V-blank, premier 0 suivant = ligne 0.

Ensuite la boucle de frame tourne en **free-run à exactement 17030
cycles/frame**, jamais resynchronisée — zéro jitter.

## Fenêtre mobile à coût constant

- **Vertical** : burners pré/post-fenêtre à nombre de lignes variable
  (somme constante : `vpos` + `195-vpos`). `vpos` = vtab[fcnt], forme
  d'onde par morceaux (traversée complète, double rebond rapide à
  mi-hauteur, retour, petit rebond en haut — période 256 frames).
- **Horizontal** : chaque scanline traverse deux glissières de 8 NOP via
  `JMP (ind)` ; les offsets d'entrée (8-H et H) se compensent — délais
  2H et 16-2H, somme constante. `hoff` = htab[hidx], **compteur séparé à
  période 192** (wrap par branchement équilibré) : lcm(256,192) = 1536
  frames ≈ 25,6 s avant que la trajectoire combinée ne se répète.
- Les branchements pris des boucles chronométrées sont verrouillés
  même-page par des `.assert` ld65 (deux « layout shims » ajustables) ; les
  tables sont page-alignées (lecture indexée à 4 cycles constants).

## Musique 2 voix sur l'ACI (Bernie Q7)

Tout accès `$C0xx` bascule le flip-flop TAPE OUT de l'ACI — la convention
SPEAKER `$C030` des ports Apple II (la carte release a déplacé ses switches
en `$C25x` précisément pour laisser `$C0xx` à l'ACI ; le preset 13 de POM1
branche l'ACI à côté de la GEN2, comme le PCB réel avec sa découpe jacks).

- **Tick par slot de scanline** : chaque slot de 65 cycles (lignes burner ET
  scanlines de la fenêtre — le tick loge dans le gap de 20 cycles entre
  TEXT_ON et TEXT_OFF, le compte à rebours passant dans Y) décrémente un
  compteur et bascule `$C030` à zéro. Demi-période = N slots →
  f = 7867/N Hz, justesse au cycle près.
- **2 voix sur 1 bit (polyphonie virtuelle)** : le séquenceur (branchless,
  index = fcnt>>2 dans une table de 64 notes) alterne BASSE et MÉLODIE
  toutes les 4 frames (~66 ms) — l'oreille sépare la walking bass
  (C3/G3 · A2/E3 · F2/C3 · G2/D3) des arpèges (I-vi-IV-V en Do). Boucle de
  4 mesures = 256 frames, en phase avec le rebond vertical.
- L'ACI **enregistre** le morceau en le jouant (comportement réel) :
  `--save-tape sortie.aci` à la fermeture de POM1 donne une cassette
  rejouable. La justesse a été vérifiée en analysant les durées d'impulsion
  de la cassette : plateaux exacts à N×65 cycles, séquence
  basse/mélodie conforme aux 4 mesures.

## Caveats

- **60 Hz uniquement** — laisser la checkbox « 50 Hz vertical » de la fenêtre
  HGR décochée (la boucle compte 17030 cycles ; en 312 lignes il en faudrait
  20280).
- **Matériel réel** : le free-run suppose 1 cycle CPU = 1 cycle vidéo — vrai
  sur les répliques SRAM (Briel) et dans POM1 ; sur un Apple-1 d'origine le
  refresh DRAM vole 4 cycles sur 65 et ferait dériver la boucle (caveat
  documenté par Bernie). Le poll HST0 lit `$C254` (PAGE_ONE) : le programme
  vit en page 1, donc le toggle du poll est toujours un no-op.
- `INC`/`DEC` zp évités dans le code chronométré (POM1 les compte à 4 cycles,
  le vrai 6502 à 5) — remplacés par LDA/ADC/STA, identiques des deux côtés.

## Build & run

```bash
make          # → software/Graphic HGR/A-1-CrazyCycle.{bin,txt}
```

POM1 : preset 13 (GEN2 HGR + ACI), charger `A-1-CrazyCycle.txt` (le dossier
`Graphic HGR/` auto-active la carte), puis `E000R`. Ou en CLI :

```bash
./build/POM1 --preset 13 --load 'E000:software/Graphic HGR/A-1-CrazyCycle.txt'
# (le .txt charge code + image ; --save-tape musique.aci pour garder le morceau)
```
