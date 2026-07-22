#!/usr/bin/env bash
# build_in_bionic.sh — the Linux release: COMPILE + PACKAGE only.
#
# The glibc-2.27 toolchain (g++-9, CMake, GLFW 3.3, cc65, appimagetool,
# linuxdeploy, python3.8) is BAKED into the pinned builder image
# packaging/linux/Dockerfile.bionic — see the `linux` job in release.yml and the
# build-bionic-image workflow. This script runs INSIDE that image with the repo
# bind-mounted at /work; nothing here touches apt or downloads a toolchain, so a
# release no longer depends on bionic's flaky EOL mirrors (that fragility moved to
# the one-off image build). The build is reproducible by the image digest.
#
# WHY `docker run` AND NOT a `container:` KEY: GitHub's node20 actions
# (checkout@v4, upload-artifact@v4) need glibc >= 2.28 to run — one notch above
# bionic's 2.27 — so they can't execute inside a bionic `container:`. The actions
# run on the host; this script runs in the container with the repo bind-mounted.
#
# Honoured env: POM1_VERSION, POM1_REQUIRE_CC65, IMGUI_TAG.
# Provided by the image ENV: PATH (CMake), CC65_BIN_DIR / CC65_SHARE_DIR,
# POM1_APPIMAGE_TOOLS_DIR, gcc/g++ -> 9, python3 -> 3.8.
set -euxo pipefail

# The bind-mounted repo is owned by the host uid, not root — let git touch it.
git config --global --add safe.directory '*'

# --- Stage the cc65 bundle from the baked toolchain --------------------------
#     CC65_BIN_DIR / CC65_SHARE_DIR come from the image ENV (/opt/cc65-install).
tools/build_cc65_bundle.sh --out dist/cc65-bundle

# --- Dear ImGui (pinned; matches the macOS/Windows jobs) ---------------------
#     Kept as a fresh clone (not baked) so the image stays decoupled from the
#     imgui version — a pinned github.com tag is far more reliable than the
#     bionic apt mirrors this rework was built to escape.
rm -rf imgui
git clone --depth 1 --branch "${IMGUI_TAG:-v1.92.7}" https://github.com/ocornut/imgui.git

# --- Build POM1 --------------------------------------------------------------
#     -static-libstdc++/-static-libgcc so the ONLY libc-family floor the AppImage
#     imposes is glibc (2.27) — g++-9's newer GLIBCXX/libgcc symbols would
#     otherwise raise the floor above what Mint 19 ships.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build -j"$(nproc)"

# --- Package the AppImage ----------------------------------------------------
#     Binary already built (SKIP_BUILD); appimagetool/linuxdeploy are baked in the
#     image at POM1_APPIMAGE_TOOLS_DIR so build_appimage.sh downloads nothing.
#     APPIMAGE_EXTRACT_AND_RUN: the container has no FUSE.
export POM1_APPIMAGE_SKIP_BUILD=1
export APPIMAGE_EXTRACT_AND_RUN=1
packaging/linux/build_appimage.sh
