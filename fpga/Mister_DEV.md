# Programmation FPGA pour MiSTer — guide de référence

Notes pratiques pour développer ou porter un core sur la plateforme **MiSTer FPGA**. Tous les exemples sont ancrés sur le port Apple-I présent dans `fpga/Apple-I_MiSTer/`.

---

## 1. Plateforme matérielle

| | |
|---|---|
| Carte officielle | **Terasic DE10-Nano** |
| SoC | **Intel/Altera Cyclone V SE 5CSEBA6U23I7** *(`Apple-I.qsf:78`)* |
| Famille | Cyclone V |
| Architecture | FPGA fabric **+** ARM Cortex-A9 dual-core (HPS) sur le même die |
| FPGA | ~110 K LE, ~5 570 ALMs, BRAM 5 570 kbit, 224 DSP 18×18 |
| HPS | Cortex-A9 dual-core 800 MHz, 1 GB DDR3, sortie HDMI exposée par les pins du FPGA |
| Horloges | 3 oscillateurs 50 MHz (`FPGA_CLK1_50`, `FPGA_CLK2_50`, `FPGA_CLK3_50`) |
| Mémoires externes | DDR3 1 GB (HPS), **SDRAM 32 MB ou 128 MB** sur module add-on enfichable sur les GPIO (`misteraddons.com`, vendeurs Amazon) — 128 MB recommandé pour Neo Geo / GBA et certains cores 32-bit |
| I/O | HDMI 24-bit, VGA legacy 6-bit, SD card SDIO, USB hub (HPS), I2S+S/PDIF+DAC sigma-delta, MIDI, joystick analog/digital, MCP23009 GPIO, LTC2308 ADC, RTC sur HPS |
| Toolchain | **Quartus Prime Lite 17.0+** *(le framework supporte aussi v13.0sp1 historique)* |
| Bitstream produit | `<core>.rbf` — Raw Binary File chargé par le HPS au démarrage du core |

Le DE10-Nano expose ses pins sur **deux GPIO 40-pins** : la couche additionnelle « MiSTer IO Board » (variantes Analog Pro, Analog VGA, Digital) y branche VGA, audio analogique, S/PDIF, boutons façade ; la SDRAM s'enfiche en parallèle sur l'autre GPIO. Tout est piloté par le top-level commun `sys_top.v`.

---

## 2. Le framework `sys/` — ce qui te dispense d'écrire

Le framework partagé vit sous `boards/MiSTer/sys/` et est inclus via `sys.qip` *(`Apple-I.qsf:11` impose `TOP_LEVEL_ENTITY = sys_top`)*. Il fournit, prêts à câbler :

### 2.1 Top-level + pont HPS

| Fichier | Rôle |
|---|---|
| `sys_top.v` | **TOP_LEVEL_ENTITY de tous les cores MiSTer**. Instancie le core utilisateur (`emu`) et gère pads HDMI, VGA, SDIO, SDRAM, audio analog, etc. |
| `hps_io.sv` | Pont bidirectionnel ARM↔FPGA : OSD, parsing du `CONF_STR`, ioctl file load, PS/2 keyboard, joystick, status[31:0], gamma table, RTC |
| `sysmem.sv`, `f2sdram_safe_terminator.sv`, `ddr_svc.sv` | Ponts SDRAM / DDR3 |

### 2.2 Pipeline vidéo

| Fichier | Rôle |
|---|---|
| `ascal.vhd` | Scaler **AS** advanced **SCAL**er — upscale n'importe quelle résolution vers HDMI |
| `scandoubler.v`, `scanlines.v` | Doublage de lignes + simulation scanlines CRT |
| `hq2x.sv` | Filtre HQ2X (lissage style emulateur) |
| `shadowmask.sv` | Masque CRT (Trinitron, aperture grille, etc.) |
| `gamma_corr.sv` | Correction gamma |
| `video_mixer.sv`, `video_cleaner.sv`, `video_freak.sv`, `video_freezer.sv` | Mux + Y/U/V + freeze frame |
| `vga_out.sv`, `yc_out.sv` | Sortie VGA analogique 6-bit + S-Video |
| `osd.v` | Overlay On-Screen Display |
| `arcade_video.v` | Wrapper helper pour cores arcade |

### 2.3 Pipeline audio

| Fichier | Rôle |
|---|---|
| `i2s.v` | Sortie I2S 16-bit stereo (vers HDMI + DAC) |
| `spdif.v` | Sortie S/PDIF |
| `sigma_delta_dac.v` | DAC 1-bit sigma-delta pour audio analogique |
| `audio_out.v`, `iir_filter.v` | Pipeline audio + filtrage |
| `alsa.sv` | Pont vers ALSA côté HPS (mt32-pi, etc.) |
| `mt32pi.sv` | Bridge optionnel MT-32 Pi |

### 2.4 PLL et helpers Quartus

| Fichier | Rôle |
|---|---|
| `pll_q13.qip` / `pll_q17.qip` | Variantes générées pour Quartus 13.0sp1 / 17.0+ — `sys.qip` choisit auto via `[regexp -inline {[0-9]+} $quartus(version)]` |
| `pll_hdmi`, `pll_audio`, `pll_cfg` | PLLs pré-configurées (clock pixel HDMI ajustable, audio 24.576 MHz, etc.) |
| `pll_hdmi_adj.vhd` | Ajustement runtime de la PLL HDMI |
| `sys_top.sdc` | Contraintes timing (déjà écrites — ne pas réinventer) |
| `sys_dual_sdram.tcl`, `sys_analog.tcl` | Scripts de configuration optionnels |

### 2.5 Périphériques

| Fichier | Rôle |
|---|---|
| `sd_card.sv` | Émulateur SD côté core (le HPS expose une image disque montable au core) |
| `i2c.v`, `mcp23009.sv`, `ltc2308.sv` | I2C master, GPIO expander, ADC analogique 8 canaux |
| `math.sv` | Helpers arithmétiques |

**Tu n'écris rien de ça.** Tu en consommes les ports.

---

## 3. Anatomie d'un core — le module `emu`

Un core MiSTer = **un module SystemVerilog nommé `emu`**, instancié par `sys_top.v`. Vu dans `Apple-I.sv:8` :

```systemverilog
module emu (
    input         CLK_50M,
    input         RESET,
    inout  [48:0] HPS_BUS,         // pont vers hps_io

    output        CLK_VIDEO,
    output        CE_PIXEL,
    output  [7:0] VIDEO_ARX,
    output  [7:0] VIDEO_ARY,
    output  [7:0] VGA_R, VGA_G, VGA_B,
    output        VGA_HS, VGA_VS, VGA_DE,
    output  [1:0] VGA_SL,           // scanlines

    output [15:0] AUDIO_L, AUDIO_R,
    output        AUDIO_S,          // 0=unsigned, 1=signed
    output        AUDIO_MIX,        // mono/stereo

    output        LED_USER, LED_DISK, LED_POWER,
    input  [11:0] HDMI_WIDTH, HDMI_HEIGHT,

    // joystick, USER_OUT, DDR3/SDRAM bus, gamma_bus, ...
);
```

L'entité fait environ **300+ ports** dans la définition complète — la plupart sont conditionnels (`` `ifdef MISTER_FB``, `` `ifdef MISTER_DUAL_SDRAM ``, etc.) et tu les laisses non câblés si ton core n'en a pas besoin (`assign FB_EN = 0;` etc.).

### 3.1 Le pattern : trois grandes zones

```systemverilog
// 1. CONF_STR  → décrit l'OSD (cf. §4)
// 2. hps_io   → instance du pont HPS (cf. §5)
// 3. PLL + clk_sys + reset
// 4. core lui-même (`apple1 apple1 (...)` chez nous)
// 5. assign sortie video / audio
```

Vu dans `Apple-I.sv` *(extraits)* :

```systemverilog
// ------------------------------- CLOCKS -------------------------------
wire clk6p25, clk25;
assign clk_sys = clk25;

pll pll (
    .refclk  (CLK_50M),
    .rst     (0),
    .locked  (locked),
    .outclk_0(clk6p25),
    .outclk_1(clk25)
);

wire reset = RESET | status[0] | buttons[1];

// ----------------------------- CORE INSTANCE --------------------------
apple1 apple1 (
    .clk25(clk25),
    .rst_n(~reset),
    .ps2_clk      (ps2_kbd_clk),
    .ps2_din      (ps2_kbd_data),
    .ps2_select   (1'b1),
    .vga_h_sync(hs), .vga_v_sync(vs),
    .vga_red(r), .vga_grn(g), .vga_blu(b),
    .vga_de(VGA_DE),
    .ioctl_download(ioctl_download && ioctl_index),  // file download from OSD
    .textinput_dout(ioctl_data),
    .textinput_addr(ioctl_addr[12:0]),
    .large_ram   (status[2])                          // OSD bit
);

// 1-bit RGB → 8-bit (sera mixé/scalé par sys/)
assign VGA_R = {{r,r,r}, 5'b0};
assign VGA_G = {{g,g,g}, 5'b0};
assign VGA_B = {{b,b,b}, 5'b0};
```

---

## 4. OSD — le DSL `CONF_STR`

Le menu sur écran (OSD = touche F12/MENU sur le clavier) est défini par une **constante string** que `hps_io` lit. Chaque ligne décrit une entrée de menu. Exemple `Apple-I.sv:201` :

```systemverilog
localparam CONF_STR = {
    "APPLE-I;;",                       // nom du core
    "F,TXT,Load Ascii;",               // bouton "Load file" — extension TXT
    "O1,Scanlines,Off,On;",            // toggle 1 bit  → status[1]
    "O2,RAM Size,8K,32K;",             // toggle 1 bit  → status[2]
    "-;",                              // séparateur
    "R0,Reset;",                       // bouton momentané → status[0]
    "V,v",`BUILD_DATE                  // version footer
};
```

### Vocabulaire

| Préfixe | Signification |
|---|---|
| `<NAME>;;` | Première ligne = nom affiché en haut de l'OSD |
| `F<idx>,EXT,Label;` | Bouton de chargement de fichier. `idx` (optionnel) → `ioctl_index` ; `EXT` → extension filtrée |
| `S<idx>,EXT,Label;` | Mount d'image disque (vers `sd_card.sv` côté core) |
| `Oxx,Label,A,B[,C…];` | Toggle / liste — `xx` = bit position dans `status[31:0]` (1 ou 2 chiffres hex). 2 valeurs = 1 bit ; 4 valeurs = 2 bits |
| `Oxxyy,Label,…;` | Champ multi-bits — bits `xx` à `yy` |
| `R<bit>,Label;` | Bouton momentané (auto-reset) |
| `T<bit>,Label;` | Trigger pulse |
| `D<n>` | Préfixe : entrée masquée par `status_menumask[n]` |
| `-;` | Séparateur visuel |
| `V,v…` | Version footer (concaténée avec `BUILD_DATE`) |
| `J,Btn1,Btn2…;` | Mapping joystick |

### Comment le core lit ça

```systemverilog
wire [31:0] status;          // produit par hps_io
wire reset = RESET | status[0] | buttons[1];   // R0 → status[0]
wire scanlines_on = status[1];                 // O1
wire large_ram   = status[2];                  // O2
```

Le HPS parse le `CONF_STR`, dessine le menu, écrit dans `status[]` selon les choix utilisateur. Le bit s'applique au cycle suivant — pas de protocole, c'est un registre live.

---

## 5. Le pont HPS↔core — `hps_io`

Tout passage de données entre l'ARM et le FPGA passe par `HPS_BUS [48:0]` et est démultiplexé par `hps_io`. Vu dans `Apple-I.sv:226` :

```systemverilog
hps_io #(.CONF_STR(CONF_STR), .PS2DIV(4000)) hps_io (
    .clk_sys(clk_sys),
    .HPS_BUS(HPS_BUS),

    .forced_scandoubler(forced_scandoubler),
    .buttons       (buttons),         // [1:0]  : OSD + USER face buttons
    .status        (status),          // [31:0] : OSD bits live
    .status_menumask({status[5]}),    // bits qui masquent des entrées D<n>

    .ioctl_download(ioctl_download),  // 1 pendant le download
    .ioctl_wr      (ioctl_wr),        // pulse 1 cycle par octet
    .ioctl_addr    (ioctl_addr),      // [15:0] (ou [24:0] selon core)
    .ioctl_dout    (ioctl_data),      // [7:0] (configurable [15:0])
    .ioctl_index   (ioctl_index),     // [7:0] : laquelle des entrées F a déclenché ?
    .ioctl_wait    (ioctl_wait),      // ralentir l'HPS en backpressure

    .joystick_0(joystick_0),
    .joystick_1(joystick_1),

    .ps2_kbd_clk_out (ps2_kbd_clk),   // PS/2 simulé depuis USB hub
    .ps2_kbd_data_out(ps2_kbd_data)
);
```

### 5.1 ioctl — le pipeline de chargement de fichier

L'utilisateur choisit un fichier dans l'OSD → HPS le lit → flux `ioctl_*` :

```
ioctl_download  __‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾__
ioctl_wr        ___|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_____
ioctl_addr      ===A0=A1=A2=A3=A4=A5=A6=A7=A8========
ioctl_dout      ===D0=D1=D2=D3=D4=D5=D6=D7=D8========
ioctl_index     ===<n>================================
```

Le core consomme ce flux pour pré-charger une RAM, écrire en SDRAM, alimenter un convertisseur ASCII *(`apple1.v` MiSTer le fait dans `ascii_input.v` pour injecter des caractères au clavier)*, etc.

### 5.2 Backpressure

`ioctl_wait = 1` met le HPS en pause sur l'octet courant — utile si ta RAM est sur SDRAM avec write latency.

### 5.3 PS/2

Le HPS détecte un USB keyboard sur son hub, le traduit en frames PS/2, et les rejoue sur `ps2_kbd_clk_out` / `ps2_kbd_data_out`. Le core voit un PS/2 « pur » et n'a besoin de rien de plus *(`Apple-I.sv:309`* `.ps2_clk(ps2_kbd_clk), .ps2_din(ps2_kbd_data)`*)*.

### 5.4 Variantes du bus

`hps_io` est très paramétrable :
- `CONF_STR` : la string OSD (cf. §4)
- `PS2DIV` : diviseur d'horloge PS/2 (4000 = ~12 kHz à clk_sys 50 MHz)
- `WIDE` : bus ioctl_dout en 16 bits (sinon 8 bits)
- `VDNUM` : nombre d'images disque virtuelles montables (1 à 4)
- `BLKSZ` : taille de bloc des reads/writes vers les images disques (0=128 → 7=16384, défaut 2=512)
- `PS2WE` : autorise le core à écrire vers le clavier/souris PS/2 (animation LEDs verr-num, etc.)

`ioctl_index` encode deux infos : `[5:0]` = index de l'entrée `F<n>` ou `S<n>` qui a déclenché le download, `[7:6]` = index d'extension si l'entrée acceptait plusieurs extensions. Utile pour distinguer un .bin d'un .hex chargé par la même entrée OSD.

Module séparé `EXT_BUS` exposé pour des cores qui veulent du MMIO custom HPS↔core (rare).

---

## 6. Quartus — settings et conventions

### 6.1 `<core>.qsf` — points clés

```
set_global_assignment -name TOP_LEVEL_ENTITY sys_top      # JAMAIS le core, toujours sys_top
set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CSEBA6U23I7
set_global_assignment -name GENERATE_RBF_FILE ON          # produit le .rbf au lieu de .sof
set_global_assignment -name OPTIMIZATION_MODE "HIGH PERFORMANCE EFFORT"
set_global_assignment -name FITTER_EFFORT "STANDARD FIT"
```

### 6.2 `files.qip` — convention « ne touche pas l'IDE »

L'avertissement en tête de chaque `<core>.qsf` MiSTer :
> **WARNING WARNING WARNING:** Do not add files to project in Quartus IDE! It will mess this file! Add the files manually to `files.qip` file.

Concrètement, `files.qip` est un fichier texte que tu édites à la main pour lister les sources :

```
set_global_assignment -name SYSTEMVERILOG_FILE Apple-I.sv
set_global_assignment -name VERILOG_FILE ../../rtl/apple1.v
set_global_assignment -name VERILOG_FILE ../../rtl/cpu/arlet_6502.v
…
set_global_assignment -name QIP_FILE sys/sys.qip          # tire tout le framework
```

`sys.qip` à son tour est intelligent : il sélectionne `pll_q13.qip` ou `pll_q17.qip` selon ta version Quartus.

### 6.3 Build

```bash
quartus_sh --flow compile boards/MiSTer/Apple-I.qpf
```

Output → `output_files/Apple-I.rbf` (typiquement quelques centaines de KB pour un core léger comme Apple-I, jusqu'à 8-10 MB pour des consoles).

### 6.4 Déploiement

Copier le `.rbf` sur la SD card MiSTer, dossier approprié :

```
/media/fat/_Computer/Apple-I_<DATE>.rbf      # cores "computer"
/media/fat/_Console/<name>_<DATE>.rbf
/media/fat/_Arcade/<name>_<DATE>.rbf
```

Le **menu MiSTer** (lui-même un core FPGA) liste les `.rbf` et les charge à la volée par reconfiguration FPGA via le HPS.

---

## 7. Mémoire et timing — règles pratiques

### 7.1 Hiérarchie mémoire

| Niveau | Capacité | Latence typique | Usage |
|---|---|---|---|
| BRAM (M10K) interne | ~5.5 Mbit (700 KB) | 1 cycle | RAM/ROM core, SAT sprites, palettes, FIFOs |
| SDRAM externe (option) | 32–128 MB | ~10 cycles | ROM cartouche, frame buffers grands |
| DDR3 HPS | 1 GB | variable (via Avalon bridge) | framebuffer scaler, MT-32 Pi, gros assets, sound samples |
| SD card (via HPS) | jusqu'à 256 GB | très lente | images disques, ROMs sur étagère |

### 7.2 Initialisation des BRAM via fichier `.hex`

Pattern Apple-1 *(simplifié)* :

```systemverilog
module rom_wozmon (input clk, input [7:0] addr, output reg [7:0] data);
    reg [7:0] mem [0:255];
    initial $readmemh("../../../roms/wozmon.hex", mem);
    always @(posedge clk) data <= mem[addr];
endmodule
```

`$readmemh` est synthétisable : Quartus génère un MIF (Memory Initialization File) inline et l'inclut dans le bitstream. Vrai pour Cyclone V — pas portable sur tous les FPGAs.

### 7.3 SDC — contraintes timing

`sys/sys_top.sdc` couvre déjà les clocks principales (50 MHz, HDMI pixel clock, audio, SDRAM). Pour ajouter une PLL dérivée, mets ton `.sdc` dans `boards/MiSTer/` et ajoute-la à `<core>.qsf` :

```
set_global_assignment -name SDC_FILE my_extra_constraints.sdc
```

### 7.4 Reset synchrone

Tous les cores MiSTer dérivent leur reset de :

```systemverilog
wire reset = RESET           // reset matériel/init
           | status[0]       // bouton "Reset" OSD
           | buttons[1];     // bouton USER face avant
```

`RESET` arrive synchrone à `CLK_50M` ; resynchronise vers ton `clk_sys` si tu en as un dérivé d'une PLL.

---

## 8. Vidéo : ce que ton core doit produire

Le pipeline `sys/` accepte n'importe quelle résolution interne et la scale vers HDMI 1080p (configurable). Ton core fournit :

| Signal | Spec |
|---|---|
| `CLK_VIDEO` | clock de pixel — souvent = `clk_sys` ou un dérivé |
| `CE_PIXEL` | clock-enable 1-cycle quand un nouveau pixel est valide *(Apple-I : `assign CE_PIXEL = 1` car output déjà au bon rate)* |
| `VGA_R/G/B [7:0]` | RGB 8-bit MSB-first ; pad bas avec 0 si profondeur < 8 |
| `VGA_HS/VS` | sync signals, polarité libre (configurable côté sys) |
| `VGA_DE` | data-enable = `~(HBlank | VBlank)` — sys s'en sert pour détecter le rectangle actif |
| `VIDEO_ARX/ARY` | aspect ratio (4:3, 16:9, …) — peut aussi encoder une taille forcée si bit 12 est set |
| `VGA_F1` | interlace flag (0 si progressif) |
| `VGA_SL` | option scanlines (consommée par sys/scanlines.v) |

Sortie HDMI / VGA / scaler / scanlines / shadowmask / gamma → tout fait par `sys_top` à partir de ces signaux. Tu ne touches jamais directement HDMI_TX_*.

**Mode framebuffer** *(`MISTER_FB`)* : alternative où tu écris ton image en DDR3 et le scaler la lit. Réservé aux cores avec résolution dynamique large (PSX, Saturn, etc.) — pas nécessaire pour un Apple-1.

---

## 9. Audio

Le core émet :
```systemverilog
output [15:0] AUDIO_L;     // PCM 16-bit signé ou non
output [15:0] AUDIO_R;
output        AUDIO_S;     // 0 = unsigned, 1 = signed
output        AUDIO_MIX;   // mode mixage
```

Sample rate **n'est pas explicite** : tu fournis un nouvel échantillon à un rythme cohérent avec ton clock. Le pipeline `sys/audio_out.v` resample à 48 kHz pour I2S/HDMI/S/PDIF. Le DAC sigma-delta analogique se fait en aval.

Pour un Apple-1 sans audio, `assign AUDIO_L = 0; AUDIO_R = 0;` suffit.

---

## 10. Saves, RTC, sauvegardes

`hps_io` expose :
- `RTC` : 64-bit horloge réelle depuis le HPS *(BCD encoded)*
- `TIMESTAMP` : Unix epoch
- Image disque virtuelle via `S<idx>` dans `CONF_STR` — le HPS expose au core une interface SD-like via `sd_card.sv`
- Save state : module `nvram` optionnel — le core indique les bytes à persister, le HPS les sauvegarde sur SD

Pas utilisé par Apple-I MiSTer (pas de save state, pas d'horloge RTC).

---

## 11. Convertir un design FPGA « nu » en core MiSTer — checklist

Si tu pars d'un design existant pour DE0/iCE40/Spartan (cf. `alangarf/apple-one`) :

1. **Renommer ton entité top** ou la **wrapper** dans un module `emu` qui matche la signature `sys/sys_top.v`.
2. **Supprimer** tout pinout direct (HDMI_*, VGA_*, audio DAC, PS/2 buffers, USB host) — `sys_top` les expose déjà.
3. **Remplacer** ton ancienne PLL par une PLL `sys/pll_*` ou par une PLL Cyclone V générée via Megawizard *(le port Apple-I MiSTer a fait ça : `rtl/pll/pll.v` + `rtl/pll.qip`, sortant 6.25 MHz et 25 MHz)*.
4. **Convertir** tes paramètres SystemVerilog problématiques : `parameter string FOO = …;` → `parameter FOO = …;` *(Quartus 17 ne supporte pas `parameter string` synthétisable)*.
5. **Définir** un `CONF_STR` minimal — au minimum `<NAME>;;` + `R0,Reset;`.
6. **Câbler** au moins `clk_sys`, reset, video out (RGB+HS+VS+DE+ARX/ARY), audio (16-bit L/R).
7. **Lister** tes sources dans `files.qip`. Inclure `sys/sys.qip` à la fin.
8. **Compiler** : `quartus_sh --flow compile boards/MiSTer/<core>.qpf`. La première compile prend 5-15 minutes.
9. **Déployer** le `.rbf` sur la SD MiSTer.
10. **Itérer** : Quartus signal-tap + le port USB-Blaster du DE10-Nano permettent du JTAG live.

### Exemple concret de ce qui a été fait pour Apple-I

| Étape | Fichier MiSTer | Ce qui change vs alangarf |
|---|---|---|
| Wrapper `emu` | `boards/MiSTer/Apple-I.sv` (331 L) | Entièrement neuf — câble PLL, hps_io, `apple1` instance |
| PLL Cyclone V | `rtl/pll/pll.v` + `pll.qip` | Neuf (alangarf utilise une PLL par board target) |
| Adaptation top | `rtl/apple1.v` | `parameter string` → `parameter`, USB keyboard supprimé, `key_select [1:0]` → `ps2_select` simple, ajout `ioctl_download` + `textinput_dout/addr` |
| ASCII injection | `rtl/ascii_input.v` (nouveau) | Convertit le flux ioctl en frames keyboard pour le core |
| Quartus settings | `Apple-I.qsf`, `.qpf`, `files.qip` | Imposés par convention MiSTer |

---

## 12. Pièges fréquents

1. **TOP_LEVEL_ENTITY** : c'est *toujours* `sys_top`, jamais ton module — sinon le bitstream n'a pas les pads HDMI/SDRAM/etc.
2. **Quartus IDE casse `<core>.qsf`** : ne jamais ajouter de fichiers via l'IDE ; tout passe par `files.qip` édité à la main.
3. **Latence des reads BRAM** : 1 cycle minimum pour les RAM/ROM Cyclone V. Pipeline ton CPU en conséquence (le core arlet_6502 attend déjà ce cycle).
4. **`ioctl_data [7:0]` vs [15:0]** : par défaut 8-bit, passe à 16 via paramètre WIDE de hps_io si tu charges un format 16-bit aligné.
5. **`status[]` est asynchrone** côté HPS : resynchronise les bits que tu compares dans des FSM critiques.
6. **PS/2 reset** : Le HPS peut reseed le keyboard PS/2 → ton core doit gérer un reset clavier intermittent.
7. **PLL `pll_q13.qip` vs `pll_q17.qip`** : `sys.qip` choisit auto via la version Quartus — garde tous tes IP dans les deux variants si tu veux supporter les deux.
8. **`GENERATE_RBF_FILE = ON`** : indispensable, sinon Quartus produit un `.sof` que MiSTer ne sait pas charger.
9. **L'HPS a la priorité d'init** : au boot, l'HPS charge le core dans le FPGA. Le HPS n'est pas en sommeil pendant que ton core tourne — il continue à lui parler via `HPS_BUS` (OSD, USB, SD).
10. **Compatibilité Quartus** : le code `sys/` est testé sur Quartus 17.0 et 13.0sp1. Versions intermédiaires (14, 15, 16) souvent buggées sur Cyclone V SoC. Stick à 17.0 Lite.

---

## 13. Lectures complémentaires

| Sujet | Source |
|---|---|
| **Doc officielle MkDocs** | `https://mister-devel.github.io/MkDocs_MiSTer/` *(la référence à jour, en remplacement progressif du wiki)* |
| **Bible MiSTer** *(community)* | `https://boogermann.github.io/Bible_MiSTer/` *(montage hardware, troubleshooting, dev)* |
| **Forum officiel** | `https://misterfpga.org/` *(sous-forums : développement, hardware, releases, news)* |
| Wiki Main_MiSTer *(historique)* | `https://github.com/MiSTer-devel/Main_MiSTer/wiki` |
| Template de core officiel | `https://github.com/MiSTer-devel/Template_MiSTer` *(point de départ pour un nouveau core)* |
| Tutorials core (alanswx) | `https://github.com/MiSTer-devel/Tutorials_MiSTer` *(curateur arcade, exemples commentés)* |
| Référence `hps_io` | `https://mister-devel.github.io/MkDocs_MiSTer/developer/hps_io/` |
| Référence `CONF_STR` | `https://mister-devel.github.io/MkDocs_MiSTer/developer/conf_str/` |
| Vue d'ensemble `emu` | `https://mister-devel.github.io/MkDocs_MiSTer/developer/emu/` |
| `sys/` lui-même | `https://github.com/MiSTer-devel/Main_MiSTer/tree/master/sys` |
| Schémas DE10-Nano | Site Terasic — pinout `DE10-Nano_Pin_Constraints.qsf` |
| Cyclone V SoC HW Manual | Intel/Altera CV-5V2 |
| HPS↔FPGA bridges | `Cyclone V SoC HPS Technical Reference Manual`, chapitres FPGA Manager + LWHPS2FPGA bridge |
| Catalogue cores | `https://github.com/MiSTer-devel/` *(chaque famille = un repo : `Apple-II_MiSTer`, `C64_MiSTer`, `NES_MiSTer`, `MegaDrive_MiSTer`, `PSX_MiSTer`, `Saturn_MiSTer`, `N64_MiSTer`, …)* |
| Downloader MiSTer | `https://github.com/MiSTer-devel/Downloader_MiSTer` *(outil officiel d'installation/MAJ des cores)* |
| Wiki community alt. | `https://emulation.gametechwiki.com/index.php/MiSTer` |

---

## 14. Pour POM1 spécifiquement

Si l'objectif est un **port POM1 → MiSTer**, observations :

- POM1 émule Apple-1 + ~15 cartes d'extension. Le core `MiSTer-devel/Apple-I_MiSTer` ne couvre que le bare Apple-1 + clavier ASCII via OSD.
- Pour un FPGA équivalent, il faudrait écrire en Verilog/SystemVerilog : 6502 *(déjà disponible : arlet/aholme — utilisable tels quels)*, PIA 6821, Wozmon ROM *(.hex déjà fourni)*, BASIC ROM, RAM. Soit ~80 % du cœur Apple-1 déjà fait.
- Les cartes complexes nécessiteraient des modules neufs : SID *(libresidfp = code C++, pas portable RTL — il existe `mist-board/sid_top.vhd` réutilisable)*, TMS9918 *(F18A core ou JF Del Nero VHDL existe)*, microSD *(le framework `sys/sd_card.sv` peut servir d'interface)*, IEC *(C64_MiSTer a un bus IEC complet)*.
- POM1 a déjà l'avantage d'avoir des dumps ROM (`roms/`) compatibles avec ce que les cores attendent — `roms/basic.hex`, `roms/wozmon.hex` MiSTer correspondent aux mêmes contenus que `roms/basic.rom`, `roms/wozmon.rom` POM1.

**Démarrage minimal pour explorer** : copier `fpga/Apple-I_MiSTer/` sur une carte SD MiSTer existante *(`releases/Apple-I_<date>.rbf` est déjà compilé)*, lancer, vérifier le user-flow `Load TXT`. Ensuite, lire `Apple-I.sv` puis `apple1.v` en parallèle de notre `M6502.cpp` + `Memory.cpp` pour voir comment la même architecture s'écrit en logique vs en C++.

---

## 15. Vision écosystème — d'où ça vient, où ça va

### 15.1 Origine

Le projet est créé par **Alexey « Sorgelig » Melnikov** et publié sur GitHub en **juin 2017**. Sorgelig venait de **MiST**, plateforme FPGA homebrew antérieure (Atari ST + Amiga + ZX Spectrum) sortie en 2011-2013, dont la **sortie vidéo était strictement analogique**. En portant des cores sur MiST, Sorgelig peinait à connecter ses moniteurs et TV modernes — d'où l'idée d'une plateforme nativement HDMI. Le DE10-Nano, sorti par Terasic en 2017 avec son SoC Cyclone V intégrant un ARM Cortex-A9 dual-core capable de piloter du HDMI à 24-bit, devenait la cible évidente. MiSTer hérite de l'architecture MiST mais y ajoute :

- HDMI natif + scaler `ascal.vhd` (et plus tard scanlines, shadowmask, gamma, HQ2X)
- HPS ARM pour OSD, USB, parsing fichiers, save states
- Audio I2S 16-bit + S/PDIF
- Communauté et catalogue de cores qui dépassent largement MiST

### 15.2 Gouvernance et organisation

Toute la stack vit sous l'organisation GitHub **`MiSTer-devel/`** :

| Repo | Rôle |
|---|---|
| `Main_MiSTer` | Le binaire ARM côté HPS (orchestrateur des cores + OSD) — l'« OS » MiSTer |
| `Menu_MiSTer` | Le core FPGA qui sert de menu de démarrage |
| `Linux-Kernel_MiSTer` | Kernel Linux ARM patché pour le HPS |
| `Template_MiSTer` | Template d'un nouveau core (point de départ recommandé) |
| `Tutorials_MiSTer` | Exemples didactiques (curateur **alanswx**, qui maintient aussi la majorité des cores arcade) |
| `MkDocs_MiSTer` | Documentation moderne (succède au wiki, plus search-friendly) |
| `Downloader_MiSTer` | Tool pour récupérer/MAJ les `.rbf` cores depuis la SD card |
| `mr-fusion` | Image SD prête à l'emploi |
| `<Console>_MiSTer`, `<Computer>_MiSTer`, `<Arcade>_MiSTer` | Un repo par core (Apple-I, NES, MegaDrive, PSX, Saturn, N64, C64, etc.) |

Sorgelig reste le mainteneur principal de `Main_MiSTer` et du framework `sys/`. Pas de fork majeur ni de schisme communautaire : la gouvernance est **« BDFL léger »** — Sorgelig conserve l'autorité sur le core framework, les cores eux-mêmes sont déléguées à des mainteneurs spécialisés (alanswx pour l'arcade, robertpeip pour PSX/N64/Saturn, srg320 pour Saturn/PCE, etc.).

### 15.3 Hardware actuel — DE10-Nano et add-ons

Le **Terasic DE10-Nano** reste **la** plateforme officielle. Mais en 2026, il **vieillit** : Intel n'a pas dégagé de successeur Cyclone V SoC abordable, les prix montent (~150 $ neuf), Terasic a parfois eu des ruptures. Le forum discute d'un *« MiSTer 2.0 »* hardware modulaire depuis ~2024 sans qu'aucun design officiel ne soit verrouillé.

Add-ons populaires *(mostly via `misteraddons.com` et vendeurs Amazon)* :

| Add-on | Rôle |
|---|---|
| **SDRAM module 32 MB** *(XS V2.2)* | Suffit pour SNES, Genesis, NES, GB, et la plupart des computers — **insuffisant** pour Neo Geo, GBA, certains arcades |
| **SDRAM module 128 MB** *(XS-D V3.0)* | Recommandé par défaut — couvre Neo Geo, GBA, etc. |
| **Dual SDRAM** *(2× 32 MB)* | Bande passante plutôt que capacité — utile pour Saturn et certains cores demandant deux bus parallèles |
| **IO Board Analog Pro / VGA / Digital** | VGA legacy 6-bit, audio analogique mini-jack, S/PDIF optique, boutons Reset/User/Menu, port User-IO (SNAC pour pads originaux) |
| **USB Hub board** | 4-7 ports USB pour pads, claviers, dongles WiFi/BT |
| **RTC board (DS3231)** | Horloge persistante côté HPS |
| **Aluminum case** | Refroidissement passif + esthétique |

### 15.4 Alternatives au DE10-Nano

Plusieurs initiatives 2024-2026 pour casser la dépendance au DE10-Nano :

| Plateforme | Année | Approche | Compatibilité cores |
|---|---|---|---|
| **MiSTer Pi** *(Taki Udon)* | 2024 | Clone du DE10-Nano basé sur le même Cyclone V SoC, intégré sur un PCB plus petit/moins cher | Annoncée perfect, dans la pratique très bonne |
| **MARS** *(Tang/Sipeed)* | 2024 | FPGA différent (Anlogic / Gowin), plus puissant — pas compatible direct, requiert ré-implémentation des cores | Communauté en construction |
| **Analogue Pocket** *(Analogue)* | 2021 | FPGA Altera Cyclone V GX, mais firmware fermé et écosystème propriétaire (openFPGA depuis 2023) | Cores Pocket-natifs, pas MiSTer |
| **N2P** *(Neptuno+ FPGA)* | — | Plateforme distincte, focus retro 8/16 bits | Pas MiSTer |
| **Discussion forum « MiSTer 2.0 »** | en cours | Objectif : Cyclone 10 GX ou Trion T35 + plus de RAM, garder framework `sys/` | Vaporware pour l'instant |

**Bilan** : le **MiSTer Pi** est aujourd'hui l'alternative la plus mature pour entrer dans l'écosystème à coût réduit ; le DE10-Nano reste la cible canonique pour le développement de cores ; MARS et autres divergent trop pour profiter du framework `sys/`.

### 15.5 Catalogue de cores — état 2026

**~100+ systèmes** sont supportés. Catégories grosses mailles :

- **Computers** : Apple I, Apple II, Amiga 500/600/1200, ZX Spectrum, Atari ST, BBC Micro, MSX, MSX2, C64, C128, PC-XT, PCW, Sharp X68000, TRS-80, ...
- **Consoles 8-bit** : NES/Famicom, Master System, GameBoy, GameBoy Color, Atari 2600/5200/7800, Colecovision, Intellivision, ...
- **Consoles 16-bit** : SNES, Genesis/MegaDrive, PCEngine/TurboGrafx-16, Neo Geo (AES + MVS), ...
- **Consoles 32/64-bit** : PSX, Saturn, N64, 3DO — **présents mais pas full cycle-accurate** (limites VRAM interne du Cyclone V + latence DDR3)
- **Handhelds** : GameBoy, GBC, GBA, Lynx, Game Gear, NeoGeo Pocket, Wonderswan, ...
- **Arcades** : centaines de PCB (CPS1/2, Neo Geo MVS, Sega System 16/18, Konami, etc.) — alanswx + équipe spécialisée

### 15.6 Limites assumées de la plateforme

Plusieurs cores ont publiquement reconnu que **« perfect cycle accuracy »** est hors d'atteinte sur DE10-Nano :

- **PSX** : timings GPU/SPU/CD ne peuvent pas être 100 % accurate à cause de la **DDR3 du HPS** (latence variable, non déterministe). PSX_MiSTer (robertpeip) tourne très bien mais reste un compromis.
- **Saturn** : srg320 a déclaré que même Saturn_MiSTer n'est pas full cycle-accurate.
- **3DO** : le créateur du core a publiquement dit *« cannot be accurate on the MiSTer »* (avril 2026).
- **N64** : possible mais le FPGA est **à la limite des ressources** — robertpeip a fait du travail miraculeux mais des textures et effets de transparence sont encore approximatifs.

Limite structurelle commune : **VRAM interne FPGA insuffisante** (5.5 Mbit ≈ 700 KB) pour certains framebuffers ou caches de tile, et la SDRAM externe / DDR3 introduisent une latence non-déterministe qui casse le « cycle exact ».

### 15.7 Toolchain — précisions importantes

- **Quartus Prime Lite 17.0.x** est la version officiellement supportée. **17.0.2** est la plus recommandée par la communauté. Versions 14, 15, 16 souvent buggées sur Cyclone V SoC.
- **Quartus 13.0sp1** est encore supporté en parallèle (les `.qip` ont des variantes `pll_q13.qip` / `pll_q17.qip` ; `sys.qip` choisit auto via `[regexp -inline {[0-9]+} $quartus(version)]`).
- Pas de version « Pro » nécessaire — Lite gratuite suffit.
- L'IDE Quartus est utilisable **uniquement pour compiler** ; toute édition de fichiers doit passer par `files.qip` à la main, sinon `<core>.qsf` est corrompu.
- **JTAG** via le mini-USB du DE10-Nano (USB-Blaster intégré) → SignalTap II live, `quartus_pgm` pour flasher des `.sof` de test sans rebuild d'image SD.

### 15.8 Workflow utilisateur (côté retro-gamer)

À titre de référence pour comprendre l'expérience finale :

1. SD card flashée avec `mr-fusion` (image officielle).
2. `Downloader_MiSTer` télécharge les cores `.rbf` depuis GitHub releases.
3. Boot DE10-Nano → Linux ARM démarre sur le HPS → Main_MiSTer charge `Menu_MiSTer.rbf` dans le FPGA.
4. Menu OSD : utilisateur choisit un core → Main_MiSTer reconfigure le FPGA via le **FPGA Manager** Cyclone V (le core change en moins d'une seconde).
5. Le nouveau core boot, expose son `CONF_STR`, l'utilisateur charge ROM/jeu → ioctl push depuis SD vers le core → game on.

Le HPS reste actif en arrière-plan — le core peut lui réclamer à tout moment l'OSD, un fichier disque, l'état des USB, le RTC, etc.

### 15.9 Particularités méthodologiques

Quelques traits qui distinguent le développement MiSTer d'un FPGA homebrew classique :

- **Pas de test en simulation pure** — beaucoup de cores valident en compile-and-flash sur le DE10-Nano (le HPS donne accès à des LEDs status + logs ARM côté Linux). Quelques cores ont des testbenches Verilator mais ce n'est pas la norme.
- **Cores monolithiques** — pas de système de plugin runtime ; chaque core est un bitstream complet. Ajouter une « carte d'extension » à un core (ce que POM1 fait massivement en software) implique de modifier le RTL et recompiler.
- **Versionning par date** — les cores sont nommés `<Name>_<YYYYMMDD>.rbf`, la date servant de version. Le HPS prend la dernière par défaut.
- **Pas de save states génériques** — chaque core doit explicitement décider quels registres exposer pour la sauvegarde via le module `nvram` ; pas de snapshot full-fabric à la PCSX2.
- **MRA files** — pour l'arcade, fichier XML qui décrit comment patcher/concaténer plusieurs ROMs en mémoire avant de les passer au core (pareil que ce que fait POM1 avec `loadHexDump` mais avec un format normalisé).
- **MGL files** — chargement de jeu automatique au démarrage d'un core (équivalent de `--preset 14 --load addr:path` côté POM1 CLI).

### 15.10 En résumé

> **MiSTer = un framework FPGA SoC clé-en-main pour cores rétro**.
>
> - Hardware : DE10-Nano (Cyclone V SoC + ARM Cortex-A9), maintenant complété/concurrencé par MiSTer Pi.
> - Software : framework `sys/` qui prend en charge HDMI, audio, OSD, USB, save states, scaling. Le développeur de core écrit uniquement le module `emu` qui décrit son CPU/GPU/RAM ; tout le reste lui est offert.
> - Communauté : organisation GitHub `MiSTer-devel/` cohérente, doc `MkDocs_MiSTer` à jour, forum `misterfpga.org` actif, ~100+ cores maintenus.
> - Limites : Cyclone V atteint ses limites sur les consoles 32-bit ; le **« cycle-perfect »** est faisable sur 8/16-bit mais pas sur N64/PSX/Saturn/3DO.
> - Comparaison Apple-I_MiSTer ↔ alangarf/apple-one : le port MiSTer **emprunte 100 % du cœur** d'alangarf et n'ajoute qu'une fine couche de plumbing (PLL Cyclone V, `ascii_input.v`, `Apple-I.sv` wrapper) — illustration parfaite de ce que le framework fait gagner.
