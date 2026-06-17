# apple1-videocard-lib (cc65, CodeTank uniquement)

*[← POM1 documentation index](../../doc/README.md)*

Port C **cc65** de la bibliothèque originale **[nippur72/apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib)** par **Antonino "Nino" Porcino** (KickC). Toute amélioration sous cet arbre conserve l'attribution upstream (header dans chaque `.c` / `.h` / `.s`) — voir [Licence / attribution](#licence--attribution).

Cible POM1 : **P-LAB CodeTank**, image ROM **16 Ko @ `$4000-$7FFF`**, démarrage **Wozmon `4000R`**, preset **7** (TMS9918 + CodeTank).

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
make            # … + tetris, text_adventure, sprite_animals, chrome_dino (voir tableau)
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
| `chrome_dino` | (clone T-Rex hors-ligne) | Mini-jeu saut + obstacles (dans la cible `make all`) |
| `frogger_codetank` | (inspiré Frogger) | Grenouille en sprite matériel, eau animée — `make` local |
| `rogue_c` | `dev/projects/tms9918_rogue/` | Port C partiel de TMS_Rogue — `make` local (voir `demos/rogue_c/README.md`) |

Non portés ici (KickC / `.c` volumineux / matériel autre) : `anagram`, `tapemon`, `sdcard`, `montyr`, `life-src`, `iec`, `viatimer` (le dépôt amont reste la reference pour ces demos).

## Test dans POM1

1. Preset **7** (Apple-1 + TMS9918 + CodeTank).
2. **Fichier → Charger la mémoire** depuis `software/Apple-1_TMS_CC65/` (auto-branchement TMS9918), ou coller le `.txt`, puis **`4000R`**.

## Modules `lib/`

### Base (port direct upstream)

| Fichier        | Rôle |
|----------------|------|
| `utils.h`      | Types `byte`/`word`, `PEEK`/`POKE`, délai I/O TMS |
| `tms9918.*`    | Registres / VRAM (`$CC00`/`$CC01`) |
| `apple1.*` + `apple1_asm.s` | Wozmon ECHO / PRBYTE / clavier |
| `screen1.*`    | Mode texte TMS (écran 1) |
| `screen2.*`    | Bitmap (écran 2) ; `screen2_ellipse_rect` en **C** (64 segments, tables cos/sin, segments = `screen2_line`) — plus de fichier `screen2_ellipse.s` requis |
| `sprites.*`    | Attributs sprites (écriture directe VRAM) |
| `interrupt.*`  | Stubs (pas d’IRQ TMS câblée dans ce port) |
| `via.*`        | Symboles VIA `$A000` (microSD — inchangé vs upstream) |
| `c64font.c`    | Police 8×8 (768 octets) dérivée de l’upstream |

### Extensions POM1 (au-delà de l'upstream)

Ces modules sont des ajouts spécifiques à ce port ; le code reste fidèle à l'esprit Nino (KickC) mais sort de l'arbre upstream. Chacun est opt-in via `SOURCES` du `Makefile` de la démo, donc les démos historiques continuent de compiler sans changement.

| Fichier             | Rôle |
|---------------------|------|
| `tms_fast.s`        | **ca65 fast-paths VRAM** — `tms_fill_vram(addr,val,count)`, `tms_copy_to_vram_fast(src,size,dest)`, `tms_shadow_flush()`. Pas de `TMS_IO_DELAY` per-byte (cadence KickC upstream). |
| `sprite_shadow.*`   | **Pattern shadow SAT** (cf. `doc/TMS9918-SPRITE_INIT.md §3.2 / §6) — 128 octets `tms_sprite_shadow[]` en RAM, API `tms_shadow_set/move/clear/set_terminator`, flush blocs en VBlank via `tms_shadow_flush`. |
| `random.*`          | LFSR 8 bits (période 255) + Galois 16 bits (période 65535) — `rand8`, `rand16`, `srand8/16`, `rand8_below(limit)`. |
| `vsync.*`           | Compteur de frames polling (`tms_wait_end_of_frame` → `vsync_frames`) — base temps ~60 Hz NTSC en l'absence de câblage IRQ TMS. |
| `printlib.*`        | Helpers décimal / hex via pointeur de fonction `putc` ; wrappers Wozmon (`woz_print_dec_u8/u16`, `woz_print_hex_u16`) et écran 1 (`screen1_print_*`). |
| `screen_ext.c`      | Helpers étendus opt-in : `screen1_putcharxy(x,y,c)`, `screen1_fill_color_attr(c)`, `screen2_clear()`, `screen2_filled_rect(x0,y0,x1,y1)`. Les deux derniers tirent `tms_fast.s`. |

### Exemple d'opt-in dans un `Makefile` de démo

```make
SOURCES := main.c \
    $(LIBDIR)/apple1_asm.s \
    $(LIBDIR)/tms9918.c \
    $(LIBDIR)/sprites.c \
    $(LIBDIR)/tms_fast.s \
    $(LIBDIR)/sprite_shadow.c \
    $(LIBDIR)/vsync.c \
    $(LIBDIR)/random.c
```

Puis dans le `main.c` :

```c
#include "apple1_videocard_lib.h"   /* umbrella : tire tous les modules */

void main(void) {
    tms_init_regs(SCREEN1_TABLE);
    screen1_prepare();
    screen1_load_font();
    tms_shadow_init();
    srand16(0xACE1U);

    for (;;) {
        unsigned char i;
        for (i = 0; i < 4; ++i) {
            tms_sprite s;
            s.y = (signed char)(20 + (rand8() & 0x3F));
            s.x = (unsigned char)(rand8());
            s.name = (unsigned char)(i * 4);
            s.color = (unsigned char)((i & 0xF) | 0x10); /* EARLY_CLOCK */
            tms_shadow_set(i, &s);
        }
        tms_shadow_set_terminator(4);
        vsync_wait();        /* attendre fin-de-frame */
        tms_shadow_flush();  /* 128 B VRAM en burst, pas de tearing */
    }
}
```

## Licence / attribution

Code **dérivé** du dépôt **[nippur72/apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib)** par **Antonino "Nino" Porcino** (alias *nippur72* sur GitHub). À ma connaissance le dépôt upstream **ne déclare pas de licence** (vérifié 2026-05) : à ce titre la redistribution hors arbre POM1 doit obtenir l'accord de l'auteur. Chaque `.c` / `.h` / `.s` de `lib/` porte un en-tête d'attribution — **ne pas le retirer** dans les forks. Les modules d'extension ci-dessus sont des contributions POM1 et conservent la même mention par respect de la chaîne dérivée.

## Écarts KickC → cc65

- Pas de pragmas KickC, pas d’`asm { }` : routines sensibles en **`apple1_asm.s`** (ca65).
- Pas de `static inline` cc65 : `tms_read_status` → macro.
- `screen2_ellipse_rect` : **C** — approximation paramétrique (64 cordes) ; nécessite le **mode screen 2** (`tms_init_regs(SCREEN2_TABLE)` + `screen2_init_bitmap`…).
- `install_interrupt` : stub — ne modifie pas la zone reset `$0000` sur Apple-1 réel.
