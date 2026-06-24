#!/usr/bin/env bash
# basicc_native.sh -- compile an Applesoft (integer-subset) listing to a STANDALONE
# 6502 binary that runs with NO interpreter, only the graphics card.
#
#   tools/basicc_native.sh <gen2|tms> INPUT.bas OUTPUT.bin [BASICC]
#
# Pipeline: basicc --native (BASIC -> ca65 asm) | ca65 program + runtime | ld65.
# The binary loads at $0300 and is entered with a JMP $0300 (no Applesoft ROM).
# Requires cc65 (ca65/ld65) on PATH and a built `basicc` (pass its path as $4, or
# it is taken from build/basicc).
set -euo pipefail

CARD="${1:?usage: basicc_native.sh <gen2|tms> in.bas out.bin [basicc]}"
SRC="${2:?missing INPUT.bas}"
OUT="${3:?missing OUTPUT.bin}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASICC="${4:-$ROOT/build/basicc}"
RT="$ROOT/dev/lib/basicrt"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$BASICC" --native --card "$CARD" "$SRC" -o "$TMP/prog.s"

case "$CARD" in
  gen2)
    ca65 -I "$ROOT/dev/lib/gen2" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/prog.o" "$TMP/prog.s"
    ca65 -I "$ROOT/dev/lib/gen2" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/rt.o"   "$RT/basicrt_gen2.s"
    ld65 -C "$RT/basicc_native.cfg" -o "$OUT" "$TMP/prog.o" "$TMP/rt.o"
    ;;
  tms)
    ca65 -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/prog.o" "$TMP/prog.s"
    ca65 -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/rt.o"   "$RT/basicrt_tms.s"
    ca65 -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -o "$TMP/m2.o"  "$ROOT/dev/lib/tms9918/tms9918m2.asm"
    ca65 -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -o "$TMP/pad.o" "$ROOT/dev/lib/tms9918/tms9918_pad.asm"
    ld65 -C "$RT/basicc_native.cfg" -o "$OUT" "$TMP/prog.o" "$TMP/rt.o" "$TMP/m2.o" "$TMP/pad.o"
    ;;
  *) echo "unknown card '$CARD' (use gen2 or tms)" >&2; exit 2 ;;
esac

echo "built $OUT ($(wc -c < "$OUT") bytes, load+run @ \$0300, $CARD)" >&2
