#!/usr/bin/env bash
# set_version.sh — bump / sync the POM1 version from the single-source VERSION file.
#
# The repo-root VERSION file is the ONE source of truth. C++ (via the generated
# PomVersion.h), CMake's macOS bundle metadata, and the packaging scripts all read
# it directly — those need NO sync. This script keeps the handful of STATIC text
# files (which can't read VERSION at build time) in lockstep:
#   - README.md                     (title)
#   - build-wasm/shell.html         (meta description, <title>, header)
#   - packaging/windows/README.txt  (header)
#
# Usage:
#   tools/set_version.sh 1.9.4   # bump: write VERSION, then sync the static files
#   tools/set_version.sh         # just re-sync the static files to current VERSION
#
# The version literal in each static file is anchored to the "POM1 v" prefix, so
# the sed can't touch unrelated version-like numbers (glibc 2.27, GLFW 3.3.10, …).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

if [ "$#" -ge 1 ]; then
    printf '%s\n' "$1" > VERSION
    echo "[set_version] VERSION -> $1"
fi

VER="$(tr -d '[:space:]' < VERSION)"
if [ -z "${VER}" ]; then echo "ERROR: VERSION is empty" >&2; exit 1; fi

# Replace the version that follows a "POM1 v" prefix (the only place these files
# carry the app version) with ${VER}, in place.
sync_file() {
    local f="$1"
    [ -f "${f}" ] || { echo "WARN: ${f} missing, skipped" >&2; return 0; }
    sed -i -E "s/POM1 v[0-9]+\.[0-9]+\.[0-9]+/POM1 v${VER}/g" "${f}"
    echo "[set_version] synced ${f}"
}
sync_file README.md
sync_file build-wasm/shell.html
sync_file packaging/windows/README.txt

echo "[set_version] done — version is ${VER}. Rebuild to propagate C++/CMake/packaging (they read VERSION)."
