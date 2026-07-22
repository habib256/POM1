#!/usr/bin/env bash
# check_version_sync.sh — fail if any static file's "POM1 v<version>" disagrees
# with the repo-root VERSION file. Run by ctest (`version_sync`).
#
# C++, CMake and the packaging scripts DERIVE the version from VERSION at build
# time, so they can't drift and aren't checked here. Only the static text files
# carry a literal that a human/tools/set_version.sh must keep in lockstep.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

VER="$(tr -d '[:space:]' < VERSION)"
if [ -z "${VER}" ]; then echo "FAIL: VERSION is empty"; exit 1; fi

FILES=(README.md build-wasm/shell.html packaging/windows/README.txt)
rc=0
for f in "${FILES[@]}"; do
    if [ ! -f "${f}" ]; then echo "FAIL: ${f} missing"; rc=1; continue; fi
    # Every "POM1 vX.Y.Z" occurrence in the file must match VERSION.
    bad="$(grep -oE 'POM1 v[0-9]+\.[0-9]+\.[0-9]+' "${f}" | grep -vF "POM1 v${VER}" || true)"
    n="$(grep -coE 'POM1 v[0-9]+\.[0-9]+\.[0-9]+' "${f}" || true)"
    if [ -n "${bad}" ]; then
        echo "FAIL: ${f} has stale version(s): ${bad//$'\n'/ } (VERSION=${VER})"
        echo "      run: tools/set_version.sh"
        rc=1
    elif [ "${n}" -eq 0 ]; then
        echo "WARN: ${f} carries no 'POM1 v<version>' string (nothing to check)"
    else
        echo "OK: ${f} (${n}x POM1 v${VER})"
    fi
done

if [ "${rc}" -eq 0 ]; then echo "version_sync OK — all static files at ${VER}"; fi
exit "${rc}"
