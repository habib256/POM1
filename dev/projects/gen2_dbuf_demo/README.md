# GEN2DBuf — double buffering (PAGE2) sur carte GEN2

Petit programme **C** qui montre le **double buffering** de la carte couleur GEN2
d'Uncle Bernie : un bloc 16×16 rebondit sur tout l'écran HIRES 280×192, **sans
scintillement ni déchirure**.

La carte a deux framebuffers HIRES — page 1 (`$2000`) et page 2 (`$4000`). Au
lieu de dessiner sur la page affichée (on verrait l'image à moitié tracée), on
dessine la frame suivante dans la page **cachée** puis on bascule l'affichage
dessus. Le spectateur ne voit que des images complètes.

API utilisée (voir `dev/lib/gen2c/gen2.h`) :

- `gen2_set_draw_page(p)` — choisit où écrivent **toutes** les primitives (1 ou 2).
  À poser **une fois par frame** : ça re-dérive les tables de scanlines (les
  chemins par pixel, eux, ne changent pas d'un cycle).
- `gen2_show_page()` — affiche la page de dessin courante (commutateur `$C254/$C255`).

La boucle : `set_draw_page(cachée)` → effacer + dessiner la frame → `show_page()`
→ alterner la page. Le mode (graphics/hires/full) est ré-asserté à chaque frame,
ce qui couvre aussi le branchement différé de la carte par le DevBench.

## Build

```sh
make            # -> "software/Graphic HGR/GEN2DBuf.bin" (+ .txt Woz-hex)
make clean
```

Le `Makefile` reprend l'invocation `cl65` du POM1 Bench : config linker
`dev/cc65/apple1_gen2_c.cfg` (code + pile C en `$6000-$BEFF`, au-dessus des **deux**
framebuffers GEN2 `$2000`/`$4000`) + `gen2.c` + `gen2_blit.s` +
`apple1io.c`/`apple1io_asm.s`, origine `$6000`.

## Run

- **DevBench → POM1 Bench** : nouveau sketch, cible *C / GEN2 HGR*, coller le
  source, compiler, uploader.
- **CLI** :
  ```sh
  build/POM1 --preset 12 \
      --load 6000:"software/Graphic HGR/GEN2DBuf.bin" --run 6000
  ```

## Réglage

- `STEP` (pixels/frame) accélère ou ralentit le bloc.
- `BALL` (taille du bloc) ; bornes de rebond `XMAX`/`YMAX` calculées dessus.

Le double buffering marche aussi en **LORES** (`gen2_set_draw_page` redirige les
primitives HIRES *et* LORES) ; ici on illustre le HIRES, le plus parlant.
