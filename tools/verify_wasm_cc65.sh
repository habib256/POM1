#!/usr/bin/env bash
# verify_wasm_cc65.sh — assert the committed cc65-in-WASM toolchain is present so
# the in-browser DevBench (the github-pages WASM build) keeps compiling BOTH asm
# and C. Unlike the desktop packages, the WASM tools are NOT rebuilt by any
# release job — they are git-tracked under build-wasm/cc65/ and served straight
# from the repo. This guard fails CI if any go missing, so they can never
# silently regress. It is the WASM counterpart of tools/verify_cc65_bundle.sh.
#
# Checks (relative to repo root, or $1 = the build-wasm/cc65 dir):
#   ca65.wasm (+ ca65.js loader)  — 6502 assembler  (asm path)
#   ld65.wasm (+ ld65.js loader)  — linker           (asm + C)
#   cc65.wasm (+ cc65.js loader)  — C compiler       (C path)
#   asminc/ include/ lib/         — the cc65 C runtime the C path links
#
# (cl65 is intentionally absent — the JS driver tools/cc65_bench.js chains
#  cc65 -> ca65 -> ld65 itself, so no cl65 driver is needed in the browser.)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="${1:-${REPO_ROOT}/build-wasm/cc65}"

fail=0
note() { echo "  MISSING: $*" >&2; fail=1; }

# The three load-bearing tool modules the user named, each with its JS loader
# (a .wasm is unusable without its emscripten glue).
for t in ca65 ld65 cc65; do
    [ -f "$DIR/$t.wasm" ] || note "build-wasm/cc65/$t.wasm"
    [ -f "$DIR/$t.js" ]   || note "build-wasm/cc65/$t.js (loader)"
done

# The cc65 C runtime the in-browser C path resolves (mounted at /cc65 in MEMFS).
for d in asminc include lib; do
    [ -d "$DIR/$d" ] || note "build-wasm/cc65/$d (cc65 C runtime)"
done

if [ "$fail" != 0 ]; then
    echo "verify_wasm_cc65: WASM cc65 toolchain INCOMPLETE at '$DIR'." >&2
    echo "  Rebuild it with:  tools/build_cc65_wasm.sh --out build-wasm/cc65" >&2
    echo "  then commit the regenerated build-wasm/cc65/ tree." >&2
    exit 1
fi

echo "verify_wasm_cc65: OK — WASM cc65 present (ca65+ld65 asm, cc65 C, + runtime) at '$DIR'"
