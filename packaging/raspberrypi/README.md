# POM1 en mode borne (kiosk) sur Raspberry Pi

Fait démarrer le Raspberry Pi **directement dans POM1, en plein écran**, sans
bureau. Une fois dedans, tout se fait depuis les menus de POM1 : écouter une
cassette (lecteur ACI / musique), charger et lancer un programme, ouvrir les
éditeurs d'images HGR / TMS9918, etc.

Cible : **Raspberry Pi OS Lite Bookworm 64-bit** (aarch64), Pi 4 / 400 / 5.

## Installation

Sur le Pi, avec le dépôt déjà cloné :

```bash
cd /chemin/vers/POM1
./packaging/raspberrypi/install.sh          # build + borne
sudo reboot
```

Variantes utiles :

| Commande | Effet |
|----------|-------|
| `install.sh --pgo` | compile en **deux passes guidées par profil** (~2× plus long, 10-20 % de CPU en moins ensuite) |
| `install.sh --no-kiosk` | compile seulement, ne touche pas au système |
| `install.sh --keep-wifi` | garde le Wi-Fi (borne en réseau / SSH sans câble) |
| `install.sh --desktop-gl` | compile sans le palier GLES (repli, cf. plus bas) |
| `install.sh --uninstall` | défait le mode borne |

`install.sh` n'est qu'un orchestrateur ; le travail est réparti :

| Fichier | Rôle |
|---------|------|
| `install.sh` | paquets, Dear ImGui, puis appelle les deux suivants |
| `build_native_pi.sh` | compilation : palier GLES, `-mcpu` du cœur réel, `--pgo`, LTO |
| `pgo_train.sh` | parcours d'entraînement headless pour le PGO |
| `install_kiosk.sh` | durcissement système + service systemd (`--uninstall`) |
| `pom1-kiosk.sh` | point d'entrée du service : démarre un **X nu** |
| `pom1-session.sh` | dans X : écran noir, pas d'économiseur, `exec POM1 --fullscreen` |
| `pom1-kiosk@.service` | l'unité systemd (RT audio, `Restart=always`, VT 1) |

## Ce que la borne change, et pourquoi

Recette reprise de la borne NeoST, où chaque point a été choisi pour un gain
mesurable — pas pour faire propre :

1. **X nu** (ni gestionnaire de fenêtres ni compositeur) sur le VT 1, lancé par
   systemd, POM1 en `--fullscreen`. Sur un bureau, chaque trame est recopiée une
   fois de plus par le compositeur avant d'atteindre l'écran. C'est le plein
   écran de POM1 lui-même qui remplace l'ancien `matchbox-window-manager`
   (toujours disponible : `POM1_WM="matchbox-window-manager"` dans la conf).
2. **Aucun serveur de son** : miniaudio parle à ALSA en direct (HDMI, carte
   détectée par l'ELD — le Pi 400 a deux sorties et pas de jack). L'unité pose
   `LimitRTPRIO=99` : sans lui, la demande `SCHED_FIFO` du thread audio de
   miniaudio **échoue en silence** et le tampon part en underrun.
3. **Gouverneur `performance`** sur tous les cœurs. Pi OS met `ondemand`, dont
   les rampes de fréquence produisent le hachage périodique « ça rame par
   à-coups ».
4. **IRQ épinglées sur le cœur 0** (`irqaffinity=0`) : la boucle d'émulation ne
   partage plus son cœur avec l'USB/Ethernet.
5. **Bluetooth coupé** au device-tree, **Wi-Fi** aussi *si* un câble Ethernet
   est branché (garde-fou : sinon on se couperait de SSH).
6. **Swap coupé** (un swap-in sur carte SD au milieu d'une trame = trou audio
   garanti), services inutiles coupés, boot silencieux, `arm_boost=1`.

Non porté : le mode **enceinte Bluetooth** de NeoST (PipeWire + A2DP). Il
faudrait réintroduire tout le serveur de son que le point 2 retire. Marche à
suivre si nécessaire : installer PipeWire à la main puis
`install_kiosk.sh --keep-audio-server` — miniaudio classe PulseAudio avant ALSA,
donc POM1 se branche tout seul sur `pipewire-pulse` (prévoir 150-250 ms de
retard son/image, inhérents à l'A2DP).

## Compilation : ce que `build_native_pi.sh` fait de plus

- **`-DPOM1_GLES=ON`** — le V3D du Pi (Mesa) plafonne à OpenGL **3.1** côté
  desktop ; le palier GLES 3.0 (contexte EGL) est le chemin que ce pilote
  implémente réellement. C'est ce qui remplace la vieille rustine
  `MESA_GL_VERSION_OVERRIDE=3.3` du lanceur.
- **`-mcpu=cortex-a72` / `-a76` / `-a53`** selon `/proc/device-tree/model` (et
  non `-mcpu=native`, dont la détection retombe silencieusement sur générique
  sur certains noyaux 64 bits).
- **`--pgo`** — deux passes : binaire instrumenté, parcours d'entraînement
  (`pgo_train.sh`), puis recompilation avec le profil. La boucle chaude de POM1
  est l'interpréteur M6502 : un branchement indirect sur l'opcode suivi de
  beaucoup de branches rares. Avec le profil, GCC range les blocs pour que le
  cas fréquent tombe en séquence — cache d'instructions bien mieux utilisé, ce
  qui compte double sur un Cortex-A72 (32 Ko de L1i).
  ⚠ Deux pièges refermés dans le script : les deux passes **partagent le même
  répertoire de build** (GCC nomme les `.gcda` d'après le chemin absolu de
  l'objet — instrumenter ailleurs ne trouve aucun profil, et
  `-Wno-missing-profile` rend l'échec totalement muet), et le script **échoue**
  si aucun profil n'a été collecté pour `M6502`, `Memory` et `GraphicsCard`.
  ⚠ Limite assumée : le parcours d'entraînement est **headless**, donc les TU
  d'interface (ImGui, `Screen_ImGui`, pile CRT, éditeurs) ne sont pas entraînées.
- **LTO** — déjà activé en Release par le `CMakeLists.txt` ; le script le coupe
  (`-DPOM1_LTO=OFF`) sous 2 Go de RAM, où le lien se fait tuer par l'OOM-killer
  (symptôme : `cc1plus: fatal error: Killed signal terminated program`).
- **`-j`** calé sur la RAM (une tâche par ~900 Mo), pas sur `nproc`.

## Compiler la borne EN CI plutôt que sur le Pi

`build_native_pi.sh --pgo` fait payer **deux compilations complètes plus le
parcours d'entraînement au Pi lui-même** — plusieurs heures sur un Pi 400, avec
le risque d'OOM-kill au lien LTO. Le workflow **« Borne Raspberry Pi »**
(`.github/workflows/pi-borne.yml`, recette portée de NeoST) fait exactement le
même travail sur un runner ARM64 de GitHub, où c'est gratuit :

```bash
gh workflow run "Borne Raspberry Pi (aarch64, -mcpu ciblé)" \
   --ref main -f mcpu=cortex-a72          # Pi 4 / Pi 400
gh run download <run-id> -n pom1-borne-aarch64
```

Le build tourne **dans un conteneur `debian:bookworm`** et non sur le runner :
Raspberry Pi OS *est* bookworm (glibc 2.36), alors que `ubuntu-24.04-arm`
estamperait `GLIBC_2.39` — le binaire ne démarrerait sur aucun Pi. Le script
(`build_in_bookworm_pi.sh`) vérifie ce plancher et échoue s'il est dépassé. Le
conteneur s'exécute nativement : pas de QEMU, pas de taxe d'émulation.

Il en sort **deux paquets issus du même build**, sans recompilation :

| Paquet | Pour quoi |
|---|---|
| `POM1-<ver>-pi400-aarch64.AppImage` | Pi OS **avec bureau** — un fichier cliquable |
| `pom1-pi400-aarch64.tar.gz` | borne **sans bureau** — `tar -xzf … -C /home/pi/POM1` |

Le tar.gz existe parce qu'une AppImage v2 réclame **libfuse2, absent de Pi OS
Lite** : la borne devrait l'extraire à chaque démarrage pour rien. Son
arborescence est celle du dépôt (`build/POM1` + `roms/`, `software/`, …) parce
que c'est exactement ce qu'attend `pom1-session.sh` — POM1 résout ses données
par rapport au répertoire courant. Les scripts de borne voyagent dedans, donc
l'arbre déballé sait s'installer lui-même via `install_kiosk.sh`.

Il est **autoportant** : il est dérivé de l'AppDir de linuxdeploy, donc il
embarque ses bibliothèques dans `lib/` et le binaire porte le RUNPATH
`$ORIGIN/../lib`. C'est indispensable — **Pi OS Lite n'installe pas `libglfw3`**,
et un paquet dont tout l'intérêt est de ne rien compiler sur la borne ne peut pas
exiger un `apt install` pour démarrer. La CI le prouve en lançant le tar.gz sur
un runner où `libglfw3` n'est **pas** installée, et le script échoue si le
RUNPATH attendu a disparu.

⚠ Ces paquets **ne remplacent pas** l'AppImage de release
`POM1-<ver>-aarch64.AppImage`, qui reste **générique** (Pi 3 → Pi 5) là où
celle-ci est compilée pour **un seul cœur**. D'où le tag `pi400`/`pi5`/`pi3`
dans le nom : le job `publish` d'une release aplatit tous les artefacts dans un
même dossier, et deux paquets homonymes s'y écraseraient en silence.

## Le piège OpenGL du Pi (résolu dans le code)

POM1 demandait un contexte **OpenGL 3.2 Core** et le GPU du Pi n'expose que la
**3.1** en OpenGL desktop : la fenêtre ne se créait pas, d'où les surcharges
`MESA_GL_VERSION_OVERRIDE=3.3` de l'ancien lanceur. Deux changements suppriment
la rustine :

- `main_imgui.cpp` redescend en cascade **3.2 core → 3.2 → 3.1 → 3.0** ;
- le préambule GLSL n'est plus figé : ImGui reçoit
  `150 → 140 → 130` selon `GL_SHADING_LANGUAGE_VERSION`
  (`PomRenderer_GL.cpp`) et la pile CRT essaie la même cascade en compilant
  réellement chaque dialecte (`OpenGLShader.cpp`) — ses shaders n'utilisent que
  des constructions GLSL 1.30. Le V3D plafonne à GLSL **1.40**, ce qui faisait
  échouer les effets CRT avec « GLSL 1.50 is not supported ».

Une ligne au démarrage dit ce qui a été retenu : `[CRT] GLSL 140 (driver: 1.40)`
(et `[GL] ImGui shaders: #version 140 instead of #version 150`).
Le chemin recommandé reste le palier **GLES** ; la cascade est le filet.

## Réglages de la borne — `/etc/pom1-kiosk.conf`

Créé une seule fois (une réinstallation ne l'écrase pas) :

| Clé | Rôle |
|-----|------|
| `POM1_ROOT` / `POM1_BIN` | racine du dépôt (POM1 y résout `roms/`, `software/`, `cassettes/`…) et binaire |
| `POM1_PRESET` | profil de machine au démarrage (`POM1 --list-presets`) ; vide = `ini/startup` |
| `POM1_AUDIO_LATENCY` | coussin audio en ms (`--audio-latency`, défaut borne 120, borné [20,250]) |
| `POM1_WM` | vide = X nu + `--fullscreen` ; sinon un WM (repli `matchbox-window-manager`) |
| `POM1_EXTRA_ARGS` | arguments supplémentaires (`--cpu-max`, `--tape …`) |

Après modification : `sudo systemctl restart pom1-kiosk@<utilisateur>`.

## Son qui craque ?

Dans l'ordre : vérifier que le service a bien `LimitRTPRIO=99`
(`systemctl show pom1-kiosk@pi -p LimitRTPRIO`), puis monter
`POM1_AUDIO_LATENCY` à 150. Le défaut hors borne (~17 ms) est calibré pour un
bureau ; sur un Pi chargé, la miss se produit dans l'ordonnanceur, pas dans
l'émulateur. Sortie audio disponible : `aplay -l`, test : `speaker-test -t wav -c2`.

## Dialogues de fichiers : navigateur ImGui par défaut

Sur Raspberry Pi, POM1 utilise **son propre navigateur de fichiers intégré**
(instantané, dessiné dans la fenêtre) au lieu du sélecteur natif GTK/KDE : la
borne tourne sans bureau, un `zenity` forké met plusieurs secondes à démarrer
depuis la carte SD et peut s'ouvrir *derrière* la fenêtre plein écran. La
détection est double — palier GLES natif **et** sonde `/proc/device-tree/model`,
pour couvrir aussi un build `--desktop-gl`. Pour repasser au sélecteur du
système : *Settings ▸ Native OS file dialogs* (mémorisé dans `ini/ui.settings`).

## Zoom de l'interface (téléviseur, 4K, petits écrans)

*Settings ▸ UI Theme ▸ Interface zoom* (75–250 %) agrandit **toute** l'interface :
polices, marges, boutons, barres et panneaux dockés. Mémorisé dans
`ini/ui.settings` (`ui_scale`).

## Sortir du mode borne

Depuis une autre console (`Ctrl+Alt+F2`) ou en `ssh` :

```bash
./packaging/raspberrypi/install.sh --uninstall
sudo reboot
```

Cela retire le service, le gouverneur, et rend `config.txt` / `cmdline.txt` à
leur état d'origine (sauvegardes `*.pom1-orig`). Le dépôt, `/etc/pom1-kiosk.conf`
et les paquets installés restent en place.

## Notes

- POM1 résout ses données **par rapport au répertoire courant** ;
  `pom1-session.sh` se place donc à la racine du dépôt. Ne déplace pas le binaire
  hors de `build/` sans garder l'arborescence du dépôt à côté.
- La session X native (X11) est choisie volontairement plutôt que Wayland : le
  `libglfw3` de bookworm est **X11 uniquement**, et le pilote V3D est le plus
  stable ainsi.
- Les getty 2-6 restent actifs (`Ctrl+Alt+F2` = porte de service). À retirer de
  `pom1-kiosk@.service` si le public a accès à un clavier complet.
- ⚠ Ces scripts sont portés de NeoST et **n'ont pas encore été rejoués sur un Pi
  réel** dans cette version ; les journaux `[Audio]` et `[CRT] GLSL …` du service
  sont ce qui le confirmera.
