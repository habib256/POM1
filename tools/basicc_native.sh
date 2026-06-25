#!/usr/bin/env bash
# basicc_native.sh -- compile an Applesoft (integer-subset) listing to a STANDALONE
# 6502 binary that runs with NO interpreter, only the graphics card.
#
#   tools/basicc_native.sh [--float] <gen2|tms> INPUT.bas OUTPUT.bin [BASICC]
#
# Pipeline: basicc --native (BASIC -> ca65 asm) | ca65 program + runtime | ld65.
# The binary loads at $0300 and is entered with a JMP $0300 (no Applesoft ROM).
# --float compiles the binary32 floating-point phase (links basicrt_float.s).
# Requires cc65 (ca65/ld65) on PATH and a built `basicc` (pass its path as $4, or
# it is taken from build/basicc).
set -euo pipefail

FLOAT=""
if [ "${1:-}" = "--float" ]; then FLOAT="--float"; shift; fi
CARD="${1:?usage: basicc_native.sh [--float] <gen2|tms> in.bas out.bin [basicc]}"
SRC="${2:?missing INPUT.bas}"
OUT="${3:?missing OUTPUT.bin}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASICC="${4:-$ROOT/build/basicc}"
RT="$ROOT/dev/lib/basicrt"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$BASICC" --native $FLOAT --card "$CARD" "$SRC" -o "$TMP/prog.s"

# Derive -D RT_xxx flags from the runtime routines the program actually imports,
# so the runtime assembles ONLY those (unused routines + pixel tables are dropped).
DEFS=$(grep -E '^\.import ' "$TMP/prog.s" | grep -oE 'rt_[a-z0-9]+' | sort -u \
       | tr 'a-z' 'A-Z' | sed 's/^/-D /' | tr '\n' ' ') || true

# Float programs also link the standalone binary32 runtime. Without --float the
# compiler runs in Auto precision and still emits float code (importing fp_*)
# whenever the program has a decimal literal, a '/' division, or SQR/SIN/COS --
# so key off what the generated asm actually imports, not just the --float flag,
# otherwise an Auto float program fails to link with unresolved fp_* externals.
# The transcendentals (fp_int/fp_sqrt/fp_sin/fp_cos) are themselves gated, so
# assemble them ONLY when imported -- a plain +-*/ float program drops them.
FP_OBJ=()
if [ -n "$FLOAT" ] || grep -qE '^\.import .*fp_' "$TMP/prog.s"; then
  FPDEFS=""
  grep -q 'fp_int\b'  "$TMP/prog.s" && FPDEFS="$FPDEFS -D FP_INT"
  grep -q 'fp_sqrt\b' "$TMP/prog.s" && FPDEFS="$FPDEFS -D FP_SQRT"
  grep -q 'fp_sin\b'  "$TMP/prog.s" && FPDEFS="$FPDEFS -D FP_SIN"
  grep -q 'fp_cos\b'  "$TMP/prog.s" && FPDEFS="$FPDEFS -D FP_COS"
  ca65 $FPDEFS -o "$TMP/fp.o" "$RT/basicrt_float.s"
  FP_OBJ=("$TMP/fp.o")
fi

case "$CARD" in
  gen2)
    ca65 -I "$ROOT/dev/lib/gen2" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/prog.o" "$TMP/prog.s"
    ca65 $DEFS -I "$ROOT/dev/lib/gen2" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/rt.o" "$RT/basicrt_gen2.s"
    # GEN2 lo-res uses the page-2 framebuffer ($0800); a program loaded at $0300 would
    # overwrite itself once GR/PLOT paints there, so lo-res programs link + load at
    # $0C00 (above both lo-res pages). HGR (framebuffer $2000) stays at $0300.
    CFG="basicc_native.cfg"; LOADADDR="0300"
    case "$DEFS" in
      *RT_GR*|*RT_COLOR*|*RT_LORESPLOT*|*RT_HLIN*|*RT_VLIN*|*RT_TEXT*|*RT_HOME*)
        CFG="basicc_native_gen2_lores.cfg"; LOADADDR="0C00" ;;
    esac
    ld65 -C "$RT/$CFG" -o "$OUT" "$TMP/prog.o" "$TMP/rt.o" ${FP_OBJ[@]+"${FP_OBJ[@]}"}
    ;;
  tms)
    ca65 -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/prog.o" "$TMP/prog.s"
    ca65 $DEFS -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -I "$RT" -o "$TMP/rt.o" "$RT/basicrt_tms.s"
    # Link the VDP graphics lib only when the program actually draws. Lo-res
    # (Multicolor) needs only tms9918_pad.o (for tms9918_pad12); hi-res also pulls
    # in tms9918m2.o (init_vdp_g2 / plot_set / line_xy).
    VDP_OBJ=()
    case "$DEFS" in
      *RT_HGR*|*RT_PLOT*|*RT_LINE*|*RT_HCOLOR*|*RT_GR*|*RT_COLOR*|*RT_LORESPLOT*|*RT_HLIN*|*RT_VLIN*|*RT_TEXT*|*RT_HOME*)
        ca65 -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -o "$TMP/pad.o" "$ROOT/dev/lib/tms9918/tms9918_pad.asm"
        VDP_OBJ=("$TMP/pad.o") ;;
    esac
    case "$DEFS" in
      *RT_HGR*|*RT_PLOT*|*RT_LINE*|*RT_HCOLOR*)
        ca65 -I "$ROOT/dev/lib/tms9918" -I "$ROOT/dev/lib/apple1" -o "$TMP/m2.o"  "$ROOT/dev/lib/tms9918/tms9918m2.asm"
        VDP_OBJ=("$TMP/m2.o" ${VDP_OBJ[@]+"${VDP_OBJ[@]}"}) ;;
    esac
    ld65 -C "$RT/basicc_native.cfg" -o "$OUT" "$TMP/prog.o" "$TMP/rt.o" ${VDP_OBJ[@]+"${VDP_OBJ[@]}"} ${FP_OBJ[@]+"${FP_OBJ[@]}"}
    LOADADDR="0300"
    ;;
  *) echo "unknown card '$CARD' (use gen2 or tms)" >&2; exit 2 ;;
esac

echo "built $OUT ($(wc -c < "$OUT") bytes, load+run @ \$${LOADADDR}, $CARD${FLOAT:+, float})" >&2
