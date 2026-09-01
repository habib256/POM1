#!/bin/bash
# ===========================================================================
# ensure_imgui.sh — guarantee that <dir> is a usable Dear ImGui checkout.
#
# imgui/ is NOT vendored: it is in .gitignore, and every machine keeps its own
# clone. Two traps, both already paid for:
#
#   1. Testing only that the DIRECTORY exists leaves a stale master copy in
#      place. The failure then surfaces in the middle of the build, on
#      ImGuiWindowFlags_NoDocking — a missing dependency disguised as a POM1
#      bug.
#   2. Testing only for docking is not enough either: an old docking tag
#      (v1.90.x) does define ImGuiWindowFlags_NoDocking, passes that test, and
#      then breaks the build further along on ImGuiChildFlags_Borders (renamed
#      upstream in 1.91.1, used by src/bench/CodeBench.cpp). The Metal sampler
#      patch in CMakeLists.txt is version-split too.
#
# So BOTH are checked: the docking branch AND IMGUI_VERSION_NUM >= the pin.
#
# The pin comes from the IMGUI_VERSION file at the repo root — one line, two
# space-separated fields: "<git tag> <IMGUI_VERSION_NUM floor>". (ImGui encodes
# its version as MAJOR*10000 + MINOR*100 + PATCH*10, hence 19290 for 1.92.9;
# the number is stored rather than derived from the tag, because that formula
# stops holding if the patch level ever reaches 10.) tools/check_imgui_pin.sh,
# wired into ctest, fails if any remaining literal disagrees with it.
#
# Usage:  tools/ensure_imgui.sh [dir]           (dir defaults to ./imgui)
# Environment overrides: IMGUI_TAG, IMGUI_MIN_VERSION_NUM, IMGUI_URL — this is
# how the packaging containers impose their own pin.
#
# LANGUAGE: this script's output is the FIRST thing a new contributor sees, and
# the documentation is in English. Keep the messages here in English too.
# ===========================================================================
set -euo pipefail

IMGUI_DIR="${1:-imgui}"

pin_file="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/IMGUI_VERSION"
if [ ! -f "$pin_file" ]; then
    echo "ERROR: $pin_file not found." >&2
    exit 1
fi
read -r pin_tag pin_num _ < "$pin_file"
if [ -z "${pin_tag:-}" ] || [ -z "${pin_num:-}" ]; then
    echo "ERROR: $pin_file is malformed (expected \"<tag> <num>\")." >&2
    exit 1
fi

IMGUI_TAG="${IMGUI_TAG:-$pin_tag}"
IMGUI_MIN_VERSION_NUM="${IMGUI_MIN_VERSION_NUM:-$pin_num}"
IMGUI_URL="${IMGUI_URL:-https://github.com/ocornut/imgui.git}"

# Network retries. A transient disconnect must not read as "POM1 is broken":
# a fresh-clone setup was measured failing here on
#     fetch-pack: unexpected disconnect while reading sideband packet
# and succeeding on the very next attempt. This script is the acquisition path
# for TEN callers — setup_pom1.sh, CMakeLists.txt, both CI workflows, three
# packaging containers and the Raspberry Pi installer — so one blip failed a
# newcomer's first build and a release build alike, with a message that did not
# even suggest trying again.
IMGUI_FETCH_ATTEMPTS="${IMGUI_FETCH_ATTEMPTS:-3}"

hdr="$IMGUI_DIR/imgui.h"

imgui_version_num() {
    [ -f "$hdr" ] || return 1
    awk '/^#define[ \t]+IMGUI_VERSION_NUM[ \t]/ { print $3; exit }' "$hdr"
}

imgui_has_docking() {
    [ -f "$hdr" ] && grep -q 'ImGuiWindowFlags_NoDocking' "$hdr"
}

imgui_is_usable() {
    local num
    num="$(imgui_version_num 2>/dev/null || true)"
    [ -n "$num" ] || return 1
    case "$num" in ''|*[!0-9]*) return 1 ;; esac
    imgui_has_docking || return 1
    [ "$num" -ge "$IMGUI_MIN_VERSION_NUM" ]
}

fail() { echo "ERROR: $*" >&2; exit 1; }

# Run a git command, retrying on failure with a widening pause. `cleanup` is a
# command run between attempts — a half-finished clone leaves the destination
# directory behind, and git refuses to clone into it, so the retry would fail
# for a second, misleading reason.
retry_git() {
    local cleanup="$1"; shift
    local attempt=1 delay=3
    while :; do
        if git "$@"; then return 0; fi
        if [ "$attempt" -ge "$IMGUI_FETCH_ATTEMPTS" ]; then return 1; fi
        echo "  network failure (attempt $attempt/$IMGUI_FETCH_ATTEMPTS) —" \
             "retrying in ${delay}s..." >&2
        [ -n "$cleanup" ] && eval "$cleanup"
        sleep "$delay"
        attempt=$((attempt + 1))
        delay=$((delay * 2))
    done
}

if [ ! -d "$IMGUI_DIR" ]; then
    echo "Dear ImGui missing — cloning $IMGUI_TAG..."
    retry_git "rm -rf '$IMGUI_DIR'" \
        clone --depth 1 --branch "$IMGUI_TAG" "$IMGUI_URL" "$IMGUI_DIR" ||
        fail "cloning Dear ImGui failed after $IMGUI_FETCH_ATTEMPTS attempts.
       Check your network, then run this script again:
         tools/ensure_imgui.sh $IMGUI_DIR"
elif imgui_is_usable; then
    echo "Dear ImGui already present ($(imgui_version_num), docking)."
    exit 0
else
    have="$(imgui_version_num 2>/dev/null || echo 'unknown')"
    dock="no"; imgui_has_docking && dock="yes"
    echo "$IMGUI_DIR is unusable (version $have, docking $dock) —" \
         "upgrading to $IMGUI_TAG..."

    git -C "$IMGUI_DIR" rev-parse --git-dir >/dev/null 2>&1 ||
        fail "$IMGUI_DIR is not a git repository. Delete it and run again."

    # --quiet HEAD, not --porcelain: the latter counts UNTRACKED files, so a
    # .DS_Store dropped by the Finder would block the very upgrade this exists
    # to perform. HEAD (rather than the index alone) also covers changes that
    # have already been `git add`-ed.
    git -C "$IMGUI_DIR" diff --quiet HEAD ||
        fail "$IMGUI_DIR has local modifications — nothing was touched.
       Save them, then run again."

    # --depth 1 on a FULL clone converts it to a shallow one and loses its
    # history at the next gc. So it is only imposed on an already-shallow repo.
    fetch_args=(origin tag "$IMGUI_TAG")
    if [ "$(git -C "$IMGUI_DIR" rev-parse --is-shallow-repository)" = "true" ]; then
        fetch_args=(--depth 1 "${fetch_args[@]}")
    fi
    retry_git "" -C "$IMGUI_DIR" fetch "${fetch_args[@]}" ||
        fail "could not fetch tag $IMGUI_TAG after $IMGUI_FETCH_ATTEMPTS attempts."
    git -C "$IMGUI_DIR" checkout --quiet "$IMGUI_TAG" ||
        fail "could not check out $IMGUI_TAG."
fi

imgui_is_usable ||
    fail "after installation, $IMGUI_DIR is still not usable
       (expected docking + IMGUI_VERSION_NUM >= $IMGUI_MIN_VERSION_NUM)."

echo "Dear ImGui ready ($(imgui_version_num), $IMGUI_TAG)."
