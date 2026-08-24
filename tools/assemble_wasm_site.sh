#!/usr/bin/env bash
# Assemble the deployable GitHub Pages site from a finished WASM build.
#
#   tools/assemble_wasm_site.sh <wasm-build-dir> <site-dir>
#
# Shared by .github/workflows/pages.yml (which deploys the result) and by
# ci.yml's `wasm` job (which browser-smokes the exact same assembly on every
# push). The sharing is the point: the page the smoke loads must be the page
# the deploy ships, not a cheaper approximation of it — the same rule the wasm
# job already applies to the build commands themselves.
#
# The published path must stay build-wasm/POM1.html — that is the URL in the
# README badge and in every link anyone has already shared.
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <wasm-build-dir> <site-dir>" >&2
  exit 2
fi

BUILD_DIR=$1
SITE_DIR=$2
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)

mkdir -p "$SITE_DIR/build-wasm"
for f in POM1.html POM1.js POM1.wasm POM1.data; do
  cp "$BUILD_DIR/$f" "$SITE_DIR/build-wasm/$f"
done

# Runtime siblings the page fetches directly rather than through MEMFS.
cp -r "$REPO_ROOT/build-wasm/cc65" "$SITE_DIR/build-wasm/cc65"
cp "$REPO_ROOT/build-wasm/cc65_bench.js" "$REPO_ROOT/build-wasm/cc65_wasm.js" \
   "$SITE_DIR/build-wasm/"
# The WHOLE pic/ tree is served beside the page: since the lazy-pic pass only
# icon.png + the cassette-deck logo ride inside POM1.data, and every other
# photo is fetched from here on first use (ensurePicFetched in
# MainWindow_Dialogs.cpp). Copy the repo's pic/, then overlay build-wasm/pic/
# (icon variants staged for the page itself).
mkdir -p "$SITE_DIR/build-wasm/pic"
cp "$REPO_ROOT"/pic/* "$SITE_DIR/build-wasm/pic/"
cp "$REPO_ROOT"/build-wasm/pic/* "$SITE_DIR/build-wasm/pic/"

# Bare habib256.github.io/pom1/ should land on the emulator, not a 404.
printf '<!doctype html><meta charset="utf-8">' > "$SITE_DIR/index.html"
printf '<meta http-equiv="refresh" content="0; url=build-wasm/POM1.html">' >> "$SITE_DIR/index.html"
printf '<title>POM1</title><a href="build-wasm/POM1.html">POM1</a>' >> "$SITE_DIR/index.html"

du -sh "$SITE_DIR"
ls -la "$SITE_DIR/build-wasm"
