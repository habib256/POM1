#!/usr/bin/env bash
# build_in_bookworm_arm64.sh — the Raspberry Pi release: install, compile, package.
#
# Runs INSIDE a `debian:bookworm` arm64 container with the repo bind-mounted at
# /work (see the `raspberry` job in release.yml). Bookworm is what Raspberry Pi
# OS is built on today — Pi 5 has never shipped anything older — so its glibc
# 2.36 is the floor this AppImage imposes. Building on the runner's own
# ubuntu-24.04-arm image instead would stamp GLIBC_2.39 and the result would
# refuse to start on every Pi in the field: exactly the failure the x86_64 job
# already solved by pinning bionic.
#
# The container runs NATIVELY on GitHub's arm64 runner — no QEMU, no emulation
# tax. This is a compile job like any other; it just happens to have a different
# glibc than the host.
#
# GLES, NOT DESKTOP GL — the reason this job exists at all. Mesa's V3D driver
# (Pi 4/5) caps *desktop* OpenGL at 3.1, so POM1's default GL 3.2 core context
# request simply fails there and the emulator never opens a window. -DPOM1_GLES=ON
# asks for OpenGL ES 3.0 through EGL instead, which V3D does expose. The C++ is
# the same either way (POM1Build.h's POM1_GL_ES picks headers, the `#version
# 300 es` shader prologue and direct entry-point linking); only the context
# creation and the -lGLESv2/-lEGL link line change.
#
# Honoured env: POM1_VERSION, POM1_REQUIRE_CC65, IMGUI_TAG.
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive

# --- Toolchain + build deps --------------------------------------------------
#     libgles2-mesa-dev / libegl1-mesa-dev are the GLES tier's headers and
#     link libraries (the desktop job's libgl1-mesa-dev counterpart).
#     cc65 comes from Debian (2.19) — build_cc65_bundle.sh stages it from PATH,
#     so the DevBench ships asm + C exactly like the other three packages.
#     file + desktop-file-utils are appimagetool/linuxdeploy prerequisites.
apt-get update -qq
apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git wget ca-certificates \
    python3 file desktop-file-utils fuse libfuse2 zsync \
    libglfw3-dev libgles2-mesa-dev libegl1-mesa-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
    cc65

# The bind-mounted repo is owned by the host uid, not root — let git touch it.
# MUST come after the apt-get above: `debian:bookworm` is a bare base image with
# no git at all, so running this first killed the job at second 14 with
# "git: command not found" (exit 127) before a single package was installed.
git config --global --add safe.directory '*'

# --- Stage the cc65 bundle from the Debian install ---------------------------
tools/build_cc65_bundle.sh --out dist/cc65-bundle

# --- Dear ImGui (pinned; matches every other release job) --------------------
rm -rf imgui
# Pin lu dans IMGUI_VERSION (IMGUI_TAG le surcharge) + contrôle docking/version.
tools/ensure_imgui.sh imgui

# --- Build POM1, GLES tier ---------------------------------------------------
#     -static-libstdc++/-static-libgcc for the same reason as the bionic job:
#     keep glibc the ONLY libc-family floor the AppImage imposes, so a Pi OS
#     image with an older libstdc++ than the builder's still runs it.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DPOM1_GLES=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build -j"$(nproc)"

# Fail loudly if the GLES link somehow fell back to desktop libGL — the whole
# point of this package is that it does NOT need it. (libGLdispatch/libGLX may
# still appear: they are libglvnd's dispatch layer, pulled in by libGLESv2
# itself on a glvnd system, and are host-provided at runtime like any driver.)
if ldd build/POM1 | grep -qE 'lib(GL|OpenGL)\.so'; then
    echo "ERROR: the GLES build links desktop libGL — POM1_GLES did not take effect" >&2
    ldd build/POM1 | grep -E 'GL|EGL' >&2
    exit 1
fi
ldd build/POM1 | grep -E 'GLES|EGL|GL' || true

# --- Package the AppImage ----------------------------------------------------
#     build_appimage.sh derives aarch64 from `uname -m` and fetches the matching
#     linuxdeploy / appimagetool. The container has no working FUSE, hence
#     APPIMAGE_EXTRACT_AND_RUN.
export POM1_APPIMAGE_SKIP_BUILD=1
export APPIMAGE_EXTRACT_AND_RUN=1
packaging/linux/build_appimage.sh
