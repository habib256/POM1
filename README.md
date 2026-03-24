# POM1 - Emulateur Apple 1

Emulateur Apple 1 utilisant **Dear ImGui** avec GLFW/OpenGL. Emulation complete du CPU MOS 6502, de la memoire (64 Ko), et du hardware Apple 1 (I/O clavier, affichage, ROMs).

Ce projet est un portage de la version Java originale vers C++ Dear ImGui pour une installation simplifiee et de meilleures performances.

## Fonctionnalites

- **Ecran Apple 1** : grille 40x24, moniteur vert vintage ou blanc, curseur `@` clignotant, effet scanline CRT, echelle ajustable
- **Emulation CPU 6502** : tous les opcodes, modes d'adressage, compteur de cycles, vitesse configurable (1 MHz / 2 MHz / Max)
- **Visualiseur memoire** : editeur hexadecimal interactif avec recherche, navigation rapide, edition en temps reel
- **ROMs incluses** : Woz Monitor (0xFF00), Apple BASIC (0xE000), Krusader assembler (0xA000)
- **Chargement de programmes** : fichiers binaires ou hex dumps au format Woz Monitor (repertoire `soft-asm/`)
- **Debogage** : execution pas-a-pas, reset logiciel/materiel, acces aux registres CPU

## Prerequis

- CMake >= 3.16
- GLFW 3
- OpenGL
- pkg-config
- Compilateur C++17

## Installation

```bash
# Installation automatique des dependances et de Dear ImGui
./setup_imgui.sh

# Build
cd build
cmake ..
make
```

### Linux (manuel)

```bash
# Ubuntu/Debian
sudo apt install cmake libglfw3-dev libgl1-mesa-dev pkg-config

# Fedora
sudo dnf install cmake glfw-devel mesa-libGL-devel pkgconf

# Arch
sudo pacman -S cmake glfw mesa pkgconf
```

### macOS (manuel)

```bash
brew install cmake glfw pkg-config
```

## Lancement

```bash
# Methode recommandee (copie les ROMs et verifie le build)
./run_emulator.sh

# Ou directement
cd build
./pom1_imgui
```

## Programmes disponibles

Le repertoire `soft-asm/` contient des programmes au format hex dump Woz Monitor, chargeables via **File > Load Memory** :

- **Microchess** - Jeu d'echecs
- **Lunar Lander** - Alunissage
- **Game of Life** - Automate cellulaire de Conway
- **LittleTower** - Jeu d'aventure textuel
- **fig-FORTH** - Interpreteur Forth
- **Enhanced BASIC** - BASIC etendu
- Et d'autres...

## Assemblage de programmes (cc65)

```bash
ca65 -o build/program.o source.in
ld65 -C build/apple1.cfg -o build/program.bin build/program.o
```

## Structure du projet

```
POM1/
├── M6502.cpp/h              # Emulation CPU MOS 6502
├── Memory.cpp/h             # Memoire 64 Ko + I/O
├── main_imgui.cpp           # Point d'entree GLFW/OpenGL
├── MainWindow_ImGui.cpp/h   # Fenetre principale + menus
├── Screen_ImGui.cpp/h       # Ecran Apple 1
├── MemoryViewer_ImGui.cpp/h # Editeur memoire hexadecimal
├── roms/                    # ROMs Apple 1 (chargees au demarrage)
├── soft-asm/                # Programmes hex dump
├── fonts/                   # Font Awesome (icones UI)
├── doc/                     # Documentation (manuel Krusader)
├── images/                  # Icones UI
├── CMakeLists.txt           # Configuration CMake
├── setup_imgui.sh           # Script d'installation
└── run_emulator.sh          # Script de lancement
```

## Licence

GPL-3.0 - voir [LICENSE](LICENSE)
