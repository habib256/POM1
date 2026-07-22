#!/usr/bin/env bash
# build_in_bionic.sh — the Linux release build, executed INSIDE an Ubuntu 18.04
# (bionic, glibc 2.27) container so the resulting AppImage links against glibc
# 2.27 and therefore runs on Linux Mint 19.x and every newer distro.
#
# WHY A CONTAINER AT ALL: an AppImage bundles GLFW/X11/GL/cc65/data but NEVER
# glibc — so its effective floor is the glibc of whatever machine built it.
# Building on ubuntu-latest (glibc 2.39) stamped a GLIBC_2.34 requirement into
# the binary and broke every pre-2.34 system (Uncle Bernie / Mint 19.x reported
# it, juillet 2026). Building on bionic pins the floor at 2.27.
#
# WHY `docker run` AND NOT a `container:` KEY IN THE WORKFLOW: GitHub's
# node20-based actions (checkout@v4, upload-artifact@v4) require glibc >= 2.28 to
# run — exactly one notch above bionic's 2.27 — so the runner cannot execute its
# own actions inside a bionic `container:`. We therefore keep the job on the
# host (actions run there) and drive this build through `docker run`, with the
# repo bind-mounted at /work so the finished AppImage lands back on the host for
# upload-artifact. See the `linux` job in .github/workflows/release.yml.
#
# Assumes: run as root inside `ubuntu:18.04`, repo bind-mounted at /work (cwd).
# Honoured env: POM1_VERSION (may be empty on dry runs), POM1_REQUIRE_CC65,
# IMGUI_TAG.
#
# bionic is EOL, so several toolchain pieces are too old and are replaced here:
#   * apt archives  → repointed at old-releases.ubuntu.com (archive.ubuntu 404s)
#   * g++-7         → g++-9 (bionic's g++-7 has no usable <filesystem>)
#   * CMake 3.10    → a modern CMake tarball (repo needs >= 3.16)
#   * GLFW 3.2.1    → GLFW 3.3 from source (repo needs 3.3; uses 3.3-only API)
#   * cc65 2.17     → cc65 from source (bionic's package predates the DevBench
#                     runtime cfgs/libs)
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
CMAKE_VER=3.27.9
GLFW_VER=3.3.10

# --- bionic is EOL: its apt archive location has MOVED TWICE ------------------
# Early juillet 2026 archive.ubuntu.com 404'd bionic and old-releases served it;
# by 22 juillet the situation had inverted (old-releases 404s, archive serves it
# again — run 29819799811 failed on exactly this). Try the stock sources first
# and only rewrite to old-releases when archive no longer carries bionic. (No
# curl/wget probe possible — the bare ubuntu:18.04 image ships neither.)
if ! apt-get update; then
    sed -i -e 's|http://archive.ubuntu.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' \
           -e 's|http://security.ubuntu.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' \
           /etc/apt/sources.list
    apt-get update
fi

# --- base tooling + X11/GL dev headers (the latter to build GLFW from source) -
apt-get install -y --no-install-recommends \
    ca-certificates gnupg wget curl git make pkg-config \
    file desktop-file-utils python3 xz-utils \
    build-essential software-properties-common \
    libgl1-mesa-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev

# --- g++-9 (bionic default g++-7 lacks a usable <filesystem>) ----------------
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y --no-install-recommends gcc-9 g++-9
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 90 \
                    --slave   /usr/bin/g++ g++ /usr/bin/g++-9
gcc --version

# --- modern CMake (bionic ships 3.10; repo needs >= 3.16) --------------------
wget -qO /tmp/cmake.tar.gz \
    "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VER}/cmake-${CMAKE_VER}-linux-x86_64.tar.gz"
mkdir -p /opt/cmake
tar --strip-components=1 -xzf /tmp/cmake.tar.gz -C /opt/cmake
export PATH="/opt/cmake/bin:${PATH}"
cmake --version

# --- GLFW 3.3 from source (bionic libglfw3-dev is 3.2.1; repo needs 3.3) -----
#     Installed to /usr/local so `find_package(glfw3 3.3 CONFIG)` resolves it;
#     linuxdeploy then bundles libglfw.so.3 into the AppImage.
wget -qO /tmp/glfw.tar.gz \
    "https://github.com/glfw/glfw/archive/refs/tags/${GLFW_VER}.tar.gz"
tar -xzf /tmp/glfw.tar.gz -C /tmp
cmake -S "/tmp/glfw-${GLFW_VER}" -B /tmp/glfw-build \
    -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
    -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build /tmp/glfw-build -j"$(nproc)" --target install
ldconfig

# --- cc65 from source (bionic's packaged 2.17 predates the DevBench runtime) --
git config --global --add safe.directory '*'
git clone --depth 1 https://github.com/cc65/cc65.git /tmp/cc65
make -C /tmp/cc65 -j"$(nproc)"
# The in-tree build leaves asminc/include/lib at the repo ROOT — share/cc65
# only exists after `make install` (run 29918523855 failed on exactly this).
# Install into a scratch prefix to get the canonical bin/ + share/cc65 layout.
make -C /tmp/cc65 install PREFIX=/tmp/cc65-install
# Stage the freshly built (glibc-2.27-linked) toolchain into the release bundle
# layout that build_appimage.sh copies into usr/share/POM1/cc65.
CC65_BIN_DIR=/tmp/cc65-install/bin CC65_SHARE_DIR=/tmp/cc65-install/share/cc65 \
    tools/build_cc65_bundle.sh --out dist/cc65-bundle

# --- Dear ImGui (pinned; matches the macOS/Windows release jobs) -------------
git clone --depth 1 --branch "${IMGUI_TAG:-v1.92.7}" https://github.com/ocornut/imgui.git

# --- Build POM1 --------------------------------------------------------------
#     -static-libstdc++/-static-libgcc so the ONLY libc-family floor the AppImage
#     imposes is glibc (2.27). Without this, g++-9's newer GLIBCXX/libgcc symbols
#     would raise the floor above what Mint 19 ships — and linuxdeploy's default
#     excludelist does not bundle libstdc++, so the target would have to provide
#     a matching one. Static-linking both sidesteps the whole problem.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build -j"$(nproc)"

# --- Package the AppImage ----------------------------------------------------
#     Binary already built (SKIP_BUILD) and cc65 bundle already staged.
#     APPIMAGE_EXTRACT_AND_RUN: the container has no FUSE, so linuxdeploy /
#     appimagetool run in extract-and-run mode.
export POM1_APPIMAGE_SKIP_BUILD=1
export APPIMAGE_EXTRACT_AND_RUN=1
packaging/linux/build_appimage.sh
