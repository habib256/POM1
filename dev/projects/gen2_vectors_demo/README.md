# GEN2Vectors — primitives vectorielles + nombres HUD (double buffering)

Petit programme **C** qui réunit trois briques de `dev/lib/gen2c` ajoutées pour
les jeux GEN2 : une balle (**cercle**) rebondit dans un **cadre** (contour de
rectangle), avec un **compteur de rebonds** en HUD largeur fixe, le tout en
**double buffering** → animation fluide, compteur sans scintillement.

Briques montrées :

- **Vectoriel (E)** — `gen2_hgr_rect`, `gen2_hgr_circle`, `gen2_hgr_line`
  (Bresenham ; les tracés H/V passent par le chemin rapide `pixrect`).
- **Nombre HUD (D)** — `gen2_hgr_putu_field(x,y,value,width)` : champ largeur
  fixe, aligné à droite, qui **efface exactement sa propre boîte** avant de
  réécrire. Un compteur qui change de largeur (9 → 10 → 100…) ne laisse aucun
  chiffre fantôme et n'a pas besoin d'un `clear_pixrect` externe (lequel, mal
  dimensionné, mordait sur le label voisin — le bug du score de HGR Snake).
  Voir aussi `gen2_hgr_puti` (signé) et `gen2_hgr_putx` (hexa).
- **Double buffering (C)** — `gen2_set_draw_page` / `gen2_show_page` : on dessine
  la frame dans la page cachée puis on la bascule.

## Build

```sh
make            # -> "software/Graphic HGR/GEN2Vectors.bin" (+ .txt Woz-hex)
make clean
```

Le `Makefile` reprend l'invocation `cl65` du POM1 Bench (config linker
`dev/cc65/apple1_gen2_c.cfg`, `gen2.c` + `gen2_blit.s` + `apple1io.*`, origine
`$6000`).

## Run

```sh
build/POM1 --preset 12 \
    --load 6000:"software/Graphic HGR/GEN2Vectors.bin" --run 6000
```

ou DevBench → POM1 Bench → cible *C / GEN2 HGR*, coller, compiler, uploader.

## Note couleur

Sur les traits d'**un pixel** de large, on voit du frangé violet/vert : c'est
l'artefact NTSC normal du HIRES (la couleur y est un effet du motif de bits, pas
une vraie couleur par pixel). Pour des traits blancs francs, dessiner épais (2
px) ; pour de la vraie couleur par bloc, voir le mode **LORES** (`gen2_lores_*`).
