#!/bin/bash

echo "Launching POM1 - Apple 1 Emulator (Dear ImGui)"
echo "================================================"

# Portable file size: macOS uses stat -f%z, Linux uses stat -c%s
filesize() {
    stat -f%z "$1" 2>/dev/null || stat -c%s "$1" 2>/dev/null || echo "?"
}

# Build if needed
if [ ! -f "build/pom1_imgui" ]; then
    echo "Emulator not built. Compiling..."
    mkdir -p build
    cd build
    cmake .. && make
    if [ $? -ne 0 ]; then
        echo "ERROR: Build failed"
        exit 1
    fi
    cd ..
fi

# Check and copy ROMs
echo "Checking ROMs..."
roms_found=0

copy_rom() {
    local name="$1"
    local optional="$2"
    if [ -f "build/$name" ]; then
        echo "  OK   $name ($(filesize "build/$name") bytes)"
        return 0
    elif [ -f "roms/$name" ]; then
        cp "roms/$name" "build/"
        echo "  OK   $name copied ($(filesize "build/$name") bytes)"
        return 0
    else
        if [ "$optional" != "optional" ]; then
            echo "  MISS $name not found"
        fi
        return 1
    fi
}

copy_rom "basic.rom"        && roms_found=$((roms_found + 1))
copy_rom "krusader-1.3.rom" && roms_found=$((roms_found + 1))
copy_rom "WozMonitor.rom"   && roms_found=$((roms_found + 1))
copy_rom "charmap.rom" optional

echo "$roms_found/3 required ROM(s) found"
echo ""

# Launch
echo "Starting emulator..."
cd build
./pom1_imgui
