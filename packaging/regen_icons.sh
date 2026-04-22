#!/usr/bin/env bash
# Regenerate every OS-native icon artefact from the single source of truth
# (pic/icon.png, 128x128 RGBA). Commit the output.
#
# Produces:
#   packaging/macos/POM1.icns                         (via sips + iconutil; macOS only)
#   packaging/windows/POM1.ico                        (via python3 + Pillow)
#   packaging/linux/hicolor/128x128/apps/POM1.png    (byte-for-byte copy of pic/icon.png)
#
# Usage:  ./packaging/regen_icons.sh
# Run from the repo root.

set -euo pipefail

SRC="pic/icon.png"
[[ -f "$SRC" ]] || { echo "ERROR: $SRC missing — run from repo root."; exit 1; }

echo "==> Source: $SRC"

# ---------- Linux: straight copy of the 128x128 master ----------------------
LINUX_ICON="packaging/linux/hicolor/128x128/apps/POM1.png"
mkdir -p "$(dirname "$LINUX_ICON")"
cp "$SRC" "$LINUX_ICON"
echo "==> Wrote $LINUX_ICON"

# ---------- Windows: multi-res .ico via Pillow ------------------------------
WIN_ICO="packaging/windows/POM1.ico"
python3 - <<'PY'
from pathlib import Path
from PIL import Image
src = Image.open("pic/icon.png").convert("RGBA")
sizes = [(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)]
out = Path("packaging/windows/POM1.ico")
out.parent.mkdir(parents=True, exist_ok=True)
src.save(out, format="ICO", sizes=sizes)
print(f"==> Wrote {out} ({out.stat().st_size} B, sizes={sizes})")
PY

# ---------- macOS: multi-res .icns via sips + iconutil ----------------------
# Bail gracefully when not on macOS — the two tools are macOS-only. The
# already-committed POM1.icns keeps the build reproducible on Linux/Windows.
if [[ "$(uname -s)" != "Darwin" ]] || ! command -v sips >/dev/null || ! command -v iconutil >/dev/null; then
    echo "==> Skipping POM1.icns regen (sips + iconutil require macOS)."
    exit 0
fi

ICNS_OUT="packaging/macos/POM1.icns"
ICONSET=$(mktemp -d)/POM1.iconset
mkdir -p "$ICONSET"

# Apple's expected iconset layout: <size>x<size>[@2x].png.
# @2x entries are double-resolution variants for Retina displays.
# Source is 128x128 — we ship up to 128@2x (256) so macOS doesn't upscale
# garbage to Dock size. Anything above 128 would be pointless upscaling.
declare -a entries=(
    "16:icon_16x16.png"
    "32:icon_16x16@2x.png"
    "32:icon_32x32.png"
    "64:icon_32x32@2x.png"
    "128:icon_128x128.png"
    "256:icon_128x128@2x.png"
)
for e in "${entries[@]}"; do
    size="${e%%:*}"
    name="${e##*:}"
    sips -z "$size" "$size" "$SRC" --out "$ICONSET/$name" >/dev/null
done

iconutil -c icns "$ICONSET" -o "$ICNS_OUT"
rm -rf "$(dirname "$ICONSET")"
echo "==> Wrote $ICNS_OUT ($(stat -f%z "$ICNS_OUT") B)"
