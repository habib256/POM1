#!/bin/bash
# ===========================================================================
# install.sh — Installe POM1 en mode BORNE (kiosk) sur un Raspberry Pi.
#
# Cible : Raspberry Pi OS Bookworm 64-bit (Pi 4 / 400 / 5).
# Effet : au démarrage, le Pi lance POM1 en plein écran, sans bureau. Rien
#         d'autre. Cassettes / programmes / éditeurs d'images restent
#         accessibles depuis les menus de POM1.
#
# Ce script est l'ORCHESTRATEUR ; le travail est réparti :
#     build_native_pi.sh   compile (palier GLES, -mcpu du cœur réel, PGO)
#     install_kiosk.sh     durcit le système et pose le service systemd
#
# À lancer DEPUIS le Pi, une seule fois, avec le dépôt déjà cloné :
#     cd /chemin/vers/POM1
#     ./packaging/raspberrypi/install.sh              # build + borne
#     ./packaging/raspberrypi/install.sh --pgo        # + profil (long, +10-20 %)
#     ./packaging/raspberrypi/install.sh --no-kiosk   # compiler seulement
#
# Idempotent. Ne PAS lancer en root (sudo est appelé au coup par coup) — il
# configure l'utilisateur courant comme utilisateur de la borne.
#
# Pour SORTIR de la borne plus tard : Ctrl+Alt+F2 (autre console) ou ssh, puis
#     ./packaging/raspberrypi/install.sh --uninstall
# ===========================================================================

set -euo pipefail

# ---- 0. Garde-fous --------------------------------------------------------
if [ "$(id -u)" -eq 0 ]; then
    echo "Ne lance PAS ce script en root/sudo. Lance-le en tant qu'utilisateur normal." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
KIOSK_USER="$(id -un)"

DO_KIOSK=1
BUILD_ARGS=()
KIOSK_ARGS=()
UNINSTALL=0
FORCE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --uninstall|--disable-kiosk) UNINSTALL=1 ;;
        --no-kiosk)                  DO_KIOSK=0 ;;
        --pgo)                       BUILD_ARGS+=(--pgo) ;;
        --desktop-gl)                BUILD_ARGS+=(--desktop-gl) ;;
        --keep-wifi)                 KIOSK_ARGS+=(--keep-wifi) ;;
        --keep-audio-server)         KIOSK_ARGS+=(--keep-audio-server) ;;
        --force)                     FORCE=1; KIOSK_ARGS+=(--force) ;;
        *) echo "Option inconnue : $1" >&2; exit 1 ;;
    esac
    shift
done

if [ "$UNINSTALL" = 1 ]; then
    exec sudo "${SCRIPT_DIR}/install_kiosk.sh" --uninstall --user "${KIOSK_USER}"
fi

ARCH="$(uname -m)"
if [ "${ARCH}" != "aarch64" ] && [ "$FORCE" = 0 ]; then
    echo "Attention : architecture '${ARCH}' (attendu aarch64 = Raspberry Pi OS 64-bit)."
    echo "Relance avec --force pour ignorer ce contrôle."
    exit 1
fi

echo "=== Installation POM1 borne — utilisateur '${KIOSK_USER}', dépôt '${REPO_ROOT}' ==="

# ---- 1. Dépendances de compilation ----------------------------------------
# Les paquets de la session X (xinit, xserver-xorg-core…) sont installés par
# install_kiosk.sh, qui sait lesquels prendre SANS recommandations (sinon
# xserver-xorg-core tire une partie du bureau, serveur de son compris).
#
# libgles2-mesa-dev + libegl1-mesa-dev : palier GLES 3.0, le chemin que le
# pilote V3D du Pi implémente réellement (cf. build_native_pi.sh).
echo "--- 1/4 Paquets de compilation ---"
sudo apt update
sudo apt install -y \
    git cmake pkg-config build-essential \
    libglfw3-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev mesa-utils \
    libasound2-dev \
    cc65

# ---- 2. Dear ImGui (dépendance de build, non vendorée) --------------------
echo "--- 2/4 Dear ImGui ---"
if [ ! -d "${REPO_ROOT}/imgui" ]; then
    git clone --depth 1 --branch v1.92.9-docking https://github.com/ocornut/imgui.git "${REPO_ROOT}/imgui"
else
    echo "  déjà présent (${REPO_ROOT}/imgui)"
fi

# ---- 3. Compilation -------------------------------------------------------
echo "--- 3/4 Compilation (plusieurs minutes ; --pgo : ~2× plus, et ça les vaut) ---"
"${SCRIPT_DIR}/build_native_pi.sh" ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}

if [ ! -x "${REPO_ROOT}/build/POM1" ]; then
    echo "ERREUR : la compilation n'a pas produit build/POM1." >&2
    exit 1
fi

# ---- 4. Mode borne --------------------------------------------------------
if [ "$DO_KIOSK" = 0 ]; then
    echo ""
    echo "=== Compilation terminée (mode borne non installé, --no-kiosk) ==="
    echo "  Lancer : cd ${REPO_ROOT} && ./build/POM1"
    exit 0
fi

echo "--- 4/4 Mode borne (X nu + service systemd) ---"
sudo "${SCRIPT_DIR}/install_kiosk.sh" --user "${KIOSK_USER}" ${KIOSK_ARGS[@]+"${KIOSK_ARGS[@]}"}

echo ""
echo "=== Terminé ! ==="
echo "Au prochain redémarrage, le Pi démarrera directement dans POM1, plein écran."
echo ""
echo "  Redémarrer maintenant : sudo reboot"
echo "  Réglages de la borne  : /etc/pom1-kiosk.conf (profil, latence audio…)"
echo "  Sortir de la borne    : Ctrl+Alt+F2 (ou ssh), puis"
echo "                          ${SCRIPT_DIR}/install.sh --uninstall"
