# GEN2Countdown — compteur 20 → 0 sur carte GEN2 HGR

*[← POM1 documentation index](../../../../doc/README.md)*

Petit programme **C** pour la carte couleur GEN2 d'Uncle Bernie : il affiche un
grand chiffre centré sur l'écran HIRES 280×192 et décompte de **20 à 0** (un
nombre par ~seconde), affiche `LIFTOFF!` à zéro, puis **rend la main au WOZ
Monitor** (le prompt `\` sur l'écran texte de l'Apple-1).

C'est un exemple minimal de la cible **« Uncle Bernie GEN2 HGR (C) »** : il
s'appuie sur le runtime `dev/lib/gen2c` (`gen2_hgr_init` / `gen2_hgr_clear` /
`gen2_hgr_puts` / `gen2_hgr_putu`) et la base texte `dev/lib/apple1c`
(`woz_mon`). Aucune télémétrie.

## Build

```sh
make            # -> "software/Graphic HGR/GEN2Countdown.bin" (+ .txt Woz-hex)
make clean
```

Le `Makefile` reprend l'invocation `cl65` du POM1 Bench : config linker
`dev/cc65/apple1_gen2_c.cfg` (code + pile C en `$6000-$BEFF`, au-dessus des
framebuffers GEN2) + `gen2.c` + `apple1io.c`/`apple1io_asm.s`, origine `$6000`.

## Run

- **DevBench → POM1 Bench** : nouveau sketch, cible *C / GEN2 HGR*, coller le
  source, compiler, uploader.
- **CLI** :
  ```sh
  build/POM1 --preset 12 \
      --load 6000:"software/Graphic HGR/GEN2Countdown.bin" --run 6000
  ```
- Charger le `.bin`/`.txt` depuis le dossier `software/Graphic HGR/` auto-branche
  la carte GEN2 et ouvre sa fenêtre (préréglage 12 = Apple-1 + GEN2 HGR).

## Réglage de la cadence

La vitesse du décompte est une boucle d'attente CPU grossière, `#define
TICK_SPINS 55000u` dans `GEN2Countdown.c` (~1 s à ~1 MHz). Augmenter pour
ralentir, diminuer pour accélérer.
