# MiSTer — Apple-I + P-LAB TMS9918 + CodeTank

Cœur FPGA autonome sous `fpga/` : Apple **6502** (MiSTer Apple-I), carte **TMS9918A** dans la fenêtre **$CC00/$CC01** (données / statut‑registres), cartouche **CodeTank** 32 Ko (28C256) avec une moitié 16 Ko visible en **$4000–$7FFF** selon l’OSD.

## Provenance et licences

| Bloc | Source |
|------|--------|
| Apple‑I MiSTer (`emu`, PLL, `sys/`) | [MiSTer-devel/Apple-I_MiSTer](https://github.com/MiSTer-devel/Apple-I_MiSTer) — voir fichiers sources |
| Cœur `apple1.v`, CPU Arlet, VGA, RAM… | Apache 2.0 (en-têtes dans `rtl/`) |
| **vdp18** (TMS9918A HDL) | Arnim Laeuger — copie alignée sur [MiSTer-devel/MSX1_MiSTer](https://github.com/MiSTer-devel/MSX1_MiSTer) `rtl/vdp18/`, licence BSD (voir `rtl/vdp18/COPYING`) |
| **spram** (`rtl/bram.vhd`) | Module Altera BRAM tel qu’utilisé par MSX1 MiSTer |

Les dépôts vendeurs complets sont conservés sous `fpga/vendor/` pour référence.

## Arborescence utile

- `AppleI_PLAB.qpf` / `AppleI_PLAB.qsf` — projet Quartus (révision **AppleI_PLAB**)
- `AppleI_P-LAB_TMS9918.sv` — module **`emu`** MiSTer
- `rtl/apple1_plab.v` — décodage bus + multiplexage données (CodeTank / TMS9918)
- `rtl/tms9918_plab.v` — instanciation **vdp18_core** + VRAM 16 Ko (`spram`)
- `rtl/codetank_rom.sv` — RAM 32 Ko, chargement OSD strict **32768** octets
- `rtl/ce_10m7_gen.sv` — enable ~10,738635 MHz à partir du domaine **25 MHz** CPU
- `rtl/vdp18/` — cœur VHDL TMS9918 (via `vdp18.qip`)
- `roms/` — `basic.hex`, `wozmon.hex`, `vga_*.bin/hex`, etc. ; `roms/codetank/` pour les images `.rom` de test

## Décode mémoire (rappel)

- **CodeTank** : lecture seule **$4000–$7FFF** ; banque **Lower** = décalage 0 dans l’image, **Upper** = décalage +16 Ko (`$4000` dans le fichier).
- **TMS9918** : **$CC00** = données VRAM, **$CC01** = registre / statut (`A0` = bit bas de l’adresse).
- Le RAM Apple‑1 est masquée dans **$4000–$7FFF** lorsque une ROM CodeTank **valide** est chargée (chargement accepté **uniquement** si **exactement** 32 768 octets reçus via OSD).

## OSD (CONF_STR)

| Option | Bit `status` | Rôle |
|--------|----------------|------|
| Load Ascii | ioctl index **0** | charge ASCII comme le core Apple‑I d’origine |
| Load CodeTank | ioctl index **1** | fichier **.bin/.rom** 32 Ko |
| CodeTank bank | **[3]** | Lower / Upper |
| Video | **[4]** | Apple‑1 texte / sortie TMS9918 |
| Scanlines | **[5]** | réservé (affiché ; non câblé au mixer dans ce MVP) |
| RAM Size | **[6]** | 8 Ko / 32 Ko |
| Reset | **[0]** | reset core |

Si le chargement TXT/CodeTank ne part pas, vérifier les indices `ioctl_index` côté MiSTer (certaines builds incrémentent à partir de **1**).

## Build Quartus

1. Ouvrir **`fpga/AppleI_PLAB.qpf`** (depuis le répertoire `fpga/` pour les chemins relatifs vers `roms/`).
2. `Processing` → `Start Compilation`.
3. Récupérer le **`.rbf`** dans `output_files/` et l’installer sur la carte SD MiSTer comme d’habitude (`/_Arcade/` ou dossier core dédié selon votre flux).

`sys/build_id.tcl` (PRE_FLOW) régénère `build_id.v` à la compilation ; une copie de secours est fournie dans le dépôt.

## Test rapide (CodeTank)

1. Copier **`Codetank_GAME1.rom`** (depuis `../roms/codetank/` du dépôt POM1) vers la carte ou utiliser `fpga/roms/codetank/Codetank_GAME1.rom`.
2. OSD → **Load CodeTank** → sélectionner le fichier **32768** octets.
3. OSD → **CodeTank bank** = **Lower**.
4. Reset (OSD ou bouton).
5. OSD → **Video** = **TMS9918** pour afficher la sortie VDP.
6. Au prompt WozMon : **`4000R`**.

Upper jumper équivalent : banque **Upper** puis **`4000R`** pour TMS_LOGO (voir `roms/codetank/Codetank_GAME1.txt` côté POM1).

## Limitations connues (MVP)

- IRQ du TMS9918 non reliée au **IRQ** du 6502 (extensions possibles).
- Scanlines OSD non routées vers `video_mixer` (placeholder).

Pour refaire une base propre à partir du dépôt upstream MiSTer Apple‑I : comparer `vendor/Apple-I_MiSTer/boards/MiSTer/` avec ce répertoire.
