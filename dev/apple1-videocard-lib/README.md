# apple1-videocard-lib (cc65, CodeTank uniquement)

Port C **cc65** de la bibliothèque [nippur72/apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib) (KickC), aligné sur **P-LAB CodeTank** sous POM1 : image ROM **16 Ko @ `$4000-$7FFF`**, démarrage **Wozmon `4000R`**, preset **8** (TMS9918 + CodeTank).

## Carte mémoire (linker `cc65/codetank_c.cfg`)

| Région   | Adresse        | Rôle |
|----------|----------------|------|
| ZP       | `$0000-$00FF`  | cc65 ZEROPAGE |
| RAM basse | `$0280-$0FFF` | `.data` (run), pile C (`__STACKSTART__` = `$1000`) |
| RAM haute | `$E000-$EFFF` | `.bss` — banque haute dual-bank (« zone BASIC » libre tant que le BASIC n’y est pas cartographié) |
| ROM      | `$4000-$7FFF`  | `CODE`, `RODATA`, image load de `.data` |

Pas de variante programme en RAM `$0280` seul ; pas de cible Fantasy.

## Prérequis

- [cc65](https://cc65.github.io/) (`cl65`, `ca65`, `ld65`) dans le `PATH`.

## Build — toutes les démos

```bash
cd dev/apple1-videocard-lib
make            # … + tetris, text_adventure, sprite_animals (voir tableau)
```

Ou une démo seule :

```bash
cd dev/apple1-videocard-lib/demos/hello_screen1
make
```

Sorties (chaque démo) : `software/Apple-1_TMS_CC65/<nom>.{bin,txt}` — image 16 Ko (padding `$FF`), hex Wozmon + `4000R`.

| Démo (cc65) | Source upstream | Rôle |
|-------------|-------------------|------|
| `hello_screen1` | équivalent `hello-world` + TMS | Texte TMS écran 1 |
| `hello_world` | `demos/hello-world` | Message Wozmon, sans TMS |
| `checksum` | `demos/checksum` | Somme d’octets sur plage hex |
| `graphs` | `demos/graphs` | Bitmap écran 2 : cercle + ellipse |
| `demo_screen1` | `demos/demo/demo_screen1.h` | Démo texte + reverse + sprites + saisie |
| `picshow` | `demos/picshow` | Version stub (Screen 2 + dessin, pas la grosse image upstream) |
| `demo` | `demos/demo` | Menu minimal : `SCREEN1` / `SCREEN2` (autres options = stubs) |
| `tetris` | `demos/tetris` | Tetris écran 1 (jeu complet) |
| `text_adventure` | (inspiré Little Tower) | Aventure textuelle 32 colonnes |
| `sprite_animals` | `dev/lib/tms9918/sprites_fauna.asm` | 4 sprites 16×16 Fauna fixes, taille native (sans MAG×2) |

Non portés ici (KickC / `.c` volumineux / matériel autre) : `anagram`, `tapemon`, `sdcard`, `montyr`, `life-src`, `iec`, `viatimer` (le dépôt amont reste la reference pour ces demos).

## Test dans POM1

1. Preset **8** (Apple-1 + TMS9918 + CodeTank).
2. **Fichier → Charger la mémoire** depuis `software/Apple-1_TMS_CC65/` (auto-branchement TMS9918), ou coller le `.txt`, puis **`4000R`**.

## Modules `lib/`

| Fichier        | Rôle |
|----------------|------|
| `utils.h`      | Types `byte`/`word`, `PEEK`/`POKE`, délai I/O TMS |
| `tms9918.*`    | Registres / VRAM (`$CC00`/`$CC01`) |
| `apple1.*` + `apple1_asm.s` | Wozmon ECHO / PRBYTE / clavier |
| `screen1.*`    | Mode texte TMS (écran 1) |
| `screen2.*`    | Bitmap (écran 2) ; `screen2_ellipse_rect` en **C** (64 segments, tables cos/sin, segments = `screen2_line`) — plus de fichier `screen2_ellipse.s` requis |
| `sprites.*`    | Attributs sprites |
| `interrupt.*`  | Stubs (pas d’IRQ TMS câblée dans ce port) |
| `via.*`        | Symboles VIA `$A000` (microSD — inchangé vs upstream) |
| `c64font.c`    | Police 8×8 (768 octets) dérivée de l’upstream |

## Licence / attribution

Code dérivé du dépôt **nippur72/apple1-videocard-lib** ; conserver la mention dans les forks. Vérifier la licence du dépôt upstream pour redistribution hors arbre POM1.

## Écarts KickC → cc65

- Pas de pragmas KickC, pas d’`asm { }` : routines sensibles en **`apple1_asm.s`** (ca65).
- Pas de `static inline` cc65 : `tms_read_status` → macro.
- `screen2_ellipse_rect` : **C** — approximation paramétrique (64 cordes) ; nécessite le **mode screen 2** (`tms_init_regs(SCREEN2_TABLE)` + `screen2_init_bitmap`…).
- `install_interrupt` : stub — ne modifie pas la zone reset `$0000` sur Apple-1 réel.
