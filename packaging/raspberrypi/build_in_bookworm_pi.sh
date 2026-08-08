#!/usr/bin/env bash
# =============================================================================
#  build_in_bookworm_pi.sh — Binaires de la BORNE Raspberry Pi, à lancer DANS un
#  conteneur debian:bookworm arm64 (dépôt monté sur /work) :
#
#    docker run --rm -v "$PWD":/work -w /work -e POM1_MCPU=cortex-a72 \
#        debian:bookworm bash /work/packaging/raspberrypi/build_in_bookworm_pi.sh
#
#  Recette portée de NeoST (packaging/raspberry/build_in_bookworm_pi.sh) — voir
#  docs/PERFORMANCE.md côté NeoST pour les mesures.
#
#  POURQUOI CE SCRIPT EXISTE À CÔTÉ de packaging/linux/build_in_bookworm_arm64.sh
#  (l'AppImage aarch64 de release) :
#
#   · -mcpu=<cœur exact> au lieu d'aarch64 générique. L'AppImage publiée doit
#     tourner du Pi 3 au Pi 5 ; la borne, elle, ne tourne QUE sur SON Pi. La
#     boucle chaude de POM1 est l'interpréteur M6502 — un branchement indirect
#     sur l'opcode suivi de branches conditionnelles rares — exactement le genre
#     de code qui profite du bon modèle de coût.
#
#   · compilation en DEUX PASSES guidée par profil (PGO) puis LTO. GCC apprend
#     quelle issue de chaque branche est la fréquente et range les blocs en
#     conséquence : moins de sauts pris, cache d'instructions bien mieux utilisé,
#     ce qui compte double sur un Cortex-A72 (32 Ko de L1i). Sur NeoST, d'où
#     vient la recette : PGO seul −20 %, PGO+LTO −34 % de temps CPU à code
#     identique. Et c'est GRATUIT pour l'utilisateur : l'entraînement tourne ICI,
#     sur le runner ARM64, pas sur le Pi (contrairement à build_native_pi.sh
#     --pgo, qui fait payer les deux passes au Pi lui-même).
#
#   · DEUX paquets issus du MÊME build, sans recompilation :
#       - un tar.gz `pom1-pi400-aarch64.tar.gz` pour la BORNE SANS BUREAU. Une
#         AppImage v2 réclame libfuse2, absent de Pi OS Lite bookworm : la borne
#         devrait l'extraire à chaque démarrage pour rien. L'arborescence du
#         tar.gz est celle du dépôt (build/POM1 + roms/ software/ …) parce que
#         c'est exactement ce que pom1-session.sh attend — POM1 résout ses
#         données par rapport au RÉPERTOIRE COURANT.
#       - une AppImage `POM1-<ver>-pi400-aarch64.AppImage` pour Pi OS AVEC
#         BUREAU, où l'on veut un fichier unique cliquable.
#
#  ⚠ Ces paquets NE REMPLACENT PAS ceux de la release : l'AppImage
#  `POM1-<ver>-aarch64.AppImage` reste le paquet aarch64 GÉNÉRIQUE. D'où le tag
#  `pi400` dans le nom — le job `publish` d'une release aplatit tous les
#  artefacts dans un même dossier, et deux paquets homonymes s'y écraseraient en
#  silence.
#
#  POURQUOI bookworm : Raspberry Pi OS EST Debian bookworm (glibc 2.36). Bâtir
#  sur le runner ubuntu-24.04-arm estamperait GLIBC_2.39 et le binaire ne
#  démarrerait sur aucun Pi. Le script vérifie ce plancher et échoue s'il est
#  dépassé. Le conteneur tourne NATIVEMENT sur le runner arm64 — pas de QEMU.
#
#  Variables : POM1_MCPU (défaut cortex-a72), POM1_VERSION, POM1_PGO (0 pour une
#  passe unique), POM1_REQUIRE_CC65, IMGUI_TAG.
#
#  (c) 2000-2026 VERHILLE Arnaud — projet POM1.
# =============================================================================
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
ROOT="$PWD"

MCPU="${POM1_MCPU:-cortex-a72}"      # a72 = Pi 4 / Pi 400 ; a76 = Pi 5 ; a53 = Pi 3
DO_PGO="${POM1_PGO:-1}"
BUILD_DIR=build-borne
PROFDIR="$ROOT/build-borne-profile"

export DEBIAN_FRONTEND=noninteractive

# --- Dépendances -------------------------------------------------------------
# Même liste que build_in_bookworm_arm64.sh : libgles2-mesa-dev / libegl1-mesa-dev
# sont les en-têtes du palier GLES, cc65 vient de Debian (2.19) pour que le
# DevBench embarque asm + C, file/desktop-file-utils sont des prérequis
# d'appimagetool.
apt-get update -qq
apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git wget ca-certificates \
    python3 file desktop-file-utils fuse libfuse2 zsync binutils \
    libglfw3-dev libgles2-mesa-dev libegl1-mesa-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
    cc65

# Le dépôt monté appartient à l'uid de l'hôte, pas à root. APRÈS l'apt-get :
# debian:bookworm est une image nue, sans git.
git config --global --add safe.directory '*'

echo "[borne] cible : -mcpu=$MCPU"
echo 'int main(){}' | g++ -x c++ -mcpu="$MCPU" -o /dev/null - \
    || { echo "ERREUR : -mcpu=$MCPU refusé par $(g++ --version | head -1)"; exit 1; }

# --- cc65 + Dear ImGui -------------------------------------------------------
tools/build_cc65_bundle.sh --out dist/cc65-bundle
rm -rf imgui
git clone --depth 1 --branch "${IMGUI_TAG:-v1.92.9-docking}" \
    https://github.com/ocornut/imgui.git

# --- Configuration -----------------------------------------------------------
# -static-libstdc++/-static-libgcc : ne dépendre QUE de la glibc de bookworm, pas
# du libstdc++ de l'image de build — une image Pi OS plus ancienne reste servie.
ARCH_FLAGS="-mcpu=$MCPU -mtune=$MCPU"

configure() {                # configure <drapeaux-supplémentaires> <LTO ON/OFF>
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DPOM1_GLES=ON \
        -DPOM1_LTO="$2" \
        -DPOM1_ENABLE_TESTS=OFF \
        -DCMAKE_C_FLAGS="$ARCH_FLAGS $1" \
        -DCMAKE_CXX_FLAGS="$ARCH_FLAGS $1" \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc $1"
}
# ⚠ La CIBLE s'appelle `pom1_imgui` (le binaire, lui, s'appelle POM1) :
# `--target POM1` ne construit rien, en silence.
build() { cmake --build "$BUILD_DIR" --target pom1_imgui -j"$(nproc)"; }

if [ "$DO_PGO" = "1" ]; then
    # ⚠ LES DEUX PASSES PARTAGENT LE MÊME RÉPERTOIRE DE BUILD. GCC nomme chaque
    # .gcda d'après le CHEMIN ABSOLU de l'objet compilé : instrumenter dans un
    # répertoire et relire depuis un autre ne trouve AUCUN profil — et
    # -Wno-missing-profile (indispensable pour les TU d'interface, que le
    # parcours headless n'entraîne pas) rend l'échec parfaitement muet : binaire
    # sans le moindre gain et sans le moindre message. D'où le contrôle plus bas.
    rm -rf "$PROFDIR"; mkdir -p "$PROFDIR"

    echo "[borne] PGO passe 1/2 — binaire instrumenté (sans LTO)"
    configure "-fprofile-generate=$PROFDIR" OFF
    build

    echo "[borne] PGO — parcours d'entraînement"
    packaging/raspberrypi/pgo_train.sh "$BUILD_DIR/POM1" "$ROOT"

    echo "[borne] profils collectés : $(find "$PROFDIR" -name '*.gcda' | wc -l)"
    # Les TU qui portent la boucle chaude. Si elles manquent, le parcours
    # d'entraînement n'a rien exécuté d'utile et le PGO serait un placebo — on
    # préfère casser le build que livrer une borne qui se croit optimisée.
    for must in M6502 Memory GraphicsCard TMS9918; do
        find "$PROFDIR" -name "*${must}*.gcda" | grep -q . \
            || { echo "ERREUR : aucun profil pour $must — parcours d'entraînement muet"; exit 1; }
    done

    echo "[borne] PGO passe 2/2 — build final (profil + LTO)"
    # -fprofile-partial-training : les objets NON entraînés (ImGui, éditeurs,
    # pile CRT — le parcours est headless) sont optimisés normalement au lieu
    # d'être traités comme du code froid ; sans lui l'interface sortirait dégradée.
    # -fprofile-correction : les compteurs d'un programme multi-thread peuvent
    # être légèrement incohérents (POM1 a un thread d'émulation + un thread
    # audio) ; on répare au lieu d'échouer.
    configure "-fprofile-use=$PROFDIR -fprofile-correction -fprofile-partial-training -Wno-missing-profile" ON
    build 2>&1 | tee /tmp/pom1-pgo-build.log
    # Contrôle final : si les profils n'avaient PAS été relus pour le CŒUR, GCC
    # l'aurait dit. L'avertissement est neutralisé globalement (il le faut pour
    # l'interface), on regarde donc explicitement les sources du cœur.
    if grep -E "src/(M6502|Memory|GraphicsCard|TMS9918).*(profile count data file not found|missing-profile)" \
            /tmp/pom1-pgo-build.log >/dev/null 2>&1; then
        echo "ERREUR : profil non relu pour une source du cœur (chemins de build désaccordés ?)"
        exit 1
    fi
else
    echo "[borne] PGO désactivé (POM1_PGO=0) — passe unique"
    configure "" ON
    build
fi

BIN="$BUILD_DIR/POM1"
test -x "$BIN" || { echo "ERREUR : $BIN non construit"; exit 1; }

# --- Contrôles qui doivent échouer ICI, pas sur la borne ---------------------
readelf -h "$BIN" | grep -q AArch64 \
    || { echo "ERREUR : pas un binaire AArch64"; exit 1; }

# Plancher glibc : le symbole GLIBC_x.y le plus haut exigé doit rester <= 2.36,
# sinon Pi OS refuse de lancer le binaire (« version `GLIBC_2.39' not found »).
MAX=$(objdump -T "$BIN" 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -V | tail -1)
echo "[borne] symbole glibc le plus haut : ${MAX:-aucun}"
test "$(printf '%s\nGLIBC_2.36\n' "$MAX" | sort -V | tail -1)" = "GLIBC_2.36" \
    || { echo "ERREUR : le binaire exige $MAX > GLIBC_2.36"; exit 1; }

# Le paquet GLES ne doit JAMAIS tirer le libGL de bureau : sur un Pi c'est le
# rastériseur logiciel de repli, et en dépendre serait une régression silencieuse
# à 2 images/s plutôt qu'une erreur de lien. (libGLdispatch/libGLX peuvent
# apparaître : c'est la couche de dispatch de libglvnd, tirée par libGLESv2.)
if ldd "$BIN" | grep -qE 'lib(GL|OpenGL)\.so'; then
    echo "ERREUR : le build GLES lie le libGL de bureau — POM1_GLES sans effet"
    ldd "$BIN" | grep -E 'GL|EGL'
    exit 1
fi
ldd "$BIN" | grep -E 'GLES|EGL' || true

# --- Nom des paquets ---------------------------------------------------------
# Le paquet porte le MODÈLE DE MACHINE, pas un nom d'usage : c'est du binaire
# taillé pour un cœur, et il sert aussi bien à la borne sans bureau qu'à un Pi OS
# de bureau.
case "$MCPU" in
    cortex-a72) PKG_TAG=pi400 ;;      # Pi 4 / Pi 400
    cortex-a76) PKG_TAG=pi5   ;;
    cortex-a53) PKG_TAG=pi3   ;;
    *)          PKG_TAG="$MCPU" ;;
esac
VERSION="${POM1_VERSION:-$(cat "$ROOT/VERSION")}"

# --- Paquet 1 : AppImage (elle produit l'AppDir dont vit le tar.gz) ----------
# POM1_VERSION porte le tag : build_appimage.sh nomme sa sortie
# POM1-<VERSION>-<arch>.AppImage, on obtient donc POM1-<ver>-pi400-aarch64 —
# distinct du POM1-<ver>-aarch64 générique de la release.
echo "[borne] AppImage (tag $PKG_TAG)…"
mkdir -p build && install -m 755 "$BIN" build/POM1   # emplacement attendu par le packager
POM1_APPIMAGE_SKIP_BUILD=1 \
APPIMAGE_EXTRACT_AND_RUN=1 \
POM1_VERSION="${VERSION}-${PKG_TAG}" \
POM1_CC65_BUNDLE="$ROOT/dist/cc65-bundle/cc65" \
    packaging/linux/build_appimage.sh
ls -lh dist/*.AppImage

# --- Paquet 2 : tar.gz pour la borne sans bureau -----------------------------
# ⚠ DÉRIVÉ DE L'AppDir, PAS du binaire nu. La première version de ce script
# copiait `build/POM1` tel quel et le paquet mourait au lancement sur
# « libglfw.so.3: cannot open shared object file » : Pi OS Lite n'installe pas
# libglfw3, et un paquet dont tout l'intérêt est de ne RIEN compiler sur la borne
# ne peut pas exiger un `apt install` pour démarrer. linuxdeploy a déjà fait le
# travail pour l'AppImage — il a rassemblé les bibliothèques dans usr/lib et
# réécrit le RUNPATH du binaire en `$ORIGIN/../lib`. On réutilise ce même AppDir :
# binaire dans build/, bibliothèques dans lib/, et `$ORIGIN/..` retombe
# exactement sur la racine de l'arbre déballé.
APPDIR="build-appimage/AppDir"
test -d "$APPDIR/usr/share/POM1" || { echo "ERREUR : AppDir absent — build_appimage.sh a-t-il tourné ?"; exit 1; }

# Contrôle du RUNPATH : sans lui le tar.gz repartirait chercher ses libs dans
# /usr/lib et on retomberait sur l'échec ci-dessus, mais SUR LA BORNE.
RPATH=$(readelf -d "$APPDIR/usr/bin/POM1" | grep -E 'RUNPATH|RPATH' || true)
echo "[borne] RUNPATH du binaire empaqueté : ${RPATH:-<aucun>}"
grep -q '\$ORIGIN/../lib' <<<"$RPATH" \
    || { echo "ERREUR : le binaire de l'AppDir n'a pas le RUNPATH \$ORIGIN/../lib — le tar.gz ne trouverait pas ses bibliothèques"; exit 1; }

STAGE="dist/$PKG_TAG"
rm -rf "$STAGE" && mkdir -p "$STAGE/build" dist
install -m 755 "$APPDIR/usr/bin/POM1" "$STAGE/build/POM1"
cp -r "$APPDIR/usr/lib" "$STAGE/lib"          # $ORIGIN/../lib depuis build/POM1

# Arborescence de données = celle du DÉPÔT, pas un $PREFIX FHS : pom1-session.sh
# fait `cd $POM1_ROOT` puis lance `$POM1_ROOT/build/POM1`, et POM1 résout roms/,
# software/, cassettes/, sdcard/, fonts/, pic/ par rapport au répertoire courant.
# Déballé par `tar -xzf … -C /home/pi/POM1`, l'unité systemd démarre sans une
# seule variable à surcharger. (L'AppDir les a déjà toutes, CODETANKDEV.rom
# généré compris — inutile de recopier depuis le dépôt.)
for d in roms fonts software sketchs pic cassettes sdcard cfcard disks ini_defaults dev; do
    [ -d "$APPDIR/usr/share/POM1/$d" ] && cp -r "$APPDIR/usr/share/POM1/$d" "$STAGE/$d"
done
# cc65 sous build/ : le DevBench sonde <exe>/cc65/bin en premier, et l'exe est
# build/POM1.
if [ -d "$APPDIR/usr/share/POM1/cc65" ]; then
    cp -r "$APPDIR/usr/share/POM1/cc65" "$STAGE/build/cc65"
    tools/verify_cc65_bundle.sh "$STAGE/build/cc65" || {
        [ "${POM1_REQUIRE_CC65:-0}" = "1" ] && \
            { echo "ERREUR : POM1_REQUIRE_CC65=1 mais le bundle cc65 est incomplet"; exit 1; }
        echo "[borne] AVERTISSEMENT : bundle cc65 incomplet"
    }
elif [ "${POM1_REQUIRE_CC65:-0}" = "1" ]; then
    echo "ERREUR : POM1_REQUIRE_CC65=1 mais aucun cc65 dans l'AppDir"; exit 1
fi

# Les scripts de borne voyagent avec le paquet : install_kiosk.sh calcule sa
# racine en remontant de deux niveaux, donc l'arbre déballé s'installe lui-même.
mkdir -p "$STAGE/packaging"
cp -r packaging/raspberrypi "$STAGE/packaging/raspberrypi"
cp VERSION "$STAGE/VERSION"

TGZ="dist/pom1-${PKG_TAG}-aarch64.tar.gz"
tar -czf "$TGZ" -C "$STAGE" .
echo "[borne] OK : $TGZ"
ls -lh "$TGZ"
