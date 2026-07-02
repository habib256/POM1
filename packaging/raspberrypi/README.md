# POM1 en mode borne (kiosk) sur Raspberry Pi 400

Fait démarrer le Raspberry Pi 400 **directement dans POM1, en plein écran**,
sans bureau. Une fois dedans, tout se fait depuis les menus de POM1 : écouter
une cassette (lecteur ACI / musique), charger et lancer un programme, ouvrir
les éditeurs d'images HGR / TMS9918, etc.

Cible : **Raspberry Pi OS Bookworm 64-bit** (aarch64).

## Installation

Sur le Pi, avec le dépôt déjà cloné :

```bash
cd /chemin/vers/POM1
./packaging/raspberrypi/install.sh
sudo reboot
```

Le script (idempotent, à lancer en utilisateur normal — **pas** en `sudo`) :

1. installe les paquets (build + une session X minimale : `xinit`,
   `matchbox-window-manager`, `unclutter`) ;
2. récupère Dear ImGui (`v1.92.7`) s'il manque ;
3. compile `build/POM1` ;
4. active la **connexion automatique** en console sur `tty1` ;
5. écrit `~/.xinitrc` (matchbox plein écran + boucle POM1) et le déclencheur
   `startx` dans `~/.bash_profile`.

Au reboot : autologin `tty1` → `startx` → matchbox → POM1 plein écran, relancé
automatiquement s'il se ferme.

## Le piège OpenGL (important)

POM1 demande un contexte **OpenGL 3.2 Core** (`src/main_imgui.cpp`). Le GPU du
Pi 400 (VideoCore VI / V3D) n'expose en OpenGL desktop que la **3.1** : sans
rien, la fenêtre ne se crée pas.

Le lanceur `pom1-kiosk.sh` pose donc :

```
MESA_GL_VERSION_OVERRIDE=3.3
MESA_GLSL_VERSION_OVERRIDE=150
```

Mesa rapporte alors 3.3, la requête 3.2 Core passe, et comme ImGui n'utilise
que des fonctions présentes dès la 3.1, tout rend correctement.

> **Plan B** si l'écran reste noir / erreur de contexte : abaisser la requête
> dans `src/main_imgui.cpp` (bloc `#else`, ~ligne 696) de `3,2` + core profile
> vers `3,1` sans `GLFW_OPENGL_CORE_PROFILE`, puis recompiler. À n'utiliser que
> si la surcharge Mesa ne suffit pas.

## Son (écouter les cassettes)

L'audio (miniaudio) sort par HDMI ou la prise jack selon la config du Pi.
Choisir la sortie : `sudo raspi-config` → *System Options* → *Audio*. Tester :

```bash
speaker-test -t wav -c2
```

## Sortir du mode borne

Depuis une autre console (`Ctrl+Alt+F2`) ou en `ssh` :

```bash
./packaging/raspberrypi/install.sh --disable-kiosk
```

Cela retire `~/.xinitrc` et le déclencheur `startx`. Repasse ensuite le
démarrage en mode normal via `sudo raspi-config` (*Boot / Auto Login*), puis
`sudo reboot`.

## Fichiers

| Fichier            | Rôle                                                             |
|--------------------|-----------------------------------------------------------------|
| `install.sh`       | Installe / compile / configure la borne (et `--disable-kiosk`).  |
| `pom1-kiosk.sh`    | Lanceur : surcharges Mesa + `cd` racine dépôt + `./build/POM1`.  |
| `xinitrc.example`  | Référence lisible du `~/.xinitrc` généré par `install.sh`.       |

## Notes

- POM1 résout ses données (`roms/`, `software/`, `cassettes/`, `sdcard/`,
  `fonts/`, `pic/`) **par rapport au répertoire courant** ; le lanceur se place
  donc à la racine du dépôt. Ne déplace pas le binaire hors de `build/` sans
  garder l'arborescence du dépôt à côté.
- Le plein écran est assuré par `matchbox-window-manager` (il maximise
  l'unique fenêtre, dont le `WM_CLASS` est `POM1`). Pas de flag `--fullscreen`
  côté POM1.
- La session X native (X11) est choisie volontairement plutôt que Wayland :
  POM1 est une appli GLFW/X11 et le pilote V3D est le plus stable ainsi.
