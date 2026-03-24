#!/bin/bash

echo "🍎 Lancement de POM1 - Émulateur Apple 1 (Dear ImGui)"
echo "=================================================="

# Vérifier que le build existe
if [ ! -f "build/pom1_imgui" ]; then
    echo "❌ L'émulateur n'est pas compilé. Exécution de la compilation..."
    if [ ! -d "build" ]; then
        mkdir build
    fi
    cd build
    cmake .. && make
    if [ $? -ne 0 ]; then
        echo "❌ Erreur de compilation"
        exit 1
    fi
    cd ..
fi

# Vérifier et copier les ROMs si nécessaire
echo "🔍 Vérification des ROMs..."
roms_found=0

# BASIC ROM
if [ -f "build/basic.rom" ]; then
    echo "✅ BASIC ROM trouvée ($(stat -f%z build/basic.rom) octets)"
    roms_found=$((roms_found + 1))
elif [ -f "roms/basic.rom" ]; then
    echo "📋 Copie de BASIC ROM vers build/..."
    cp "roms/basic.rom" "build/"
    echo "✅ BASIC ROM copiée ($(stat -f%z build/basic.rom) octets)"
    roms_found=$((roms_found + 1))
else
    echo "❌ BASIC ROM non trouvée"
fi

# Krusader ROM
if [ -f "build/krusader-1.3.rom" ]; then
    echo "✅ Krusader ROM trouvée ($(stat -f%z build/krusader-1.3.rom) octets)"
    roms_found=$((roms_found + 1))
elif [ -f "roms/krusader-1.3.rom" ]; then
    echo "📋 Copie de Krusader ROM vers build/..."
    cp "roms/krusader-1.3.rom" "build/"
    echo "✅ Krusader ROM copiée ($(stat -f%z build/krusader-1.3.rom) octets)"
    roms_found=$((roms_found + 1))
else
    echo "❌ Krusader ROM non trouvée"
fi

# WOZ Monitor ROM
if [ -f "build/WozMonitor.rom" ]; then
    echo "✅ WOZ Monitor ROM trouvée ($(stat -f%z build/WozMonitor.rom) octets)"
    roms_found=$((roms_found + 1))
elif [ -f "roms/WozMonitor.rom" ]; then
    echo "📋 Copie de WOZ Monitor ROM vers build/..."
    cp "roms/WozMonitor.rom" "build/"
    echo "✅ WOZ Monitor ROM copiée ($(stat -f%z build/WozMonitor.rom) octets)"
    roms_found=$((roms_found + 1))
else
    echo "❌ WOZ Monitor ROM non trouvée"
fi

# Charmap ROM (optionnelle)
if [ -f "build/charmap.rom" ]; then
    echo "✅ Charmap ROM trouvée ($(stat -f%z build/charmap.rom) octets)"
elif [ -f "roms/charmap.rom" ]; then
    echo "📋 Copie de Charmap ROM vers build/..."
    cp "roms/charmap.rom" "build/"
    echo "✅ Charmap ROM copiée ($(stat -f%z build/charmap.rom) octets)"
fi

echo "📊 $roms_found ROM(s) trouvée(s)"
echo ""

# Lancer l'émulateur
echo "🚀 Lancement de l'émulateur..."
echo "   (Fermez la fenêtre ou appuyez Ctrl+C pour quitter)"
echo ""

cd build
./pom1_imgui

echo ""
echo "👋 Émulateur fermé. Au revoir !" 