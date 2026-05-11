# rogue_c — port C (cc65) de TMS_Rogue

Binaire CodeTank @ `$4000` (comme les autres démos `apple1-videocard-lib`), sortie dans `software/Apple-1_TMS_CC65/rogue_c.{bin,txt}`.

## Déjà porté (aligné sur `dev/projects/tms9918_rogue/TMS_Rogue.asm`)

- Grille 16×10, `map_buffer` 80 octets bit-packed ; constantes `TILE_*`, `PLAY_*`, `check_collision` (portes bord passables, escaliers montants bloqués).
- Rendu `render_map` : tuiles 2×2 caractères, table `tile_char_base`, pièges cachés masqués comme en ASM.
- `vis_buffer` : ici toujours **tout éclairé** (`0xFF`) — pas de `compute_fov` / torche.
- PRNG 16 bits Galois (`$B400`) et `rand_mod` ; `gen_dungeon` (grande salle ou deux salles + couloir en L) ; `finalize_doors` (3 passes) ; portes périmètre ; wrap bord (`trans_mode` 2–5 + `apply_wrap_spawn`) ; descente (`trans_mode==1`, profondeur++).
- Tileset + couleurs : générés depuis `tileset_rogue.inc` via `tools/rogue_tileset_inc_to_c.py` → `rogue_gfx_data.c`. Motif sprite joueur = `char_paladin2_pat` (32 octets) inliné dans le script et le `.c`.

## Pas encore porté (jeu ASM complet)

Monstres, combat, FOV ombre, inventaire, lancer de dagues, boss niveau 13, pits, objets au sol, écrans titre/aide, timings `tms9918_pad12` au niveau de l’ASM.

## Contrôles

- **H J K L** : ouest / sud / nord / est  
- **Escalier `>`** (tuile descente) : nouveau niveau, profondeur +1  
- **Porte sur le bord de l’écran** : wrap « grande salle » + respawn opposé (comme l’ASM)  
- **Q** : retour Wozmon  

## Init VDP (piège)

`screen1_prepare()` remplit toute la **pattern table** à zéro et impose une **colour table** par défaut. Il faut l’appeler **avant** `tms_copy_to_vram(rogue_tileset…)` et `rogue_color_table`, sinon le donjon reste invisible (seuls les sprites, hors table de motifs, restent corrects).

## Build

```bash
cd dev/apple1-videocard-lib/demos/rogue_c
make          # régénère rogue_gfx_data.c : make gfx && make
```

POM1 : preset 8 (TMS9918 + CodeTank), charger `rogue_c.txt` ou `rogue_c.bin`, puis **`4000R`**.
