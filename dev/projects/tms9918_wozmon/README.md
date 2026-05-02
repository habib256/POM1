# TMS9918 WozMon — preuve de concept M1→M2

Remplacer l'écran Apple-1 d'origine (PIA `$D012`, busy-wait sur bit 7,
~16 ms par caractère) par le TMS9918 en **Text Mode F1 (40×24 monochrome,
mêmes dimensions que l'Apple-1)**.

Cette livraison est la **preuve de concept visuelle** (M1→M2 du plan) :

- driver Text Mode F1 mutualisé sous `dev/lib/tms9918/tms9918_text.asm`
- runtime console (CR / BS / wrap colonne 40 / scroll) sous
  `dev/lib/tms9918/tms9918_console.asm`
- démo autonome qui imprime, valide le wrap et le scroll, puis rend la
  main à Wozmon stock.

La redirection de `JSR $FFEF` vers la console TMS9918 — qui permettra de
lancer Chess, Connect4, Sokoban-text, etc. inchangés — est l'objet de la
livraison **M3** (patch Wozmon 256 octets) et **M4** (tests jeux).

## Structure

```
dev/projects/tms9918_wozmon/
├── Makefile                       # build TMS_Wozmon_demo.{bin,txt}
├── apple1_tms_wozmon.cfg           # ld65 cfg, CODE @ $0300-$1FFF
├── TMS_Wozmon_demo.asm             # démo : init + bannière + wrap + scroll
├── emit_TMS_Wozmon_demo_txt.py     # wrapper Wozmon-hex
└── README.md                       # ce fichier

dev/lib/tms9918/
├── tms9918_text.asm                # NEW driver Text Mode F1 (~250 lignes)
└── tms9918_console.asm             # NEW runtime console (~180 lignes)
```

Le fichier `tms9918_text.asm` `.incbin` `roms/charmap.rom` (1 ko) et le
bit-reverse à la volée pendant l'upload (Apple-1 = LSB à gauche, TMS9918
= MSB à gauche).

## Build

```bash
cd dev/projects/tms9918_wozmon
make
```

Sortie :

- `software/tms9918/TMS_Wozmon_demo.bin`
- `software/tms9918/TMS_Wozmon_demo.txt` (Wozmon-hex, copie-collable au
  prompt Wozmon ou utilisable via *File → Load Memory*)

## Lancement

```bash
./build/POM1 --preset 2 \
             --load 0300:software/tms9918/TMS_Wozmon_demo.bin \
             --run 0300
```

`--preset 2` = *P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)*, qui
plug le TMS9918 par défaut.

## Ce que la démo affiche

```
TMS9918 TEXT MODE F1 -- 40x24
WRAP TEST (50 X, expect 1.25 lines):
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
X
SCROLL TEST (30 lines):
LINE 06
LINE 07
...
LINE 29
TMS9918 CONSOLE OK
```

(Les 6 premières lignes ont disparu en haut suite au scroll : 30 lignes
émises − 24 visibles = 6 retirées.)

## Choix d'implémentation

- **Adresse de chargement = `$0300`** (et non `$0280`) pour préserver la
  zone de chargement standard Apple-1 (`$0280-$02FF`) — un programme
  utilisateur peut donc cohabiter en RAM. Le buffer clavier Wozmon
  (`$0200-$027F`) est aussi préservé.
- **Police = `roms/charmap.rom`** (charmap Apple-1 d'origine, 128
  glyphes 8×8). `.incbin` dans le driver, bit-reversé à l'upload.
- **ZP allouée par le driver** : 6 octets (`vdp_lo/hi`, `vdp_src_lo/hi`,
  `vdp_row`, `vdp_col`) ; par le runtime : 3 octets (`cur_row`, `cur_col`,
  `con_tmp`). Hors zone Wozmon (`$24-$2B`).
- **Buffer de scroll** : 40 octets en BSS (`$1E00`+) — pas en ZP.
- **Display blanké pendant l'upload du charmap** (R1 = `$80` puis
  `$D0`) : fenêtre d'accès silicon-strict ouverte en grand pendant les
  1024 octets du burst.

## Conformité silicon-strict

- macros `WRT_DATA_REG` / `WRT_DATA_VAL` de `tms9918.inc` non utilisées
  ici (le driver gère sa propre cadence) ; chaque `STA VDP_DATA` est
  suivi d'un corps de boucle `INY/CPY/BNE` ou `LSR/ROL/DEC/BNE` qui
  consomme ≥10 cycles, soit largement au-delà du seuil de 8 cycles
  (cf. `dev/SILICONBUGS.md` Bug N°1).
- Pendant `upload_charmap` le display est blanké, donc même la rafale
  dense de 1024 octets ne déclenche pas de drop.
- `console_scroll` est l'opération la plus exigeante : 23 lignes de
  40 lectures puis 40 écritures = 1840 accès VRAM ; chaque accès a un
  corps `INY/CPY #40/BNE` derrière, ~16 cycles entre transactions.

## Limitations connues (M1→M2)

1. **`JSR $FFEF` n'est pas redirigé.** Le Wozmon stock continue à
   écrire sur `$D012` — pour valider que la console TMS9918 marche en
   conditions réelles (Chess et consorts), il faudra M3.
2. **Pas de gestion du curseur visible** (clignotant). Reporté à M3.
3. **Pas d'entrée clavier** : la démo écrit, retourne à Wozmon. M3
   ajoutera la boucle d'entrée pour faire de l'écran TMS9918 un
   terminal Wozmon complet.
4. **`_` (`$5F` / `$DF`) rendu littéralement**, pas comme un
   backspace. C'est un choix : certains jeux impriment de vrais
   underscores. Le BS reste sur `$08`.

## Vérification rapide

Inspection visuelle (manuel) :

1. La bannière `"TMS9918 TEXT MODE F1 -- 40x24"` apparaît sur la
   ligne 0 avec la police Apple-1 (et pas une police de fallback).
2. La rafale de 50 `X` occupe la ligne 2 + les 10 premiers de la
   ligne 3 (preuve du wrap colonne 40).
3. Les 30 lignes `LINE NN` font scroller — la ligne 0 affichée
   doit être `LINE 06` après stabilisation, la ligne 23 `TMS9918
   CONSOLE OK`.
4. Le `\` du prompt Wozmon réapparaît sur l'écran `$D012` d'origine
   (pas sur le TMS9918) : c'est attendu — la redirection ECHO
   arrive en M3.

Snapshot VRAM (semi-automatique) :

```bash
./build/POM1 --preset 2 \
             --load 0300:software/tms9918/TMS_Wozmon_demo.bin \
             --run 0300 \
             --step 500000 \
             --snapshot-save /tmp/tms_wozmon_demo.snap
```

Inspecter la name table à `$0800` + 23×40 = `$0BB8` : doit contenir
les 18 caractères ASCII de `"TMS9918 CONSOLE OK"` puis des espaces.

## Suite

- **M3** : ROM Wozmon 256 octets avec `JMP console_putc` à `$FFEF`,
  installée via `--load FF00:tms_wozmon_rom.bin`.
- **M4** : tests E2E sur Chess, Connect4, Sokoban-text, Little-Tower
  inchangés.
- **M5** : layout `--single-upper` dans `tools/build_codetank_rom.py`
  pour livrer une cartouche CodeTank 16 ko bootable sans CLI.
- **M6** : option *Hardware menu → TMS9918 console (replace ECHO)*
  côté C++.
