#!/bin/bash

echo "=== Configuration de POM1 (Dear ImGui + cc65) ==="

# Installation des dépendances sur macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Installation des dépendances macOS..."
    
    # Vérifier si Homebrew est installé
    if ! command -v brew &> /dev/null; then
        echo "Installation de Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    
    # Installer les dépendances
    brew install cmake glfw pkg-config cc65   # cc65 = toolchain 6502 du Bench
    
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Installation des dépendances Linux..."
    
    # Debian / Ubuntu / Linux Mint (cc65 est dans la composante "universe",
    # activée par défaut sur Mint et Ubuntu)
    if command -v apt &> /dev/null; then
        sudo apt update
        sudo apt install -y cmake libglfw3-dev pkg-config libgl1-mesa-dev cc65
    # Fedora/CentOS
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y cmake glfw-devel pkgconfig mesa-libGL-devel cc65
    # Arch Linux (cc65 est dans l'AUR, pas dans les dépôts officiels)
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --needed cmake glfw-x11 pkgconfig mesa
        echo "Note : cc65 (toolchain 6502 du Bench) est dans l'AUR — p.ex. 'yay -S cc65'."
    fi
fi

# Télécharger Dear ImGui
if [ ! -d "imgui" ]; then
    echo "Téléchargement de Dear ImGui (v1.92.7, version testée par POM1)..."
    git clone --depth 1 --branch v1.92.7 https://github.com/ocornut/imgui.git
    echo "Dear ImGui téléchargé avec succès !"
else
    echo "Dear ImGui déjà présent."
fi

# Créer le dossier de build
mkdir -p build
cd build

# Configurer avec CMake
echo "Configuration du projet..."
cmake ..

echo ""
# Vérification cc65 (requis par le POM1 Bench / DEV Edition)
if command -v ca65 &> /dev/null; then
    echo "Toolchain cc65 : $(ca65 --version 2>&1 | head -1)"
else
    echo "ATTENTION : cc65 (ca65/ld65/cl65) introuvable — le POM1 Bench en a besoin."
    echo "  Debian/Ubuntu/Mint : sudo apt install cc65  ·  Fedora : sudo dnf install cc65"
    echo "  macOS : brew install cc65  ·  Arch : AUR (yay -S cc65)"
fi

echo ""
echo "=== Configuration terminée ! ==="
echo ""
echo "Pour compiler le projet :"
echo "  cd build"
echo "  make"
echo ""
echo "Pour exécuter :"
echo "  ./POM1"
echo "" 