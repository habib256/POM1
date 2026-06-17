#!/usr/bin/env bash
# =============================================================================
# build_cc65_wasm.sh — compile the cc65 toolchain (ca65/ld65/cc65/ar65) to
# WebAssembly so the POM1 DevBench's asm/C path can run in the browser build
# (which has no subprocesses). The WASM counterpart of tools/build_cc65_bundle.sh.
#
# Each cc65 tool is an ordinary command-line C program (main(argc,argv), reads
# input files, writes output files). Emscripten compiles them to a MODULARIZEd
# WASM module with an in-memory filesystem (MEMFS); JS populates the FS with the
# source + include/cfg files, calls main() via callMain([...]), then reads the
# output file back out. (Same approach 8bitworkshop uses for cc65.)
#
# Output: <out>/{ca65,ld65,cc65,ar65}.{js,wasm}
#   - MODULARIZEd factory, EXPORT_NAME = the tool name.
#   - callMain + FS exported; INVOKE_RUN=0 (we drive main with argv).
#   - EXIT_RUNTIME=1: the tool exit()s when done; the driver instantiates a
#     fresh module per invocation (cheap; tools are one-shot).
#
# Usage:
#   tools/build_cc65_wasm.sh [--src DIR] [--out DIR] [--tools "ca65 ld65 ..."] [-O0|-O2|-O3]
#     --src  cc65 source checkout (default: clone github.com/cc65/cc65 into a work dir)
#     --out  output dir (default: build-wasm/cc65)
# Needs: emcc on PATH (Homebrew emscripten or an activated emsdk) + git (to clone).
# =============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC=""
OUT="$REPO/build-wasm/cc65"
TOOLS="ca65 ld65 cc65 ar65"
OPT="-O2"
# Pin the cc65 revision: the C path links cc65's C runtime, so the bundled libs
# must match the compiler version (cc65 2.19 renamed zp `sp`->`c_sp` vs 2.18).
# The asm path (ca65/ld65) is version-agnostic. Override with --rev / $CC65_REV.
REV="${CC65_REV:-master}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --src)  SRC="$2"; shift 2;;
    --out)  OUT="$2"; shift 2;;
    --tools) TOOLS="$2"; shift 2;;
    --rev)  REV="$2"; shift 2;;
    -O0|-O1|-O2|-O3) OPT="$1"; shift;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

command -v emcc >/dev/null || { echo "ERROR: emcc not found (source emsdk_env.sh or 'brew install emscripten')" >&2; exit 1; }

# ---- 1. cc65 source --------------------------------------------------------
if [[ -z "$SRC" ]]; then
  SRC="${TMPDIR:-/tmp}/cc65wasm/cc65"
  if [[ ! -d "$SRC/src" ]]; then
    echo "[cc65-wasm] cloning cc65 source ($REV) -> $SRC"
    mkdir -p "$(dirname "$SRC")"
    git clone https://github.com/cc65/cc65 "$SRC"
    git -C "$SRC" checkout --quiet "$REV"
  fi
fi
[[ -d "$SRC/src/common" ]] || { echo "ERROR: $SRC/src/common not found (bad --src?)" >&2; exit 1; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"   # absolute — emcc runs after `cd $SRC/src` below
echo "[cc65-wasm] source : $SRC"
echo "[cc65-wasm] out    : $OUT"

# ---- 2. compile each tool to WASM ------------------------------------------
# Runtime default search paths, baked in as C string literals (the tools fall
# back to these when no explicit -I/-C is given). MEMFS is mounted at /cc65.
DEFS=(
  '-DCA65_INC="/cc65/asminc"'
  '-DCC65_INC="/cc65/include"'
  '-DCL65_TGT="/cc65/target"'
  '-DLD65_LIB="/cc65/lib"'
  '-DLD65_OBJ="/cc65/lib"'
  '-DLD65_CFG="/cc65/cfg"'
  '-DBUILD_ID="wasm"'
)
EMFLAGS=(
  -sMODULARIZE=1
  -sEXPORTED_RUNTIME_METHODS=callMain,FS
  -sINVOKE_RUN=0
  -sEXIT_RUNTIME=1
  -sALLOW_MEMORY_GROWTH=1
  -sFORCE_FILESYSTEM=1
  -sENVIRONMENT=web,worker,node
)

cd "$SRC/src"
for tool in $TOOLS; do
  [[ -d "$tool" ]] || { echo "ERROR: no source dir src/$tool" >&2; exit 1; }
  echo "[cc65-wasm] building $tool ($(ls "$tool"/*.c | wc -l | tr -d ' ') + common .c) ..."
  emcc "$OPT" -I common "${DEFS[@]}" \
       "$tool"/*.c common/*.c -lm \
       "${EMFLAGS[@]}" -sEXPORT_NAME="$tool" \
       -o "$OUT/$tool.js"
  echo "[cc65-wasm]   -> $OUT/$tool.js ($(wc -c <"$OUT/$tool.wasm") B wasm)"
done

# ---- 3. C runtime: asminc + cc65 headers + the `none` target lib ------------
# The asm path needs nothing extra, but the C path does: ca65 opens .macpack
# files (longbranch.mac …) from asminc/, cc65 reads system headers from include/,
# and ld65 links the `-t none` C runtime (none.lib). none.lib must be built from
# THIS cc65 source so its zero-page ABI (c_sp) matches the WASM cc65 — a stock
# (older) none.lib won't link. We build it with the native tools (one `make`).
echo "[cc65-wasm] staging C runtime (asminc + include) ..."
cp -R "$SRC/asminc"  "$OUT/asminc"
cp -R "$SRC/include" "$OUT/include"

if [[ ! -f "$OUT/lib/none.lib" ]]; then
  echo "[cc65-wasm] building none.lib (native cc65 from the same source) ..."
  make -C "$SRC/src" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
  mkdir -p "$SRC/lib"                      # libsrc expects ../lib to exist
  make -C "$SRC/libsrc" none >/dev/null
  mkdir -p "$OUT/lib"
  cp "$SRC/lib/none.lib" "$OUT/lib/none.lib"
fi
echo "[cc65-wasm]   -> $OUT/{asminc,include,lib/none.lib} ($(wc -c <"$OUT/lib/none.lib") B lib)"

echo "[cc65-wasm] done: $TOOLS + C runtime"
