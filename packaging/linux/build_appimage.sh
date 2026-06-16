#!/bin/bash
# Build d'un AppImage POM1 pour Linux x86_64.
#
# Stratégie :
#   1. Compilation classique (./setup_pom1.sh + cmake/make) — l'utilisateur
#      doit l'avoir déjà fait, sinon on la déclenche.
#   2. Layout AppDir miroir d'une install /usr classique :
#        usr/bin/POM1                  binaire stripé
#        usr/lib/                      glfw + GLU + libX* déployés par
#                                      linuxdeploy, rpath = $ORIGIN/../lib
#        usr/share/POM1/{roms,fonts,software,pic,cassettes,sdcard,cfcard}
#        usr/share/applications/POM1.desktop
#        usr/share/icons/hicolor/128x128/apps/POM1.png
#   3. AppRun (packaging/linux/AppRun) qui :
#        - amorce $XDG_DATA_HOME/POM1 avec des symlinks read-only et copie
#          les répertoires writable une fois (sdcard, cfcard, ini)
#        - chdir là-dedans pour que les probes cwd-relatives existants
#          (Memory, fonts, icone, cassette par défaut) résolvent correctement
#        - exec usr/bin/POM1 avec LD_LIBRARY_PATH = usr/lib
#   4. linuxdeploy bundle les libs non-blacklist, appimagetool emballe.
#
# Sortie : dist/POM1-<VERSION>-x86_64.AppImage

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${REPO_ROOT}"

VERSION="${POM1_VERSION:-1.9.2}"
DIST="${REPO_ROOT}/dist"
WORK="${REPO_ROOT}/build-appimage"
APPDIR="${WORK}/AppDir"
TOOLS="${WORK}/tools"
RESOURCES="${APPDIR}/usr/share/POM1"

# 1. Build du binaire si absent.
if [ ! -f "${REPO_ROOT}/build/POM1" ]; then
    echo "[appimage] Build POM1 (cmake/make)…"
    mkdir -p "${REPO_ROOT}/build"
    (cd "${REPO_ROOT}/build" && cmake .. >/dev/null && make -j"$(nproc)")
fi

# 2. Téléchargement de linuxdeploy + appimagetool si nécessaire (extraits
# pour pouvoir tourner sans FUSE — pratique en CI ou en sandbox).
mkdir -p "${TOOLS}"
fetch_extract() {
    local url="$1" outname="$2" appdir="${TOOLS}/${2}.AppDir"
    if [ -d "${appdir}" ]; then return 0; fi
    echo "[appimage] Téléchargement de ${outname}…"
    wget -q "${url}" -O "${TOOLS}/${outname}.AppImage"
    chmod +x "${TOOLS}/${outname}.AppImage"
    (cd "${TOOLS}" && "./${outname}.AppImage" --appimage-extract >/dev/null && mv squashfs-root "${outname}.AppDir")
}
fetch_extract "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" linuxdeploy
fetch_extract "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" appimagetool

# 3. AppDir from scratch.
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" \
         "${APPDIR}/usr/lib" \
         "${APPDIR}/usr/share/applications" \
         "${APPDIR}/usr/share/icons/hicolor/128x128/apps" \
         "${RESOURCES}"

cp "${REPO_ROOT}/build/POM1" "${APPDIR}/usr/bin/POM1"
strip "${APPDIR}/usr/bin/POM1" 2>/dev/null || true

cp "${REPO_ROOT}/packaging/linux/AppRun" "${APPDIR}/AppRun"
chmod +x "${APPDIR}/AppRun"
cp "${REPO_ROOT}/packaging/linux/POM1.desktop" "${APPDIR}/POM1.desktop"
cp "${REPO_ROOT}/packaging/linux/hicolor/128x128/apps/POM1.png" "${APPDIR}/POM1.png"
ln -sf POM1.png "${APPDIR}/.DirIcon"

# Données embarquées : tout ce que les probes cwd-relatives cherchent.
for d in roms fonts software pic cassettes sdcard cfcard; do
    if [ -d "${REPO_ROOT}/${d}" ]; then
        cp -r "${REPO_ROOT}/${d}" "${RESOURCES}/${d}"
    fi
done

# 3b. cc65 toolchain bundle (optionnel) → DevBench autonome, sans cc65 système.
# Source : $POM1_CC65_BUNDLE, sinon dist/cc65-bundle/cc65, sinon auto-build si
# cc65 est installé. POM1 le trouve via <exe>/../share/POM1/cc65/bin (sonde
# relative à l'exe) ; AppRun exporte aussi POM1_CC65_DIR + CC65_HOME.
CC65_TREE=""
if [ -n "${POM1_CC65_BUNDLE:-}" ] && [ -d "${POM1_CC65_BUNDLE}/bin" ]; then
    CC65_TREE="${POM1_CC65_BUNDLE}"
elif [ -d "${REPO_ROOT}/dist/cc65-bundle/cc65/bin" ]; then
    CC65_TREE="${REPO_ROOT}/dist/cc65-bundle/cc65"
elif command -v ca65 >/dev/null 2>&1; then
    echo "[appimage] cc65 détecté — génération du bundle…"
    "${REPO_ROOT}/tools/build_cc65_bundle.sh" --out "${REPO_ROOT}/dist/cc65-bundle" >/dev/null \
        && CC65_TREE="${REPO_ROOT}/dist/cc65-bundle/cc65"
fi
if [ -n "${CC65_TREE}" ]; then
    echo "[appimage] cc65 bundle : ${CC65_TREE}"
    cp -r "${CC65_TREE}" "${RESOURCES}/cc65"
    # The DevBench's linker cfgs + libs (release bundles otherwise omit dev/).
    mkdir -p "${RESOURCES}/dev"
    for d in cc65 lib apple1-videocard-lib; do
        [ -d "${REPO_ROOT}/dev/${d}" ] && cp -r "${REPO_ROOT}/dev/${d}" "${RESOURCES}/dev/${d}"
    done
else
    echo "[appimage] (pas de cc65 bundle — DevBench limité au Woz-hex sans cc65 système)"
fi

# 4. linuxdeploy : bundle les libs (rpath = $ORIGIN/../lib) + recopie
# desktop/icone aux emplacements standards. NO_STRIP=1 : on a déjà strippé.
NO_STRIP=1 "${TOOLS}/linuxdeploy.AppDir/AppRun" \
    --appdir="${APPDIR}" \
    --executable="${APPDIR}/usr/bin/POM1" \
    --desktop-file="${APPDIR}/POM1.desktop" \
    --icon-file="${APPDIR}/POM1.png" \
    >/dev/null

# 5. appimagetool : assemble et compresse en zstd squashfs.
mkdir -p "${DIST}"
OUT="${DIST}/POM1-${VERSION}-x86_64.AppImage"
PATH="${TOOLS}/appimagetool.AppDir/usr/bin:${PATH}" \
ARCH=x86_64 \
VERSION="${VERSION}" \
"${TOOLS}/appimagetool.AppDir/usr/bin/appimagetool" \
    "${APPDIR}" \
    "${OUT}"

echo
echo "[appimage] OK → ${OUT}"
ls -lh "${OUT}"
