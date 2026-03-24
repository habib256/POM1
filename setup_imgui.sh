#!/bin/bash

echo "=== Configuration de POM1 avec Dear ImGui ==="

# Installation des dépendances sur macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Installation des dépendances macOS..."
    
    # Vérifier si Homebrew est installé
    if ! command -v brew &> /dev/null; then
        echo "Installation de Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    
    # Installer les dépendances
    brew install cmake glfw pkg-config
    
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Installation des dépendances Linux..."
    
    # Ubuntu/Debian
    if command -v apt &> /dev/null; then
        sudo apt update
        sudo apt install -y cmake libglfw3-dev pkg-config libgl1-mesa-dev
    # Fedora/CentOS
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y cmake glfw-devel pkgconfig mesa-libGL-devel
    # Arch Linux
    elif command -v pacman &> /dev/null; then
        sudo pacman -S cmake glfw-x11 pkgconfig mesa
    fi
fi

# Télécharger Dear ImGui
if [ ! -d "imgui" ]; then
    echo "Téléchargement de Dear ImGui..."
    git clone https://github.com/ocornut/imgui.git
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
echo "=== Configuration terminée ! ==="
echo ""
echo "Pour compiler le projet :"
echo "  cd build"
echo "  make"
echo ""
echo "Pour exécuter :"
echo "  ./pom1_imgui"
echo "" 