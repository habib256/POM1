#!/usr/bin/env bash
# check_imgui_pin.sh — fail if anything in the repo disagrees with the Dear
# ImGui pin in IMGUI_VERSION. Run by ctest (`imgui_pin_sync`).
#
# WHY THIS EXISTS: imgui/ is not vendored, and the pin used to be copy-pasted
# into nine files (CMake, both setup scripts, three packaging containers, the
# Pi installer, and three workflows). Nothing tied them together, so bumping
# the pin by editing the places you remembered left CI and the release
# containers building against the old tag — silently, because each of them
# works perfectly well on its own stale value.
#
# Most consumers now READ IMGUI_VERSION and cannot drift. The ones that can't
# are GitHub workflow `env:` blocks, which are parsed before any step runs, and
# prose in the docs. Rather than list those, this scans EVERY tracked file: a
# tenth site added tomorrow is caught the day it appears, which a fixed list
# would not do.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

if [ ! -f IMGUI_VERSION ]; then
    echo "FAIL: IMGUI_VERSION is missing"
    exit 1
fi
read -r TAG NUM _ < IMGUI_VERSION
if [ -z "${TAG:-}" ] || [ -z "${NUM:-}" ]; then
    echo "FAIL: IMGUI_VERSION is malformed (expected \"<tag> <version-num>\")"
    exit 1
fi
case "${NUM}" in ''|*[!0-9]*) echo "FAIL: version-num '${NUM}' is not numeric"; exit 1 ;; esac
echo "pin: ${TAG} (IMGUI_VERSION_NUM floor ${NUM})"

rc=0

# Prefer Git's tracked-file view, but source archives intentionally have no
# .git directory. In that case scan source material while pruning local
# dependencies, build products and generated distribution trees.
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    pin_matches() {
        git grep -nI -oE 'v[0-9]+\.[0-9]+\.[0-9]+-docking' \
            -- . ':!IMGUI_VERSION' 2>/dev/null || true
    }
else
    echo "INFO: no Git metadata; scanning source archive"
    pin_matches() {
        find . \
            \( -path './imgui' -o -path './build' -o -path './build-*' \
               -o -path './dist' -o -path './node_modules' -o -path './.git' \) -prune \
            -o -type f -print0 |
        xargs -0 grep -nIH -oE 'v[0-9]+\.[0-9]+\.[0-9]+-docking' 2>/dev/null || true
    }
fi

# -- 1. Every "vX.Y.Z-docking" literal in a tracked file must BE the pin. -----
# In a checkout, git grep naturally excludes the local imgui dependency and
# build trees. The archive fallback above prunes their conventional paths.
matches="$(pin_matches)"
stale="$(printf '%s\n' "${matches}" | grep -v ":${TAG}\$" || true)"
if [ -n "${stale}" ]; then
    echo "FAIL: these carry a Dear ImGui tag that is not ${TAG}:"
    echo "${stale}" | sed 's/^/      /'
    echo "      edit IMGUI_VERSION, then update the literals above to match."
    rc=1
else
    n="$(printf '%s\n' "${matches}" | sed '/^$/d' | wc -l | tr -d ' ')"
    echo "OK: ${n} tag literal(s), all ${TAG}"
fi

# -- 2. The numeric floor quoted in the docs must match too. ------------------
# CLAUDE.md states the ">= N" rule in prose; a bump that missed it would leave
# the one document people actually read contradicting the build.
doc_nums="$(grep -oE 'IMGUI_VERSION_NUM[^0-9]{0,12}[0-9]{5}' CLAUDE.md 2>/dev/null |
            grep -oE '[0-9]{5}$' | sort -u || true)"
if [ -n "${doc_nums}" ]; then
    bad="$(echo "${doc_nums}" | grep -v "^${NUM}\$" || true)"
    if [ -n "${bad}" ]; then
        echo "FAIL: CLAUDE.md quotes IMGUI_VERSION_NUM $(echo "${bad}" | tr '\n' ' ')— pin says ${NUM}"
        rc=1
    else
        echo "OK: CLAUDE.md quotes ${NUM}"
    fi
fi

# -- 3. The pinned tag and the numeric floor must describe the same release. --
# Catches the half-bump: the tag moved to the next release, the floor did not.
# ImGui encodes MAJOR*10000 + MINOR*100 + PATCH*10, so derive and compare. Only
# checked when the patch is a single digit, the range where the formula holds.
#
# NB: no example tag is spelled out anywhere in this file on purpose — check 1
# scans every TRACKED file, so an illustrative "vX.Y.Z-docking" in a comment
# here would make the guard fail on itself. It did, the first time it ran after
# being committed: until then the file was untracked and git grep skipped it.
if [[ "${TAG}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9])-docking$ ]]; then
    want=$(( ${BASH_REMATCH[1]} * 10000 + ${BASH_REMATCH[2]} * 100 + ${BASH_REMATCH[3]} * 10 ))
    if [ "${want}" -ne "${NUM}" ]; then
        echo "FAIL: tag ${TAG} implies IMGUI_VERSION_NUM ${want}, but the pin says ${NUM}"
        rc=1
    else
        echo "OK: tag and version-num agree (${want})"
    fi
fi

[ "${rc}" -eq 0 ] && echo "imgui_pin_sync: OK"
exit "${rc}"
