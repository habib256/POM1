#!/usr/bin/env bash
# build_cc65_bundle.sh — stage a *relocatable* cc65 toolchain tree that POM1's
# release packagers drop into the Windows ZIP / macOS .app / Linux AppImage so
# the DevBench has a working ca65/ld65/cl65 with no system cc65 on PATH.
#
# Output layout (a self-contained cc65 prefix):
#
#   <out>/cc65/bin/{ca65,ld65,cl65,cc65,ar65}[.exe]
#   <out>/cc65/share/cc65/{asminc,include,lib/none.lib}
#   <out>/cc65/LICENSE
#
# POM1 finds it exe-relative (see bench/ProcessUtil.cpp whichExe + Pom1BenchHost
# probe: <exe>/cc65/bin, <exe>/../Resources/cc65/bin, <exe>/../share/POM1/cc65/bin),
# and points CC65_HOME at <cc65>/share/cc65 so cl65 resolves its C runtime.
#
# Sources, in order of preference:
#   1. A local cc65 install on this machine (default) — for the AppImage on Linux
#      and the .dmg on macOS, build the bundle on that same OS.
#   2. --from <dir|zip>: repackage an official cc65 snapshot tree (the way to
#      stage the *Windows* bundle from a Linux/macOS box — feed it the unzipped
#      cc65-snapshot-win64).
#
# Overrides: CC65_BIN_DIR / CC65_SHARE_DIR force the source dirs.
#
# Usage:
#   tools/build_cc65_bundle.sh [--out DIR] [--from DIR_OR_ZIP] [--no-strip]
#
# cc65 is under the zlib license — redistributable; the LICENSE file is copied
# into the bundle.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

OUT="${REPO_ROOT}/dist/cc65-bundle"
FROM=""
STRIP=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)      OUT="$2"; shift 2;;
        --from)     FROM="$2"; shift 2;;
        --no-strip) STRIP=0; shift;;
        -h|--help)  grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
        *) echo "unknown arg: $1" >&2; exit 2;;
    esac
done

CC65_BINS=(ca65 ld65 cl65 cc65 ar65)
EXESUF=""

# Single cleanup trap for every mktemp dir we create below. (A per-call
# `trap … EXIT` would clobber any earlier one — bash keeps only the last EXIT
# trap — so the unzip scratch dir used to leak once the self-test added its own.)
CLEANUP_DIRS=()
cleanup() { for d in "${CLEANUP_DIRS[@]:-}"; do [[ -n "$d" ]] && rm -rf "$d"; done; }
trap cleanup EXIT

# ---- 1. Resolve source bin dir + share/cc65 data dir ------------------------
SRC_BIN="${CC65_BIN_DIR:-}"
SRC_SHARE="${CC65_SHARE_DIR:-}"

if [[ -n "$FROM" ]]; then
    SNAP="$FROM"
    if [[ "$FROM" == *.zip ]]; then
        TMP="$(mktemp -d)"; CLEANUP_DIRS+=("$TMP")
        echo "[cc65-bundle] unzip $FROM"
        unzip -q "$FROM" -d "$TMP"
        # official snapshots unzip to a single top dir (e.g. cc65/) — descend if so
        SNAP="$TMP"; [[ -d "$TMP/cc65" ]] && SNAP="$TMP/cc65"
    fi
    [[ -z "$SRC_BIN"   && -d "$SNAP/bin" ]]   && SRC_BIN="$SNAP/bin"
    [[ -z "$SRC_SHARE" ]] && for d in "$SNAP/share/cc65" "$SNAP/share" "$SNAP"; do
        [[ -d "$d/lib" && -d "$d/include" ]] && { SRC_SHARE="$d"; break; }
    done
    # Windows snapshots ship ca65.exe etc.
    [[ -f "$SRC_BIN/ca65.exe" ]] && EXESUF=".exe"
fi

if [[ -z "$SRC_BIN" ]]; then
    CA65="$(command -v ca65 || true)"
    [[ -n "$CA65" ]] || { echo "ERROR: ca65 not found (install cc65 or pass --from)." >&2; exit 1; }
    SRC_BIN="$(cd "$(dirname "$CA65")" && pwd)"
fi
if [[ -z "$SRC_SHARE" ]]; then
    # cl65 --print-target-path prints <share>/target
    if TP="$(cl65 --print-target-path 2>/dev/null)" && [[ -d "$TP" ]]; then
        SRC_SHARE="$(cd "$TP/.." && pwd)"
    else
        for d in /usr/share/cc65 /usr/local/share/cc65 "$(dirname "$SRC_BIN")/share/cc65"; do
            [[ -d "$d/lib" ]] && { SRC_SHARE="$d"; break; }
        done
    fi
fi
[[ -d "$SRC_SHARE/lib" && -d "$SRC_SHARE/include" ]] || {
    echo "ERROR: cc65 data dir (share/cc65 with lib/ + include/) not found." >&2
    echo "       tried: $SRC_SHARE  — set CC65_SHARE_DIR to override." >&2
    exit 1
}

echo "[cc65-bundle] bin   : $SRC_BIN"
echo "[cc65-bundle] share : $SRC_SHARE"
echo "[cc65-bundle] out   : $OUT"

# ---- 2. Build the relocatable tree ------------------------------------------
DST="$OUT/cc65"
rm -rf "$DST"
mkdir -p "$DST/bin" "$DST/share/cc65"

for b in "${CC65_BINS[@]}"; do
    src="$SRC_BIN/${b}${EXESUF}"
    if [[ -f "$src" ]]; then
        cp "$src" "$DST/bin/"
        [[ "$STRIP" == 1 && -z "$EXESUF" ]] && strip "$DST/bin/${b}${EXESUF}" 2>/dev/null || true
    else
        echo "[cc65-bundle] WARN: $src missing — skipped"
    fi
done

# Data: copy the full cc65 runtime tree, then prune to what POM1's DevBench uses.
for d in asminc include lib; do
    [[ -d "$SRC_SHARE/$d" ]] && cp -r "$SRC_SHARE/$d" "$DST/share/cc65/$d"
done
rm -rf "$DST/share/cc65/cfg" "$DST/share/cc65/target"
if [[ -f "$DST/share/cc65/lib/none.lib" ]]; then
    shopt -s nullglob
    for f in "$DST/share/cc65/lib"/*; do
        [[ "$(basename "$f")" == "none.lib" ]] || rm -rf "$f"
    done
fi

# ---- 3. LICENSE (zlib) ------------------------------------------------------
LIC=""
for c in "$SRC_SHARE/../LICENSE" /usr/share/doc/cc65/copyright /usr/share/doc/cc65/LICENSE; do
    [[ -f "$c" ]] && { LIC="$c"; break; }
done
if [[ -n "$LIC" ]]; then
    cp "$LIC" "$DST/LICENSE"
else
    cat > "$DST/LICENSE" <<'EOF'
cc65 is distributed under the zlib license. This is a redistribution of the
cc65 cross-development package (https://cc65.github.io/). See the cc65 project
for the full license text:
  https://github.com/cc65/cc65/blob/master/LICENSE
EOF
fi

# ---- 4. Self-test: the bundled cl65 must resolve its runtime via CC65_HOME ---
echo "[cc65-bundle] self-test (cl65 -t none -C none.cfg)…"
TESTDIR="$(mktemp -d)"; CLEANUP_DIRS+=("$TESTDIR")
cat > "$TESTDIR/t.c" <<'EOF'
int main(void){ return 0; }
EOF
NONECFG=""
for c in "$SRC_SHARE/cfg/none.cfg" "$SRC_SHARE/../cfg/none.cfg" /usr/share/cc65/cfg/none.cfg; do
    [[ -f "$c" ]] && { NONECFG="$c"; break; }
done
[[ -n "$NONECFG" ]] || { echo "ERROR: none.cfg not found for self-test" >&2; exit 1; }
if CC65_HOME="$DST/share/cc65" "$DST/bin/cl65" -t none -C "$NONECFG" -o "$TESTDIR/t.bin" "$TESTDIR/t.c" 2>"$TESTDIR/err"; then
    echo "[cc65-bundle] self-test OK ($(wc -c <"$TESTDIR/t.bin") bytes)"
else
    echo "[cc65-bundle] self-test FAILED:" >&2; cat "$TESTDIR/err" >&2; exit 1
fi

echo
SIZE="$(du -sh "$DST" | cut -f1)"
echo "[cc65-bundle] OK → $DST  ($SIZE)"
echo "[cc65-bundle] packagers copy this 'cc65' dir next to the POM1 binary."
