#!/usr/bin/env bash
# POM1 — macOS release packager.
#
# Builds (if needed), copies every data dir into POM1.app/Contents/Resources/
# (Apple-canonical location), and wraps the signed-friendly bundle in a DMG
# with a drag-to-/Applications shortcut + custom volume icon.
#
# Output: dist/POM1-macOS-v<VERSION>.dmg  + dist/POM1.app (staging)
#
# Layout: read-only assets (roms/, fonts/, software/, pic/, cassettes/, plus
# sdcard/ + cfcard/ seeds) live at Contents/Resources/. At startup the app's
# pom1_macos_provision_user_data_dir() helper creates
# ~/Library/Application Support/POM1/ with symlinks into the bundle for the
# read-only dirs and seeded real dirs for sdcard / cfcard / ini, then chdirs
# there. Existing cwd-relative probes all resolve through that layout.

set -euo pipefail

cd "$(dirname "$0")"

VERSION="1.9.1"
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

# ---------- 3. Stage POM1.app with data at Contents/Resources/ ---------------
echo "==> Staging $STAGING"
rm -rf "$STAGING"
mkdir -p "$(dirname "$STAGING")"
ditto "$APP" "$STAGING"   # icon, Info.plist, inner binary

# Apple convention: bundled data = read-only under Contents/Resources/.
# User-writable state (sdcard saves, cfcard writes, per-preset layouts)
# gets provisioned under ~/Library/Application Support/POM1/ at startup
# — see pom1_macos_provision_user_data_dir() in main_imgui.cpp.
DATA_ROOT="$STAGING/Contents/Resources"

cp -R roms      "$DATA_ROOT/roms"
cp -R fonts     "$DATA_ROOT/fonts"
cp -R software  "$DATA_ROOT/software"
cp -R cassettes "$DATA_ROOT/cassettes"
cp -R pic       "$DATA_ROOT/pic"

# sdcard + cfcard ship as *seeds* — the first-launch provisioner copies
# them into ~/Library/Application Support/POM1/ and subsequent writes
# land there, leaving the bundle untouched (signed-friendly + /Applications
# install friendly + translocation-safe).
cp -R sdcard    "$DATA_ROOT/sdcard"
mkdir -p        "$DATA_ROOT/cfcard"
[[ -f cfcard/cfcard.po ]] && cp cfcard/cfcard.po "$DATA_ROOT/cfcard/"

# ---------- 4. DMG staging: POM1.app + /Applications shortcut + README -------
echo "==> Preparing DMG staging in $DMG_STAGE"
rm -rf "$DMG_STAGE"
mkdir -p "$DMG_STAGE"
# ditto preserves bundle attributes cleanly — matters for codesign later.
ditto "$STAGING" "$DMG_STAGE/POM1.app"
# Drag-to-/Applications shortcut, the canonical macOS installer gesture.
ln -s /Applications "$DMG_STAGE/Applications"

# Volume icon: same .icns as the app, shown on the mounted DMG in Finder
# and on the .dmg file itself. The hidden `.VolumeIcon.icns` + SetFile -c
# dance is the documented pattern (no supported alternative in Monterey+).
cp packaging/macos/POM1.icns "$DMG_STAGE/.VolumeIcon.icns"

# README the user sees right on the DMG window.
cat > "$DMG_STAGE/README.txt" <<EOF
POM1 — Apple 1 Emulator (macOS), version ${VERSION}
=====================================================

Install: drag POM1.app onto the Applications shortcut in this window.

The app is unsigned. On first run, macOS Gatekeeper blocks it:

   1. Right-click POM1.app → Open → Open (confirm the warning).
      -- or --
   2. System Settings → Privacy & Security → scroll to the blocked-app
      notice → "Open Anyway" → confirm.

After one successful launch, Gatekeeper remembers the decision.

Where your saves live
---------------------
On first launch POM1 creates:

   ~/Library/Application Support/POM1/

with the bundled ROMs / fonts / software / demos / cassettes as read-only
symlinks back into POM1.app, plus real sdcard/, cfcard/, and ini/ folders
where your work is kept:

   sdcard/     Applesoft SAVE / microSD writes
   cfcard/     CFFA1 disk writes (cfcard.po)
   ini/        Per-preset window layouts

These survive app updates. Install or uninstall POM1.app however you want
(dragging anywhere on disk is fine, including /Applications); your data
stays safe in ~/Library/Application Support/POM1/.

To fully uninstall:
   Trash POM1.app
   rm -rf ~/Library/Application\ Support/POM1

Credits + full docs: https://github.com/habib256/POM1
Play in your browser: https://habib256.github.io/POM1/build-wasm/POM1.html

License: GPL-3.0.
EOF

# ---------- 5. DMG build (two-pass: writable image → set icon attr → UDZO) ----
# We write a scratch UDRW (read-write) image first so we can flip the
# custom-volume-icon attribute bit on the mounted volume (SetFile -c icnC),
# then convert to a compressed read-only UDZO for distribution. Doing this
# directly on a -format UDZO fails because the image is read-only at that
# point; -format UDRW + attach + SetFile + detach + convert is the
# standard hdiutil dance for custom DMG icons.
echo "==> Building $DMGPATH (with custom volume icon)"
rm -f "$DMGPATH"
SCRATCH="dist/POM1-scratch.dmg"
rm -f "$SCRATCH"

hdiutil create -quiet \
    -volname "POM1 v${VERSION}" \
    -srcfolder "$DMG_STAGE" \
    -format UDRW \
    -ov \
    "$SCRATCH"

# Attach, set the custom-icon volume attribute, detach. SetFile lives in
# /usr/bin (Command Line Tools). hdiutil's output format is tab-separated
# "devnode \t partition-type \t mountpoint"; the mountpoint only appears
# on the actual Volumes line (the GUID-scheme + partition lines above are
# empty in the third column). Filter for `/Volumes/` and grab the trailing
# run — APFS vs HFS doesn't matter since we only care about the mount.
ATTACH_OUT="$(hdiutil attach -nobrowse -readwrite -noverify \
                              -noautoopen "$SCRATCH")"
MOUNT="$(echo "$ATTACH_OUT" | awk -F '\t' '$3 ~ /^\/Volumes\// {print $3}' | tail -1)"
if [[ -n "$MOUNT" && -e "$MOUNT/.VolumeIcon.icns" ]]; then
    SetFile -a C "$MOUNT" 2>/dev/null || true
fi
[[ -n "$MOUNT" ]] && hdiutil detach -quiet "$MOUNT" || true

# Convert to the final compressed, read-only UDZO distributable.
hdiutil convert -quiet -format UDZO -o "$DMGPATH" "$SCRATCH"
rm -f "$SCRATCH"

# Staging is consumed — clean up so `dist/` only carries the final DMG
# and the raw .app (useful for ad-hoc debugging of the unbundled build).
rm -rf "$DMG_STAGE"

SIZE="$(du -h "$DMGPATH" | cut -f1)"
echo ""
echo "============================================"
echo "  Done: $DMGPATH ($SIZE)"
echo "  Staging bundle: $STAGING/"
echo "============================================"
