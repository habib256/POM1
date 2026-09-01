#!/bin/bash
# ===========================================================================
# setup_pom1.sh — one-time developer setup: system dependencies, Dear ImGui,
# and a configured build/ directory.
#
# LANGUAGE: this script's output is the FIRST thing a new contributor sees, and
# the documentation is in English. Keep the messages here in English too.
# ===========================================================================

echo "=== POM1 setup (Dear ImGui + cc65) ==="

# --- macOS dependencies ----------------------------------------------------
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Installing macOS dependencies..."

    if ! command -v brew &> /dev/null; then
        echo "Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi

    brew install cmake glfw pkg-config cc65   # cc65 = the Bench's 6502 toolchain

elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Installing Linux dependencies..."

    # Debian / Ubuntu / Linux Mint (cc65 lives in the "universe" component,
    # which is enabled by default on Mint and Ubuntu)
    if command -v apt &> /dev/null; then
        sudo apt update
        sudo apt install -y cmake libglfw3-dev pkg-config libgl1-mesa-dev cc65
    # Fedora / CentOS
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y cmake glfw-devel pkgconfig mesa-libGL-devel cc65
    # Arch Linux (cc65 is in the AUR, not in the official repositories)
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --needed cmake glfw-x11 pkgconfig mesa
        echo "Note: cc65 (the Bench's 6502 toolchain) is in the AUR — e.g. 'yay -S cc65'."
    fi
fi

# --- Dear ImGui ------------------------------------------------------------
# Not vendored; the same logic the Pi installer and the CI workflows use. See
# tools/ensure_imgui.sh: it requires the docking branch AND the version pin,
# retries a failed network fetch, and upgrades a stale checkout instead of
# letting it break the build much further along.
if ! "$(dirname "$0")/tools/ensure_imgui.sh" imgui; then
    echo "ERROR: Dear ImGui could not be installed." >&2
    exit 1
fi

# --- Configure the build ---------------------------------------------------
mkdir -p build
cd build

echo "Configuring the project..."
cmake ..

echo ""
# cc65 is needed by the POM1 Bench (the in-app 6502 development environment).
# Its absence is a WARNING, not an error: the emulator itself builds and the
# `ctest -L emulator` release gate is green without it — the toolchain-dependent
# tests skip rather than fail.
if command -v ca65 &> /dev/null; then
    echo "cc65 toolchain: $(ca65 --version 2>&1 | head -1)"
else
    echo "WARNING: cc65 (ca65/ld65/cl65) not found — the POM1 Bench needs it."
    echo "  Debian/Ubuntu/Mint: sudo apt install cc65  ·  Fedora: sudo dnf install cc65"
    echo "  macOS: brew install cc65  ·  Arch: AUR (yay -S cc65)"
    echo "  The emulator builds and tests fine without it."
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "Build:"
echo "  cd build && make"
echo ""
echo "Run:"
echo "  ./POM1        (or ./run_emulator.sh from the repo root)"
echo ""
echo "Test:"
echo "  cd build && ctest        # ~2 min, 126 tests"
echo ""
echo "New here? Read ARCHITECTURE.md — it is the human entry point."
echo ""
