# GEN2Vectors — sprites XOR rapides + vectoriel + HUD (double buffering)

*[← POM1 documentation index](../../../../doc/README.md)*

Démo **C** qui réunit cinq briques de `dev/lib/gen2c` et montre comment animer
plein écran vite sur la carte GEN2 : **quatre balles** (une grosse 48×48, trois
petites 16×16) rebondissent dans un cadre **et s'entrechoquent** (toutes les
paires), avec un compteur de rebonds en HUD et une légende en police 8×8.

Briques :

- **Sprites XOR rapides (B)** — `gen2_hgr_blit7(..., GEN2_XOR)`. Les balles sont
  pré-empaquetées en **7px/octet** (le format du framebuffer), donc le blit XOR
  des **octets entiers** au lieu de pixels. Effacement = re-blit XOR au même
  endroit (fond restauré pixel-parfait). Contrepartie : x aligné sur 7px (pas
  horizontal de 7).
- **Double buffering (C)** — `gen2_set_draw_page` / `gen2_show_page` : on dessine
  la frame dans la page cachée puis on la bascule.
- **Nombre HUD + texte 8×8 (D)** — `gen2_hgr_putu_field` (compteur largeur fixe
  auto-effaçant) + `gen2_hgr_puts8` (légende dense en police native 8×8).
- **Vectoriel (E)** — `gen2_hgr_rect` + `gen2_hgr_line` pour le décor.

## Les trois leviers de vitesse

1. **Pas de redessin du décor** : cadre, séparateur, label, légende tracés UNE
   FOIS par page.
2. **Effacement XOR** des balles : pas de boîte d'effacement à laver.
3. **Blit aligné-octet** (`gen2_hgr_blit7`) : ~7× moins d'écritures que le blit
   pixel-par-pixel. Mesuré sur une scène (grosse + petite balle) : **42 → 223
   frames** pour un même budget de cycles, soit **×5.3**.

## Collision balle-balle

Pour chaque paire : si les centres sont à moins de `R_i + R_j` **et** se
rapprochent (`dx·Δvx + dy·Δvy < 0`), on échange les vitesses. Toutes les balles
vont à la même vitesse, donc l'échange préserve l'alignement octet.

## Build / Run

```sh
make            # -> "software/Graphic HGR/GEN2Vectors.bin" (+ .txt Woz-hex)
make clean
```

```sh
build/POM1 --preset 8 \
    --load 6000:"software/Graphic HGR/GEN2Vectors.bin" --run 6000
```

ou DevBench → POM1 Bench → cible *C / GEN2 HGR*, coller, compiler, uploader.
