# GEN2Lores — démo couleur LORES 40×48 sur carte GEN2

*[← POM1 documentation index](../../../../doc/README.md)*

Petit programme **C** pour la carte couleur GEN2 d'Uncle Bernie qui montre le
mode **LORES** : une grille **40×48 de blocs 7px×4px, 16 vraies couleurs**.
Contrairement au HIRES (où la couleur est un artefact NTSC du motif de bits),
LORES a une **vraie couleur par bloc** — propre, sans demi-densité ni masque
porteur. La démo :

1. affiche les **16 couleurs** de la palette en barres verticales,
2. trace un **cadre blanc** tout autour de l'écran,
3. dessine une **diagonale arc-en-ciel** bloc par bloc,

puis garde l'image en ré-assertant le mode LORES en boucle (instantané — ça
couvre aussi le branchement *différé* de la carte par le DevBench).

C'est un exemple minimal de la cible **« Uncle Bernie GEN2 HGR (C) »** : même
runtime `dev/lib/gen2c` (`gen2.c` gère HIRES *et* LORES) + base texte
`dev/lib/apple1c` (`woz_mon`). API LORES utilisée :
`gen2_lores_init` / `gen2_lores_clear` / `gen2_lores_setblock` /
`gen2_lores_hlin` / `gen2_lores_vlin` (voir `dev/lib/gen2c/gen2.h`).

## Build

```sh
make            # -> "software/Graphic HGR/GEN2Lores.bin" (+ .txt Woz-hex)
make clean
```

Le `Makefile` reprend l'invocation `cl65` du POM1 Bench : config linker
`dev/cc65/apple1_gen2_c.cfg` (code + pile C en `$6000-$BEFF`, au-dessus des
framebuffers GEN2) + `gen2.c` + `gen2_blit.s` + `apple1io.c`/`apple1io_asm.s`,
origine `$6000`. La page LORES partage la RAM de la page texte (`$0400-$07FF`).

## Run

- **DevBench → POM1 Bench** : nouveau sketch, cible *C / GEN2 HGR*, coller le
  source, compiler, uploader.
- **CLI** :
  ```sh
  build/POM1 --preset 11 \
      --load 6000:"software/Graphic HGR/GEN2Lores.bin" --run 6000
  ```
- Charger le `.bin`/`.txt` depuis le dossier `software/Graphic HGR/` auto-branche
  la carte GEN2 et ouvre sa fenêtre (préréglage 12 = Apple-1 + GEN2 HGR).

## Vérification headless

Le rendu se contrôle sans GUI (mêmes pixels que l'affichage) :

```sh
build/POM1 --preset 11 \
    --load 6000:"software/Graphic HGR/GEN2Lores.bin" --run 6000 \
    --dump-after-cycles 2000000 --dump-gen2-frame /tmp/lores.png
```

→ PNG 280×192 : 16 barres de couleurs nettes, cadre blanc (bords haut/bas/gauche/
droite à 255,255,255), diagonale arc-en-ciel. Zéro artefact NTSC.
