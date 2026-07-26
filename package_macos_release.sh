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

# Single source of truth: the repo-root VERSION file (release workflow overrides
# via POM1_VERSION from the git tag). Never hardcode the version here.
VERSION="${POM1_VERSION:-$(cat VERSION)}"
STAGING="dist/POM1.app"
DMG_STAGE="dist/dmg-staging"
DMGPATH="dist/POM1-macOS-v${VERSION}.dmg"

echo "============================================"
echo " POM1 — macOS distribution package (v${VERSION})"
echo "============================================"

# ---------- 1. Build (or reuse) the .app -------------------------------------
# POM1_BUILD_DIR lets a universal release build live beside a developer's
# native build/ instead of clobbering it (the release workflow leaves it unset
# and gets the usual build/).
BUILD_DIR="${POM1_BUILD_DIR:-build}"
APP="$BUILD_DIR/POM1.app"
if [[ ! -d "$APP" ]]; then
    echo "==> POM1.app not found, building in Release mode..."
    # POM1_MACOS_ARCHS (set by the release workflow to "arm64;x86_64") makes this
    # a Universal 2 build. Unset for a local build → native arch only, which is
    # what a developer wants: half the compile time, and Homebrew's single-arch
    # GLFW is enough. See packaging/macos/build_universal_deps.sh.
    CMAKE_ARCH_ARG=()
    [[ -n "${POM1_MACOS_ARCHS:-}" ]] && \
        CMAKE_ARCH_ARG=(-DCMAKE_OSX_ARCHITECTURES="${POM1_MACOS_ARCHS}")
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARCH_ARG[@]}" >/dev/null
    cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)" --target pom1_imgui
fi
[[ -d "$APP" ]] || { echo "ERROR: $APP still missing."; exit 1; }
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
    # incl. tms9918c/gen2c/apple1c/gfx/telemetry) + dev/bench (per-target C
    # build specs, JSON). dev/codetank is a
    # developer-only build tree — never loaded by a packaged app — so it stays
    # out. (The old apple1-videocard-lib line was dead: that C lib moved under
    # dev/lib/tms9918c, already covered by dev/lib below.)
    mkdir -p "$DATA_ROOT/dev"
    for d in cc65 lib bench; do
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

# ---------- 3a. Bundle non-system dylibs into Contents/Frameworks -----------
# POM1 links GLFW from whatever Homebrew prefix built it, and the linker bakes
# that ABSOLUTE path into the binary:
#
#   /usr/local/opt/glfw/lib/libglfw.3.dylib      (Intel brew — the CI runner)
#   /opt/homebrew/opt/glfw/lib/libglfw.3.dylib   (Apple Silicon brew)
#
# A locally-built .app therefore runs on the machine that built it and NOWHERE
# else: the CI-produced DMG died on every Apple Silicon Mac with
# "dyld: Library not loaded: /usr/local/opt/glfw/lib/libglfw.3.dylib" (surfaced
# by Finder as the misleading "POM1 quit unexpectedly"). The CI smoke-launch
# missed it because the runner obviously HAS brew glfw at that exact path.
#
# Fix: copy every non-system dependency into Contents/Frameworks, rewrite the
# references to @rpath, and give the executable an @executable_path/../Frameworks
# rpath — the standard self-contained-bundle layout. Recursive, so a dependency
# that itself pulls a brew dylib is caught too. MUST run BEFORE the codesign in
# 3b: install_name_tool rewrites the Mach-O headers and invalidates signatures.
echo "==> Bundling non-system dylibs into Contents/Frameworks"
BIN="$STAGING/Contents/MacOS/POM1"
FRAMEWORKS="$STAGING/Contents/Frameworks"
mkdir -p "$FRAMEWORKS"

# Absolute paths outside /usr/lib and /System are ours to carry. Anything
# already expressed as @rpath/@loader_path/@executable_path is either a system
# framework reference or a dep we have already rewritten.
needs_bundling() {
    case "$1" in
        /usr/lib/*|/System/*|@*) return 1 ;;
        /*)                      return 0 ;;
        *)                       return 1 ;;
    esac
}

# Work queue: files still to scan. bash 3.2 (macOS system bash) — indexed
# arrays only, no associative arrays; "already copied?" is just a file test.
QUEUE=("$BIN")
qi=0
while [[ $qi -lt ${#QUEUE[@]} ]]; do
    f="${QUEUE[$qi]}"; qi=$((qi + 1))
    # Skip the `path:` header AND (for a dylib) its own LC_ID_DYLIB line — the
    # id was rewritten to @rpath/... when we copied it in, so the @* filter in
    # needs_bundling() drops it naturally.
    while read -r dep _; do
        needs_bundling "$dep" || continue
        base="$(basename "$dep")"
        if [[ ! -f "$FRAMEWORKS/$base" ]]; then
            echo "    + $base  ($dep)"
            cp "$dep" "$FRAMEWORKS/$base"
            chmod u+w "$FRAMEWORKS/$base"
            install_name_tool -id "@rpath/$base" "$FRAMEWORKS/$base"
            # A bundled dylib resolves its OWN siblings from its own directory.
            install_name_tool -add_rpath "@loader_path" "$FRAMEWORKS/$base" 2>/dev/null || true
            QUEUE+=("$FRAMEWORKS/$base")
        fi
        install_name_tool -change "$dep" "@rpath/$base" "$f"
    done < <(otool -L "$f" | tail -n +2 | sed 's/^[[:space:]]*//')
done

if [[ -n "$(ls -A "$FRAMEWORKS" 2>/dev/null)" ]]; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN" 2>/dev/null || true
else
    rmdir "$FRAMEWORKS"
fi

# Hard gate: a single surviving brew path means the DMG is machine-specific
# again. Fail the packaging rather than ship a bundle that dies on dyld.
LEAKED="$(otool -L "$BIN" | tail -n +2 | sed 's/^[[:space:]]*//' \
          | awk '{print $1}' | grep -E '^(/usr/local|/opt/homebrew|/opt/local)' || true)"
if [[ -n "$LEAKED" ]]; then
    echo "ERROR: POM1 still references non-bundled Homebrew/MacPorts dylibs:" >&2
    echo "$LEAKED" >&2
    exit 1
fi
echo "    bundle is self-contained (no /usr/local, /opt/homebrew, /opt/local refs)"

# Architecture report + gate. When POM1_MACOS_ARCHS asks for a Universal 2
# build, every Mach-O we ship must actually carry every requested slice —
# including the cc65 tools, which the in-app DevBench executes: an arm64-only
# toolchain cannot run on an Intel Mac at all, and an x86_64-only one silently
# requires Rosetta on Apple Silicon.
echo "==> Architectures"
if [[ -z "${POM1_MACOS_ARCHS:-}" ]]; then
    echo "    POM1 : $(lipo -info "$BIN" | sed 's/.*:[^:]*: *//')  (native build)"
else
    MACHOS=("$BIN")
    for f in "$FRAMEWORKS"/*.dylib "$DATA_ROOT"/cc65/bin/*; do
        [[ -f "$f" ]] && MACHOS+=("$f")
    done
    for f in "${MACHOS[@]}"; do
        HAVE="$(lipo -info "$f" 2>/dev/null | sed 's/.*:[^:]*: *//')"
        for want in ${POM1_MACOS_ARCHS//;/ }; do
            case " $HAVE " in
                *" $want "*) ;;
                *) echo "ERROR: $(basename "$f") is missing the $want slice (has: $HAVE)" >&2
                   exit 1 ;;
            esac
        done
        echo "    $(basename "$f") : $HAVE"
    done
    echo "    all Mach-O files carry: ${POM1_MACOS_ARCHS//;/ }"
fi

# ---------- 3b. Ad-hoc codesign --------------------------------------------
# Without ANY signature, a quarantined download trips the misleading
# "POM1.app is damaged and can't be opened" Gatekeeper dialog (and on Apple
# Silicon an unsigned arm64 binary won't execute at all). An ad-hoc signature
# ("-" = no identity, no cert/$99 Apple account needed) downgrades that to the
# normal, recoverable "unidentified developer" prompt and makes the binary
# valid on arm64. It does NOT notarize — users still clear quarantine on first
# launch (right-click → Open, or `xattr -cr`; see README.txt below).
# --deep signs nested code too (the cc65 toolchain binaries under
# Contents/Resources/cc65/bin); inside-out so containers verify.
echo "==> Ad-hoc codesigning $STAGING (deep)"
if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "$STAGING" \
        && codesign --verify --deep --strict "$STAGING" \
        && echo "    signed (ad-hoc)" \
        || echo "    WARNING: ad-hoc codesign failed — bundle ships unsigned."
else
    echo "    WARNING: codesign not found — bundle ships unsigned."
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

First launch -- "POM1.app is damaged and can't be opened"?
--------------------------------------------------------
POM1 is ad-hoc signed but NOT notarized (notarization needs a paid Apple
Developer account). When your browser downloads the DMG, macOS tags it
"quarantined". On a quarantined, non-notarized app Gatekeeper shows the
MISLEADING "is damaged and can't be opened / move it to the Trash" dialog.
The app is NOT damaged -- do NOT trash it. Clear the quarantine flag:

   1. Open Terminal and run (drag POM1.app onto the Terminal window to
      fill in the path):

         xattr -cr /Applications/POM1.app

      Then double-click POM1.app -- it opens normally.
      -- or --
   2. Right-click POM1.app → Open → Open (confirm the warning).
      -- or --
   3. System Settings → Privacy & Security → scroll to the blocked-app
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

# hdiutil is the flakiest thing in this script: on a busy CI runner
# create/attach intermittently fails with "Resource temporarily unavailable"
# or a device-busy error. It cost release 1.9.4 a full re-run — the build had
# already passed every universal/self-contained gate and died only here.
# Retry each hdiutil call a few times, and NOT with -quiet: when it does fail
# for a real reason we need to see why, instead of a bare "exit 1".
hdiutil_retry() {
    local what="$1"; shift
    local attempt
    for attempt in 1 2 3; do
        if hdiutil "$@"; then return 0; fi
        echo "    hdiutil $what failed (attempt $attempt/3) — retrying…" >&2
        sleep $((5 * attempt))
    done
    echo "ERROR: hdiutil $what failed after 3 attempts." >&2
    return 1
}

hdiutil_retry create create \
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
ATTACH_LOG="dist/hdiutil-attach.log"
# The volume icon is cosmetic — never let a flaky attach sink a release that
# has already passed every correctness gate. Retry, then carry on without the
# custom icon if the mount still refuses.
if hdiutil_retry attach attach -nobrowse -readwrite -noverify \
                               -noautoopen "$SCRATCH" >"$ATTACH_LOG" 2>&1; then
    MOUNT="$(awk -F '\t' '$3 ~ /^\/Volumes\// {print $3}' "$ATTACH_LOG" | tail -1)"
    if [[ -n "$MOUNT" && -e "$MOUNT/.VolumeIcon.icns" ]]; then
        SetFile -a C "$MOUNT" 2>/dev/null || true
    fi
    [[ -n "$MOUNT" ]] && hdiutil detach -quiet "$MOUNT" || true
else
    echo "    WARNING: could not mount the scratch image — DMG ships without" >&2
    echo "             the custom volume icon (cosmetic only)." >&2
fi
rm -f "$ATTACH_LOG"

# Convert to the final compressed, read-only UDZO distributable.
hdiutil_retry convert convert -format UDZO -o "$DMGPATH" "$SCRATCH"
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
