#!/usr/bin/env bash
# build_universal_deps.sh — build POM1's macOS dependencies as UNIVERSAL
# (arm64 + x86_64) binaries, so the release .app is one Universal 2 bundle that
# runs natively on both Apple Silicon and Intel Macs.
#
# Why this exists (the bug it fixes):
#
#   Homebrew only ever installs ONE architecture — the one of the machine (or
#   runner) doing the install. The release job used `brew install glfw`, which
#   made the shipped .app both single-arch AND machine-specific: the linker
#   baked brew's ABSOLUTE prefix into the binary, so a DMG built on the Intel
#   runner died on every Apple Silicon Mac with
#       dyld: Library not loaded: /usr/local/opt/glfw/lib/libglfw.3.dylib
#   which Finder reports as the misleading "POM1 quit unexpectedly".
#
#   Building GLFW from source solves both halves at once: it is compiled for
#   both slices, and it is STATIC — there is no dylib left to resolve at
#   runtime, so no absolute prefix can leak into the bundle.
#
# cc65 gets the same treatment. It ships INSIDE the .app (the in-app DevBench
# runs it), so a single-arch toolchain would mean an Apple Silicon user's
# DevBench silently depends on Rosetta being installed — or, worse, an arm64
# toolchain simply cannot execute on an Intel Mac at all.
#
# Only what POM1's DevBench needs is built (`make -C src <tools>` +
# `make -C libsrc none`), mirroring packaging/windows/build_cc65.ps1 — not the
# full cc65 snapshot, which builds every 6502 target library and is far slower.
#
# Output (a prefix ready to hand to CMake and to build_cc65_bundle.sh):
#   <out>/glfw/lib/libglfw3.a + <out>/glfw/lib/cmake/glfw3/…
#   <out>/cc65-src/            (built cc65 tree: bin/ asminc/ include/ lib/ cfg/)
#
# Usage:
#   packaging/macos/build_universal_deps.sh [--out DIR] [--jobs N]
#
# Env overrides: GLFW_TAG (default 3.4), CC65_REV (default master — same as the
# Windows packager), POM1_MACOS_ARCHS (default "arm64;x86_64").

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

OUT="${REPO_ROOT}/build-universal-deps"
JOBS="$(sysctl -n hw.ncpu)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)     OUT="$2"; shift 2;;
        --jobs)    JOBS="$2"; shift 2;;
        -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
        *) echo "unknown arg: $1" >&2; exit 2;;
    esac
done

GLFW_TAG="${GLFW_TAG:-3.4}"
CC65_REV="${CC65_REV:-master}"
ARCHS="${POM1_MACOS_ARCHS:-arm64;x86_64}"
# The same list in the two other spellings the tools below want.
ARCH_LIST="${ARCHS//;/ }"                      # "arm64 x86_64"
ARCH_FLAGS=""; for a in $ARCH_LIST; do ARCH_FLAGS="$ARCH_FLAGS -arch $a"; done

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

echo "============================================"
echo " POM1 — universal macOS deps"
echo "   archs : $ARCHS"
echo "   glfw  : $GLFW_TAG"
echo "   cc65  : $CC65_REV"
echo "   out   : $OUT"
echo "============================================"

# ---------- GLFW (static, universal) ----------------------------------------
GLFW_SRC="$OUT/glfw-src"
GLFW_PREFIX="$OUT/glfw"
if [[ ! -f "$GLFW_PREFIX/lib/libglfw3.a" ]]; then
    echo "==> GLFW $GLFW_TAG"
    [[ -d "$GLFW_SRC" ]] || git clone --depth 1 --branch "$GLFW_TAG" \
        https://github.com/glfw/glfw.git "$GLFW_SRC"
    # BUILD_SHARED_LIBS=OFF is the point: a static libglfw3.a is linked INTO the
    # POM1 binary, so nothing has to be found on the user's filesystem at launch.
    cmake -S "$GLFW_SRC" -B "$OUT/glfw-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
        -DCMAKE_INSTALL_PREFIX="$GLFW_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DGLFW_BUILD_EXAMPLES=OFF \
        -DGLFW_BUILD_TESTS=OFF \
        -DGLFW_BUILD_DOCS=OFF >/dev/null
    cmake --build "$OUT/glfw-build" -j"$JOBS" >/dev/null
    cmake --install "$OUT/glfw-build" >/dev/null
fi
echo "    libglfw3.a : $(lipo -info "$GLFW_PREFIX/lib/libglfw3.a" | sed 's/.*: //')"

# ---------- cc65 (universal) -------------------------------------------------
# Two single-arch passes + lipo, rather than one pass with -arch twice. cc65's
# makefiles append to CFLAGS/LDFLAGS, and a command-line assignment overrides a
# makefile's `+=` outright — so injecting the arch flags that way would silently
# drop cc65's own required flags. Per-arch builds keep its flags intact and the
# merge is done afterwards by lipo, which is exactly what lipo is for.
CC65_SRC="$OUT/cc65-src"
CC65_BINS=(ca65 ld65 cl65 cc65 ar65)
if [[ ! -x "$CC65_SRC/bin/ca65" ]]; then
    echo "==> cc65 $CC65_REV"
    if [[ ! -d "$CC65_SRC" ]]; then
        git clone --depth 1 --branch "$CC65_REV" https://github.com/cc65/cc65.git "$CC65_SRC" \
            || { git clone https://github.com/cc65/cc65.git "$CC65_SRC"
                 git -C "$CC65_SRC" checkout --quiet "$CC65_REV"; }
    fi
    SLICES=()
    for a in $ARCH_LIST; do
        echo "    building tools for $a"
        make -C "$CC65_SRC/src" -j"$JOBS" CC="clang -arch $a" >/dev/null
        SLICE="$OUT/cc65-slice-$a"
        rm -rf "$SLICE"; mkdir -p "$SLICE"
        for b in "${CC65_BINS[@]}"; do cp "$CC65_SRC/bin/$b" "$SLICE/$b"; done
        SLICES+=("$SLICE")
        # Force a full rebuild for the next arch — the object files are per-arch.
        make -C "$CC65_SRC/src" clean >/dev/null 2>&1 || true
    done
    echo "    lipo -create"
    mkdir -p "$CC65_SRC/bin"
    for b in "${CC65_BINS[@]}"; do
        INPUTS=(); for s in "${SLICES[@]}"; do INPUTS+=("$s/$b"); done
        lipo -create "${INPUTS[@]}" -output "$CC65_SRC/bin/$b"
    done
    # none.lib is 6502 object code — architecture-independent, built once by the
    # (now universal) tools we just merged.
    echo "    building none.lib"
    mkdir -p "$CC65_SRC/lib"
    make -C "$CC65_SRC/libsrc" none >/dev/null
fi
for b in "${CC65_BINS[@]}"; do
    echo "    $b : $(lipo -info "$CC65_SRC/bin/$b" | sed 's/.*: //')"
done
[[ -f "$CC65_SRC/lib/none.lib" ]] || { echo "ERROR: cc65 none.lib missing" >&2; exit 1; }

echo
echo "============================================"
echo "  Universal deps ready."
echo "    GLFW prefix    : $GLFW_PREFIX"
echo "    cc65 build dir : $CC65_SRC"
echo
echo "  Consume with:"
echo "    CMAKE_PREFIX_PATH=$GLFW_PREFIX"
echo "    CC65_BIN_DIR=$CC65_SRC/bin CC65_SHARE_DIR=$CC65_SRC"
echo "============================================"
