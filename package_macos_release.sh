#!/usr/bin/env bash
# POM1 — macOS release packager.
#
# Builds (if needed), copies every data dir into POM1.app/Contents/MacOS/
# next to the binary so the .app is fully self-contained, then wraps the
# bundle in a DMG with a drag-to-/Applications shortcut.
#
# Output: dist/POM1-macOS-v<VERSION>.dmg  + dist/POM1.app (staging)
#
# Layout convention: data dirs live *inside* the bundle at Contents/MacOS/.
# main_imgui.cpp's pom1_macos_chdir_to_distribution_root() chdir's there at
# startup so every cwd-relative probe (roms/, fonts/, sdcard/, cfcard/,
# pic/, cassettes/) resolves without the user having to keep sibling dirs.

set -euo pipefail

cd "$(dirname "$0")"

VERSION="1.8.5"
STAGING="dist/POM1.app"
DMG_STAGE="dist/dmg-staging"
DMGPATH="dist/POM1-macOS-v${VERSION}.dmg"

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
for f in roms/WozMonitor.rom roms/basic.rom roms/ACI.rom roms/charmap.rom \
         fonts/fa-solid-900.ttf pic/icon.png; do
    [[ -f "$f" ]] || { echo "ERROR: $f missing."; exit 1; }
done

# ---------- 3. Stage POM1.app with data inside Contents/MacOS/ ---------------
echo "==> Staging $STAGING"
rm -rf "$STAGING"
mkdir -p "$(dirname "$STAGING")"
ditto "$APP" "$STAGING"   # icon, Info.plist, inner binary

DATA_ROOT="$STAGING/Contents/MacOS"

cp -R roms      "$DATA_ROOT/roms"
cp -R fonts     "$DATA_ROOT/fonts"
cp -R software  "$DATA_ROOT/software"
cp -R cassettes "$DATA_ROOT/cassettes"
cp -R pic       "$DATA_ROOT/pic"

# sdcard + cfcard are user-writable by the emulator. macOS blocks writes
# inside /Applications for non-admin users, so the .app only works for
# persistent saves when left somewhere writable (Desktop, ~/Applications,
# Downloads). Read-only play always works. See README section below.
cp -R sdcard    "$DATA_ROOT/sdcard"
mkdir -p        "$DATA_ROOT/cfcard"
[[ -f cfcard/cfcard.po ]] && cp cfcard/cfcard.po "$DATA_ROOT/cfcard/"

# ---------- 4. DMG staging: POM1.app + /Applications shortcut + README -------
echo "==> Preparing DMG staging in $DMG_STAGE"
rm -rf "$DMG_STAGE"
mkdir -p "$DMG_STAGE"
# Move (not copy) the staged .app into the DMG staging folder — the user
# only cares about the final .dmg, and a stray dist/POM1.app alongside it
# would be confusing. ditto preserves bundle attributes cleanly.
ditto "$STAGING" "$DMG_STAGE/POM1.app"
# Drag-to-/Applications shortcut, the canonical macOS installer gesture.
ln -s /Applications "$DMG_STAGE/Applications"
# README the user sees right on the DMG window.
cat > "$DMG_STAGE/README.txt" <<EOF
POM1 — Apple 1 Emulator (macOS), version ${VERSION}
=====================================================

Install: drag POM1.app onto the Applications shortcut in this window.

The app is fully self-contained — every ROM, program, cassette, and SD
card image lives inside the bundle.

The app is unsigned. On first run, macOS Gatekeeper blocks it:

   1. Right-click POM1.app → Open → Open (confirm the warning).
      -- or --
   2. System Settings → Privacy & Security → scroll to the blocked-app
      notice → "Open Anyway" → confirm.

After one successful launch, Gatekeeper remembers the decision.

Persistent saves (microSD SAVE / LOAD, CFFA1 writes)
----------------------------------------------------
These write inside POM1.app/Contents/MacOS/{sdcard,cfcard}/. macOS blocks
writes under /Applications for non-admin users. For persistent saves,
keep POM1.app somewhere writable:
   ~/Applications/      (user-local, no admin needed)
   ~/Desktop/
   ~/Downloads/

Read-only play (games, demos, tapes) works from /Applications regardless.

Credits + full docs: https://github.com/habib256/POM1
Play in your browser: https://habib256.github.io/POM1/build-wasm/POM1.html

License: GPL-3.0.
EOF

# ---------- 5. DMG ------------------------------------------------------------
echo "==> Building $DMGPATH"
rm -f "$DMGPATH"
# UDZO = zlib-compressed read-only image, the standard macOS distribution
# format. Volume name matches the product for a clean Finder display.
hdiutil create -quiet \
    -volname "POM1 v${VERSION}" \
    -srcfolder "$DMG_STAGE" \
    -ov \
    -format UDZO \
    "$DMGPATH"

# Staging is consumed by hdiutil — clean up so `dist/` only carries the
# build artefacts (POM1.app raw staging + the final DMG).
rm -rf "$DMG_STAGE"

SIZE="$(du -h "$DMGPATH" | cut -f1)"
echo ""
echo "============================================"
echo "  Done: $DMGPATH ($SIZE)"
echo "  Staging bundle: $STAGING/"
echo "============================================"
