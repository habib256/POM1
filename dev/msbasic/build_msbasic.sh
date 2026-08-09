#!/usr/bin/env sh
# build_msbasic.sh -- rebuild roms/msbasic.rom from public sources, reproducibly.
#
# Produces a byte-identical copy of the shipped ROM (sha256 asserted at the end),
# so the artefact in roms/ is never something you have to take on trust: it is
# whatever these two pinned commits plus patches/cz6502-target.patch assemble to.
#
# Needs cc65 (ca65 + ld65) and git. POM1 already depends on cc65 for the
# DevBench, so a dev machine has it; release packaging does NOT run this (the
# ROM is committed, like every other ROM in roms/).
#
# Usage:   sh dev/msbasic/build_msbasic.sh [outdir]
# Default outdir is a temp dir; the result is copied over roms/msbasic.rom only
# when --install is passed.

set -eu

MSBASIC_REPO="https://github.com/mist64/msbasic.git"
MSBASIC_COMMIT="2a0bc2fe0db13f8cf1b5c40b1d5617263cdb9cb4"
EXPECTED_SHA256="bbe7bfe7b1c518c0e54e741e5df1c0170572751bbf41374a9d5898f03ac642aa"

install=0
outdir=""
for arg in "$@"; do
    case "$arg" in
        --install) install=1 ;;
        *) outdir="$arg" ;;
    esac
done

here=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$here/../.." && pwd)
[ -n "$outdir" ] || outdir=$(mktemp -d)
mkdir -p "$outdir"

command -v ca65 >/dev/null 2>&1 || { echo "ca65 not found (install cc65)" >&2; exit 1; }
command -v ld65 >/dev/null 2>&1 || { echo "ld65 not found (install cc65)" >&2; exit 1; }

echo "== fetching mist64/msbasic @ ${MSBASIC_COMMIT}"
src="$outdir/msbasic"
if [ ! -d "$src/.git" ]; then
    git clone --quiet "$MSBASIC_REPO" "$src"
fi
git -C "$src" checkout --quiet "$MSBASIC_COMMIT"
git -C "$src" checkout --quiet -- .

echo "== applying the cz6502 (Apple-1) target"
# The overlay files come from coopzone-dc/Apple-1-Replica (see README.md); the
# patch adds the four target-dispatch branches its author never published, which
# is why a straight overlay copy does not assemble.
cp "$here/overlay/"*.s "$here/overlay/"*.cfg "$here/overlay/make.sh" "$src/"
git -C "$src" apply "$here/patches/cz6502-target.patch"

echo "== assembling"
( cd "$src" && sh make.sh )

built="$src/tmp/cz6502.bin"
[ -f "$built" ] || { echo "build produced no binary" >&2; exit 1; }

actual=$(sha256sum "$built" | cut -d' ' -f1)
echo "== sha256 $actual"
if [ "$actual" != "$EXPECTED_SHA256" ]; then
    echo "MISMATCH: expected $EXPECTED_SHA256" >&2
    echo "  The build no longer reproduces the shipped ROM. Do NOT install it" >&2
    echo "  without working out why — the pinned commit is what makes this" >&2
    echo "  artefact auditable." >&2
    exit 1
fi
echo "== reproduces roms/msbasic.rom exactly"

if [ "$install" -eq 1 ]; then
    cp "$built" "$repo_root/roms/msbasic.rom"
    echo "== installed to roms/msbasic.rom"
else
    echo "   (pass --install to copy it over roms/msbasic.rom)"
fi
