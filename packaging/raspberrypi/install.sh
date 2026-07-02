#!/bin/bash
# ===========================================================================
# install.sh — Installe POM1 en mode BORNE (kiosk) sur un Raspberry Pi 400.
#
# Cible : Raspberry Pi OS Bookworm 64-bit.
# Effet : au démarrage, le Pi se connecte tout seul et lance POM1 en plein
#         écran, sans bureau. Rien d'autre. Cassettes / programmes / éditeurs
#         d'images sont accessibles depuis les menus de POM1.
#
# À lancer DEPUIS le Pi, une seule fois, avec le dépôt déjà cloné :
#     cd /chemin/vers/POM1
#     ./packaging/raspberrypi/install.sh
#
# Idempotent : on peut le relancer sans casse. Ne PAS lancer en root (sudo est
# appelé au coup par coup en interne) — il configure l'utilisateur courant.
#
# Pour SORTIR de la borne plus tard : Ctrl+Alt+F2 (autre console) ou ssh, puis
#     ./packaging/raspberrypi/install.sh --disable-kiosk
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
LAUNCHER="${SCRIPT_DIR}/pom1-kiosk.sh"

ARCH="$(uname -m)"
if [ "${ARCH}" != "aarch64" ] && [ "${1:-}" != "--force" ]; then
    echo "Attention : architecture '${ARCH}' (attendu aarch64 = Raspberry Pi OS 64-bit)."
    echo "Relance avec --force pour ignorer ce contrôle."
    [ "${1:-}" = "--disable-kiosk" ] || exit 1
fi

# ---- Désinstallation du mode borne (option) -------------------------------
if [ "${1:-}" = "--disable-kiosk" ]; then
    echo "Désactivation du mode borne pour ${KIOSK_USER}…"
    rm -f "${HOME}/.xinitrc"
    # Retirer le déclencheur startx du profil de connexion.
    if [ -f "${HOME}/.bash_profile" ]; then
        sed -i '/# >>> POM1 kiosk >>>/,/# <<< POM1 kiosk <<</d' "${HOME}/.bash_profile"
    fi
    echo "Repasse le démarrage en mode normal via : sudo raspi-config  (System Options > Boot / Auto Login)."
    echo "Fait. Redémarre pour retrouver un démarrage classique."
    exit 0
fi

echo "=== Installation POM1 borne — utilisateur '${KIOSK_USER}', dépôt '${REPO_ROOT}' ==="

# ---- 1. Dépendances -------------------------------------------------------
echo "--- 1/5 Installation des paquets (build + session X minimale) ---"
sudo apt update
sudo apt install -y \
    git cmake pkg-config build-essential \
    libglfw3-dev libgl1-mesa-dev mesa-utils \
    libasound2-dev \
    cc65 \
    xserver-xorg xinit x11-xserver-utils \
    matchbox-window-manager unclutter

# ---- 2. Dear ImGui (dépendance de build, non vendorée) --------------------
echo "--- 2/5 Dear ImGui ---"
if [ ! -d "${REPO_ROOT}/imgui" ]; then
    git clone --depth 1 --branch v1.92.7 https://github.com/ocornut/imgui.git "${REPO_ROOT}/imgui"
else
    echo "  déjà présent (${REPO_ROOT}/imgui)"
fi

# ---- 3. Compilation -------------------------------------------------------
echo "--- 3/5 Compilation de POM1 (cela peut prendre plusieurs minutes sur le Pi) ---"
mkdir -p "${REPO_ROOT}/build"
cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build"
cmake --build "${REPO_ROOT}/build" --target pom1_imgui -j"$(nproc)"
chmod +x "${LAUNCHER}"

if [ ! -x "${REPO_ROOT}/build/POM1" ]; then
    echo "ERREUR : la compilation n'a pas produit build/POM1." >&2
    exit 1
fi

# Vérif OpenGL — informatif : montre ce que le pilote V3D rapporte.
echo "  OpenGL rapporté par Mesa (avec la surcharge borne) :"
MESA_GL_VERSION_OVERRIDE=3.3 glxinfo 2>/dev/null | grep -iE "OpenGL (renderer|version)" | sed 's/^/    /' || \
    echo "    (glxinfo indisponible hors session X — sans importance)"

# ---- 4. Autologin console sur tty1 ----------------------------------------
echo "--- 4/5 Connexion automatique en console (tty1) ---"
if command -v raspi-config >/dev/null 2>&1; then
    # B2 = console + autologin. La session X est démarrée depuis .bash_profile.
    sudo raspi-config nonint do_boot_behaviour B2
else
    # Repli générique : override systemd du getty tty1.
    sudo mkdir -p /etc/systemd/system/getty@tty1.service.d
    sudo tee /etc/systemd/system/getty@tty1.service.d/autologin.conf >/dev/null <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin ${KIOSK_USER} --noclear %I \$TERM
EOF
    sudo systemctl daemon-reload
fi

# ---- 5. Démarrage de X + POM1 à la connexion ------------------------------
echo "--- 5/5 Écriture de ~/.xinitrc et du déclencheur startx ---"

# .xinitrc : session X minimale = matchbox plein écran + boucle POM1.
cat > "${HOME}/.xinitrc" <<EOF
#!/bin/sh
# Généré par packaging/raspberrypi/install.sh — mode borne POM1.
xset s off
xset s noblank
xset -dpms
command -v unclutter >/dev/null 2>&1 && unclutter -idle 1 &
matchbox-window-manager -use_titlebar no &
while true; do
    "${LAUNCHER}"
    sleep 1
done
EOF
chmod +x "${HOME}/.xinitrc"

# .bash_profile : lancer startx uniquement sur tty1 et hors X déjà lancé.
PROFILE="${HOME}/.bash_profile"
touch "${PROFILE}"
if ! grep -q "# >>> POM1 kiosk >>>" "${PROFILE}"; then
    cat >> "${PROFILE}" <<'EOF'

# >>> POM1 kiosk >>>
if [ -z "${DISPLAY:-}" ] && [ "$(tty)" = "/dev/tty1" ]; then
    exec startx
fi
# <<< POM1 kiosk <<<
EOF
fi

echo ""
echo "=== Terminé ! ==="
echo "Au prochain redémarrage, le Pi démarrera directement dans POM1, plein écran."
echo ""
echo "  Redémarrer maintenant : sudo reboot"
echo "  Sortir de la borne    : Ctrl+Alt+F2 (ou ssh), puis"
echo "                          ${SCRIPT_DIR}/install.sh --disable-kiosk"
echo ""
echo "Son : si tu n'entends pas les cassettes, choisis la sortie audio avec"
echo "      'sudo raspi-config' (System Options > Audio) et teste 'speaker-test -t wav -c2'."
