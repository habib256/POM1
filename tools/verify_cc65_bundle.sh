#!/usr/bin/env bash
# verify_cc65_bundle.sh <cc65-dir> — assert that a *staged* cc65 tree carries
# everything the POM1 DevBench (Pom1BenchHost) needs at runtime to compile BOTH
# of its native languages with no system cc65 on PATH:
#
#   asm  →  ca65 (assembler) + ld65 (linker)
#   C    →  cl65 (driver) + cc65 (compiler)
#   runtime data  →  share/cc65/{include,lib,target}
#
# Exit 0 = complete bundle. Non-zero + a diagnostic listing the missing pieces
# otherwise. Windows .exe suffixes are accepted transparently.
#
# Used by the release packagers (their POM1_REQUIRE_CC65 strict gate) and by the
# GitHub release workflow so a package can never silently ship without the
# toolchain the Bench advertises.
#
# Usage:
#   tools/verify_cc65_bundle.sh dist/cc65-bundle/cc65
#   tools/verify_cc65_bundle.sh path/to/AppDir/usr/share/POM1/cc65
#   tools/verify_cc65_bundle.sh dist/POM1.app/Contents/Resources/cc65

set -euo pipefail

DIR="${1:?usage: verify_cc65_bundle.sh <cc65-dir>}"
# Accept either the cc65 prefix itself or its parent (…/cc65).
[ -d "$DIR/bin" ] || DIR="$DIR/cc65"
BIN="$DIR/bin"

fail=0
note() { echo "  MISSING: $*" >&2; fail=1; }

# Binaries: ca65+ld65 cover asm, cl65+cc65 cover C. A native exe or a Windows
# .exe both count.
for t in ca65 ld65 cl65 cc65; do
    if [ -f "$BIN/$t" ] || [ -f "$BIN/$t.exe" ]; then
        :
    else
        note "bin/$t (asm needs ca65+ld65, C needs cl65+cc65)"
    fi
done

# Runtime data the linker/compiler resolve through CC65_HOME=<cc65>/share/cc65.
for d in include lib target; do
    [ -d "$DIR/share/cc65/$d" ] || note "share/cc65/$d"
done

if [ "$fail" != 0 ]; then
    echo "verify_cc65_bundle: INCOMPLETE cc65 bundle at '$DIR'" >&2
    exit 1
fi

echo "verify_cc65_bundle: OK — asm (ca65+ld65) + C (cl65+cc65) + runtime present at '$DIR'"
