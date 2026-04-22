#!/usr/bin/env bash
# POM1 — macOS release packager.
#
# Builds (if needed), assembles a distribution folder with POM1.app + sibling
# data directories, and zips it for distribution.
#
# Output: dist/POM1-macOS-v<VERSION>.zip   + dist/POM1-macOS/ (staging)
#
# Pairs with package_windows_release.bat; the layout mirrors the Windows ZIP
# (sibling data dirs next to the executable) so users can drop the release
# anywhere on disk and double-click.

set -euo pipefail

cd "$(dirname "$0")"

VERSION="1.8.5"
OUTDIR="dist/POM1-macOS"
ZIPPATH="dist/POM1-macOS-v${VERSION}.zip"

echo "============================================"
echo " POM1 — macOS distribution package (v${VERSION})"
echo "============================================"

# ---------- 1. Build (or reuse) the .app -------------------------------------
APP="build/POM1.app"
if [[ ! -d "$APP" ]]; then
    echo "==> POM1.app not found, building in Release mode..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build -j"$(sysctl -n hw.ncpu)" --target pom1_imgui
fi
[[ -d "$APP" ]] || { echo "ERROR: build/POM1.app still missing."; exit 1; }
[[ -x "$APP/Contents/MacOS/POM1" ]] || { echo "ERROR: inner binary missing."; exit 1; }

# ---------- 2. Preflight: mandatory assets -----------------------------------
for f in roms/WozMonitor.rom roms/basic.rom roms/ACI.rom \
         fonts/fa-solid-900.ttf pic/icon.png; do
    [[ -f "$f" ]] || { echo "ERROR: $f missing."; exit 1; }
done

# ---------- 3. Stage the distribution folder ---------------------------------
echo "==> Staging $OUTDIR"
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"

# The .app itself (self-contained: icon, Info.plist, inner binary).
ditto "$APP" "$OUTDIR/POM1.app"

# Data dirs — sibling to POM1.app, resolved by the exe-relative chdir at
# boot. Same layout as the Windows release.
cp -R roms      "$OUTDIR/roms"
cp -R fonts     "$OUTDIR/fonts"
cp -R software  "$OUTDIR/software"
cp -R cassettes "$OUTDIR/cassettes"
cp -R pic       "$OUTDIR/pic"

# sdcard + cfcard are user-writable by the emulator — duplicate, not link.
cp -R sdcard    "$OUTDIR/sdcard"
mkdir -p        "$OUTDIR/cfcard"
[[ -f cfcard/cfcard.po ]] && cp cfcard/cfcard.po "$OUTDIR/cfcard/"

# ---------- 4. README for the end user ---------------------------------------
cat > "$OUTDIR/README.txt" <<EOF
POM1 — Apple 1 Emulator (macOS), version ${VERSION}
=====================================================

Launch: double-click POM1.app.

The app is unsigned. macOS Gatekeeper blocks unsigned apps on first run:

   1. Right-click POM1.app → Open → Open (confirm the warning).
      -- or --
   2. System Settings → Privacy & Security → scroll to the blocked-app
      notice → "Open Anyway" → confirm.

After one successful launch Gatekeeper remembers the decision.

Contents of this folder:

   POM1.app            The emulator (.app bundle with icon + Info.plist).
   roms/               Apple 1 ROMs: WOZ Monitor, Integer BASIC, ACI,
                       Applesoft Lite (x2), Krusader, SD CARD OS, CFFA1,
                       Juke-Box, charmap.
   fonts/              Font Awesome 6 (toolbar glyphs).
   software/           60+ ready-to-run programs (games, BASIC, dev tools,
                       A1-SID tunes, HGR / TMS9918 / GT-6144 / a1io_rtc demos).
   cassettes/          Original-tape captures (default WOZ_talk.mp3).
   sdcard/             Virtual microSD content — writable by the emulator.
   cfcard/             CFFA1 ProDOS disk image.
   pic/                App icon + About photo.

The .app looks for these dirs as siblings. Keep the folder together; don't
extract just POM1.app on its own.

Credits + full docs: https://github.com/habib256/POM1
Play in your browser: https://habib256.github.io/POM1/build-wasm/POM1.html

License: GPL-3.0.
EOF

# ---------- 5. ZIP it --------------------------------------------------------
echo "==> Zipping $ZIPPATH"
rm -f "$ZIPPATH"
( cd dist && zip -qr "POM1-macOS-v${VERSION}.zip" "POM1-macOS" )

SIZE="$(du -h "$ZIPPATH" | cut -f1)"
echo ""
echo "============================================"
echo "  Done: $ZIPPATH ($SIZE)"
echo "  Staging: $OUTDIR/"
echo "============================================"
