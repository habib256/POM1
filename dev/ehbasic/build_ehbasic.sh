#!/usr/bin/env sh
# build_ehbasic.sh -- rebuild roms/ehbasic.rom from source.
#
# Unlike dev/msbasic/, there is no published image to reproduce: the Apple-1
# port is ours (src/apple1.s + src/apple1_mon.asm + src/apple1.cfg). What is
# fetched is only the interpreter itself, pinned by commit.
#
# Needs cc65 (ca65 + ld65) and git.
#
# Usage:  sh dev/ehbasic/build_ehbasic.sh [--install] [outdir]

set -eu

EHBASIC_REPO="https://github.com/jfredrickson/ehbasic-cc65.git"
EHBASIC_COMMIT="204318b585ac09faa8ded83fceeeb2e3bdf524f4"
EXPECTED_SHA256="6b8beca2a0930e82514d3a606fa6b4e79daa59ba2b6702af72fe63d8e6c025d3"

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

echo "== fetching jfredrickson/ehbasic-cc65 @ ${EHBASIC_COMMIT}"
src="$outdir/ehbasic-cc65"
if [ ! -d "$src/.git" ]; then
    git clone --quiet "$EHBASIC_REPO" "$src"
fi
git -C "$src" checkout --quiet "$EHBASIC_COMMIT"

work="$outdir/build"
rm -rf "$work"; mkdir -p "$work"
# Only basic.asm is taken from upstream — main.s/min_mon.asm/memory.cfg target
# an SBC with a 6551 ACIA and are replaced wholesale by the Apple-1 versions.
cp "$src/basic.asm" "$work/"
cp "$here/src/apple1.s" "$here/src/apple1_mon.asm" "$here/src/apple1.cfg" "$work/"

echo "== assembling"
# --cpu 6502: the Apple-1 is NMOS, no 65C02 opcodes may creep in.
# labels_without_colons: EhBASIC's original source style, kept by the cc65 port.
( cd "$work" \
  && ca65 --target none --cpu 6502 -U -g --feature labels_without_colons \
          apple1.s -o apple1.o \
  && ld65 -C apple1.cfg apple1.o -o ehbasic.rom -m ehbasic.map -Ln ehbasic.lbl )

built="$work/ehbasic.rom"
[ -f "$built" ] || { echo "build produced no binary" >&2; exit 1; }

actual=$(sha256sum "$built" | cut -d' ' -f1)
echo "== sha256 $actual"
if [ "$actual" != "$EXPECTED_SHA256" ]; then
    echo "NOTE: differs from the shipped roms/ehbasic.rom ($EXPECTED_SHA256)." >&2
    echo "  Expected if you edited src/ — run the ehbasic_smoke test before" >&2
    echo "  installing, and update EXPECTED_SHA256 here." >&2
    [ "$install" -eq 1 ] || exit 1
fi

if [ "$install" -eq 1 ]; then
    cp "$built" "$repo_root/roms/ehbasic.rom"
    echo "== installed to roms/ehbasic.rom"
    echo "   now run: ctest -R ehbasic_smoke"
else
    echo "   (pass --install to copy it over roms/ehbasic.rom)"
fi
