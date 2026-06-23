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

VERSION="${POM1_VERSION:-1.9.2}"   # release workflow overrides from the git tag
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
# DevBench source tree: the "browse sketchs/" picker and the built-in examples
# (kP1Examples) open paths cwd-relative ("sketchs/gen2/…"), so the tree must ship
# AND be symlinked into the user-data cwd by the startup provisioner.
cp -R sketchs   "$DATA_ROOT/sketchs"
cp -R cassettes "$DATA_ROOT/cassettes"
cp -R pic       "$DATA_ROOT/pic"
cp -R ini_defaults "$DATA_ROOT/ini_defaults"  # curated per-preset layout baseline (found exe-relative)

# sdcard + cfcard ship as *seeds* — the first-launch provisioner copies
# them into ~/Library/Application Support/POM1/ and subsequent writes
# land there, leaving the bundle untouched (signed-friendly + /Applications
# install friendly + translocation-safe).
cp -R sdcard    "$DATA_ROOT/sdcard"
mkdir -p        "$DATA_ROOT/cfcard"
[[ -f cfcard/cfcard.po ]] && cp cfcard/cfcard.po "$DATA_ROOT/cfcard/"
# disks/ = IEC virtual-1541 .d64 seed (writable, like sdcard/cfcard) — parity
# with the Windows ZIP + Linux AppImage. The first-launch provisioner seeds it
# into the user-data dir (kWritableDirs in main_imgui.cpp); Drive1541 writes there.
[[ -d disks ]] && cp -R disks "$DATA_ROOT/disks"

# cc65 toolchain bundle (optional) → self-contained DevBench, no system cc65.
# POM1 finds it exe-relative at Contents/Resources/cc65/bin and points CC65_HOME
# at Contents/Resources/cc65/share/cc65 (ensureCc65Home, no launcher needed).
# Source: $POM1_CC65_BUNDLE, else dist/cc65-bundle/cc65, else auto-build (brew).
CC65_TREE=""
if [[ -n "${POM1_CC65_BUNDLE:-}" && -d "${POM1_CC65_BUNDLE}/bin" ]]; then
    CC65_TREE="${POM1_CC65_BUNDLE}"
elif [[ -d "dist/cc65-bundle/cc65/bin" ]]; then
    CC65_TREE="dist/cc65-bundle/cc65"
elif command -v ca65 >/dev/null 2>&1; then
    echo "==> cc65 detected — building bundle…"
    tools/build_cc65_bundle.sh --out dist/cc65-bundle >/dev/null && CC65_TREE="dist/cc65-bundle/cc65"
fi
if [[ -n "$CC65_TREE" ]]; then
    echo "==> cc65 bundle: $CC65_TREE"
    cp -R "$CC65_TREE" "$DATA_ROOT/cc65"
    # DevBench linker cfgs + libs (release bundles otherwise omit dev/).
    # Pom1BenchHost probes dev/ exe-relative at Contents/Resources/dev and needs
    # exactly dev/cc65 (the .cfg linker configs) + dev/lib (recursed for ca65 -I,
    # incl. tms9918c/gen2c/apple1c/gfx/telemetry). dev/projects is a
    # developer-only build tree — never loaded by a packaged app — so it stays
    # out. (The old apple1-videocard-lib line was dead: that C lib moved under
    # dev/lib/tms9918c, already covered by dev/lib below.)
    mkdir -p "$DATA_ROOT/dev"
    for d in cc65 lib; do
        [[ -d "dev/$d" ]] && cp -R "dev/$d" "$DATA_ROOT/dev/$d"
    done
else
    echo "==> (no cc65 bundle — DevBench limited to Woz-hex without system cc65)"
fi

# Verify the staged toolchain covers BOTH DevBench languages — asm (ca65+ld65)
# AND C (cl65+cc65) + runtime. POM1_REQUIRE_CC65=1 (set by the release workflow)
# turns a missing/partial bundle into a hard failure instead of a Woz-hex-only app.
if [[ -d "$DATA_ROOT/cc65" ]] && tools/verify_cc65_bundle.sh "$DATA_ROOT/cc65"; then
    :
elif [[ "${POM1_REQUIRE_CC65:-0}" == "1" ]]; then
    echo "ERROR: POM1_REQUIRE_CC65=1 but the cc65 bundle is missing/incomplete (asm+C required)." >&2
    echo "       Install cc65 (brew install cc65) or provide POM1_CC65_BUNDLE." >&2
    exit 1
fi

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

Build your own software (in-app DevBench)
-----------------------------------------
POM1 bundles the cc65 toolchain, so the in-app DevBench works out of the box -
nothing to install. Open DevBench > POM1 Bench, click New, pick a Language
(6502 assembly, C, BASIC, or Woz hex) x Machine (Apple-1 text, P-LAB TMS9918
256x192 + sprites, or Uncle Bernie GEN2 HGR 280x192 colour), then hit Run -
POM1 builds and boots it for you. POM1 even ships an Apple-1 Applesoft with the
Apple II graphics command set (HGR / HCOLOR= / HPLOT ... TO) that draws on BOTH
the GEN2 HGR and TMS9918 colour cards from the same listing - graphics-BASIC
demos in the bundled sketchs/basic_applesoft/ (Mandelbrot, Sierpinski, 3D Hat).

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
