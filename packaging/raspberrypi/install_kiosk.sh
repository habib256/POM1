#!/usr/bin/env bash
# =============================================================================
#  install_kiosk.sh — Transforme un Raspberry Pi OS Lite en borne POM1.
#
#  Résultat : mise sous tension → écran Apple-1, sans bureau, sans gestionnaire
#  de connexion, sans serveur de son. Le Pi ne fait plus QUE de l'émulation.
#
#  Ce que le script fait, et POURQUOI (dans l'ordre des gains mesurables) :
#
#   1. X NU (ni WM ni compositeur) sur le VT 1, lancé par systemd, avec POM1 en
#      --fullscreen. → supprime une recopie plein écran par trame.
#   2. Aucun serveur de son : miniaudio parle à ALSA en direct (HDMI).
#      → supprime resampling + graphe PipeWire, 1re cause de micro-coupures.
#      L'unité systemd pose en plus LimitRTPRIO=99, sans quoi la demande
#      SCHED_FIFO du thread audio de miniaudio échoue EN SILENCE.
#   3. Gouverneur `performance` sur tous les cœurs.
#      → Pi OS met `ondemand` : les rampes de fréquence produisent exactement le
#        hachage périodique « ça rame par à-coups ».
#   4. IRQ matérielles épinglées sur le cœur 0 (irqaffinity=0).
#      → la boucle d'émulation ne partage plus son cœur avec l'USB/Ethernet.
#   5. Bluetooth coupé au device-tree, Wi-Fi aussi SI un câble Ethernet est
#      branché (sinon on se couperait de SSH — cf. le garde-fou).
#   6. Swap coupé, services inutiles coupés, boot silencieux.
#
#  ⚠ NON PORTÉ depuis NeoST : le mode enceinte Bluetooth (--bluetooth-audio,
#  PipeWire + A2DP). Il demande de réintroduire tout le serveur de son que le
#  point 2 retire, plus un appairage. Si la borne doit sortir en Bluetooth,
#  installer PipeWire à la main et lancer ce script avec --keep-audio-server :
#  miniaudio classe PulseAudio AVANT ALSA, donc POM1 se branche tout seul sur
#  pipewire-pulse. Prévoir 150-250 ms de retard son/image, inhérents à l'A2DP.
#
#  Usage (SUR le Pi, en root) :
#      sudo packaging/raspberrypi/install_kiosk.sh                 # utilisateur `pi`
#      sudo packaging/raspberrypi/install_kiosk.sh --user borne
#      sudo packaging/raspberrypi/install_kiosk.sh --keep-wifi     # borne en réseau
#      sudo packaging/raspberrypi/install_kiosk.sh --uninstall     # tout défaire
#
#  PRÉ-REQUIS : le binaire doit exister (packaging/raspberrypi/build_native_pi.sh
#  ou install.sh, qui appelle les deux).
#
#  (c) 2000-2026 VERHILLE Arnaud — projet POM1.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
KIOSK_USER="pi"
KEEP_WIFI=0
KEEP_AUDIO_SERVER=0
UNINSTALL=0
ALSA_CARD=""
FORCE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --user)              KIOSK_USER="$2"; shift 2 ;;
        --keep-wifi)         KEEP_WIFI=1; shift ;;
        --keep-audio-server) KEEP_AUDIO_SERVER=1; shift ;;
        --alsa-card)         ALSA_CARD="$2"; shift 2 ;;
        --uninstall)         UNINSTALL=1; shift ;;
        --force)             FORCE=1; shift ;;
        *) echo "Option inconnue : $1"; exit 1 ;;
    esac
done

[ "$(id -u)" -eq 0 ] || { echo "ERREUR : lancer avec sudo."; exit 1; }

MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo inconnu)"
case "$MODEL" in
    *"Raspberry Pi"*) ;;
    *) [ "$FORCE" = 1 ] || { echo "ERREUR : « $MODEL » n'est pas un Raspberry Pi (--force pour passer outre)."; exit 1; } ;;
esac

# Bookworm a déplacé la partition de démarrage de /boot vers /boot/firmware.
BOOTDIR=/boot/firmware
[ -f "$BOOTDIR/config.txt" ] || BOOTDIR=/boot
[ -f "$BOOTDIR/config.txt" ] || { echo "ERREUR : config.txt introuvable (ni /boot/firmware ni /boot)."; exit 1; }

MARK_BEGIN="# >>> POM1 kiosk (install_kiosk.sh) — ne pas éditer à la main"
MARK_END="# <<< POM1 kiosk"

log() { echo "[install_kiosk] $*"; }

# ---------------------------------------------------------------------------
#  Édition idempotente : bloc délimité par des marqueurs, remplacé s'il existe.
# ---------------------------------------------------------------------------
write_block() {           # write_block <fichier> <contenu…>
    local file="$1"; shift
    local tmp; tmp="$(mktemp)"
    [ -f "$file" ] && sed "/^${MARK_BEGIN//\//\\/}$/,/^${MARK_END}$/d" "$file" > "$tmp" || true
    if [ $# -gt 0 ]; then
        { echo "$MARK_BEGIN"; printf '%s\n' "$@"; echo "$MARK_END"; } >> "$tmp"
    fi
    # Sauvegarde unique de l'ORIGINAL : une seconde exécution ne doit pas écraser
    # la sauvegarde par une version déjà modifiée (sinon --uninstall ne rend rien).
    [ -f "$file" ] && [ ! -f "$file.pom1-orig" ] && cp -a "$file" "$file.pom1-orig"
    cat "$tmp" > "$file"
    rm -f "$tmp"
}

# cmdline.txt est une LIGNE UNIQUE : pas de bloc, on ajoute/retire des jetons.
cmdline_add() {           # cmdline_add <jeton…>
    local file="$BOOTDIR/cmdline.txt" line tok key
    [ -f "$file" ] || return 0
    [ -f "$file.pom1-orig" ] || cp -a "$file" "$file.pom1-orig"
    line="$(tr -d '\n' < "$file")"
    for tok in "$@"; do
        key="${tok%%=*}"
        # On retire d'abord toute occurrence de la CLÉ : une valeur différente
        # d'un passage précédent doit être remplacée, pas dupliquée.
        line="$(echo " $line " | sed -E "s/ ${key}(=[^ ]*)? / /g")"
        line="$line $tok"
    done
    echo "$line" | tr -s ' ' | sed 's/^ //;s/ $//' > "$file"
}

cmdline_restore() {
    [ -f "$BOOTDIR/cmdline.txt.pom1-orig" ] && \
        mv "$BOOTDIR/cmdline.txt.pom1-orig" "$BOOTDIR/cmdline.txt" || true
}

# ---------------------------------------------------------------------------
#  Désinstallation
# ---------------------------------------------------------------------------
if [ "$UNINSTALL" = 1 ]; then
    log "désinstallation…"
    systemctl disable --now "pom1-kiosk@${KIOSK_USER}.service" 2>/dev/null || true
    systemctl disable --now pom1-perf.service 2>/dev/null || true
    rm -f /etc/systemd/system/pom1-kiosk@.service /etc/systemd/system/pom1-perf.service
    rm -f /usr/local/bin/pom1-kiosk.sh
    systemctl enable getty@tty1.service 2>/dev/null || true
    systemctl set-default graphical.target >/dev/null 2>&1 || \
        systemctl set-default multi-user.target >/dev/null
    write_block "$BOOTDIR/config.txt"
    cmdline_restore
    systemctl daemon-reload
    log "fait. Service, réglages de démarrage et gouverneur retirés."
    log "NOTE : /etc/pom1-kiosk.conf, le dépôt et les paquets installés restent en place."
    exit 0
fi

id "$KIOSK_USER" >/dev/null 2>&1 || { echo "ERREUR : utilisateur '$KIOSK_USER' inexistant."; exit 1; }

# GARDE-FOU : couper le Wi-Fi sur une machine dont c'est le SEUL lien réseau,
# c'est se couper de SSH sur une borne qui n'a peut-être ni clavier ni écran de
# service. On ne le fait donc QUE si un lien Ethernet est effectivement branché.
if [ "$KEEP_WIFI" = 0 ]; then
    if ! grep -qs 1 /sys/class/net/eth0/carrier; then
        log "ATTENTION : pas de lien Ethernet détecté → le Wi-Fi est CONSERVÉ."
        log "            (--force pour le couper quand même)"
        [ "$FORCE" = 1 ] || KEEP_WIFI=1
    fi
fi

# ---------------------------------------------------------------------------
#  1. Paquets : le strict minimum pour un X sans bureau
# ---------------------------------------------------------------------------
log "installation des paquets (X nu + ALSA)…"
export DEBIAN_FRONTEND=noninteractive
# -o DPkg::Lock::Timeout : au premier démarrage, unattended-upgrades tient
# souvent encore le verrou dpkg — sans attente, l'installation échouerait sec.
APT_OPTS=(-o DPkg::Lock::Timeout=900)
apt-get "${APT_OPTS[@]}" update -qq
# --no-install-recommends est ESSENTIEL : sans lui, xserver-xorg-core tire une
# partie du bureau (et donc un serveur de son) par recommandation.
apt-get "${APT_OPTS[@]}" install -y --no-install-recommends \
    xserver-xorg-core xserver-xorg-legacy xserver-xorg-input-libinput \
    xinit x11-xserver-utils alsa-utils libglfw3 libgl1-mesa-dri

# Xwrapper : par défaut Debian n'autorise startx que depuis une console de
# connexion. Le service systemd n'en est pas une → « Only console users are
# allowed to run the X server ».
install -d /etc/X11
cat > /etc/X11/Xwrapper.config <<'EOF'
# Écrit par POM1 install_kiosk.sh — X est lancé par un service systemd, pas
# par une session de connexion interactive.
allowed_users=anybody
needs_root_rights=no
EOF

# Accès direct au KMS, aux périphériques d'entrée et au VT sans passer par root.
usermod -aG video,input,tty,audio,render "$KIOSK_USER" || true

# ---------------------------------------------------------------------------
#  2. La sortie son — pas de serveur, miniaudio → ALSA en direct
# ---------------------------------------------------------------------------
if [ "$KEEP_AUDIO_SERVER" = 0 ]; then
    log "suppression des serveurs de son (miniaudio → ALSA en direct)…"
    for p in pipewire pipewire-pulse pipewire-alsa wireplumber pulseaudio; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" \
            && apt-get "${APT_OPTS[@]}" purge -y "$p" || true
    done
    # Les unités utilisateur survivent à la purge si elles ont été copiées.
    sudo -u "$KIOSK_USER" systemctl --user mask pipewire.socket pipewire-pulse.socket \
        pulseaudio.socket 2>/dev/null || true

    # --- Carte de sortie ALSA ------------------------------------------------
    # Le Pi 400 a DEUX sorties HDMI (vc4hdmi0/1) et pas de jack. On prend celle
    # où un écran est réellement branché : l'ELD n'est valide que si un moniteur
    # répond.
    if [ -z "$ALSA_CARD" ]; then
        for eld in /proc/asound/card*/eld#*; do
            [ -r "$eld" ] || continue
            grep -q "eld_valid.*1" "$eld" 2>/dev/null || continue
            cand="${eld#/proc/asound/card}"; cand="${cand%%/*}"
            grep -qi "vc4hdmi" "/proc/asound/card$cand/id" 2>/dev/null || continue
            ALSA_CARD="$cand"; log "sortie HDMI détectée : carte $cand ($(cat "/proc/asound/card$cand/id"))"
            break
        done
    fi
    if [ -n "$ALSA_CARD" ]; then
        cat > /etc/asound.conf <<EOF
# Écrit par POM1 install_kiosk.sh — carte de sortie de la borne.
defaults.pcm.card $ALSA_CARD
defaults.ctl.card $ALSA_CARD
EOF
    else
        log "AUCUNE sortie HDMI détectée — brancher l'écran puis relancer, ou --alsa-card N"
    fi
else
    log "serveur de son conservé (--keep-audio-server) — attention aux micro-coupures."
fi

# ---------------------------------------------------------------------------
#  3. Gouverneur `performance` — le meilleur rapport gain/effort
# ---------------------------------------------------------------------------
log "gouverneur CPU → performance"
cat > /etc/systemd/system/pom1-perf.service <<'EOF'
[Unit]
Description=POM1 — gouverneur CPU performance (borne)
# La borne tourne à pleine charge en permanence : `ondemand` ne fait que
# rajouter de la latence de montée en fréquence, jamais d'économie utile.
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'for g in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do echo performance > "$g" || true; done'
[Install]
WantedBy=multi-user.target
EOF
systemctl enable pom1-perf.service >/dev/null

# ---------------------------------------------------------------------------
#  4. Démarrage : config.txt / cmdline.txt
# ---------------------------------------------------------------------------
log "réglages de démarrage dans $BOOTDIR"
CFG_LINES=(
    "disable_splash=1"        # pas d'arc-en-ciel Broadcom au démarrage
    "boot_delay=0"
    "arm_boost=1"             # Pi 4/400 : 1,5 → 1,8 GHz (officiel, sans surcadençage)
    "dtoverlay=disable-bt"
)
[ "$KEEP_WIFI" = 0 ] && CFG_LINES+=("dtoverlay=disable-wifi")
CFG_LINES+=(
    ""
    "# Surcadençage : SEULEMENT avec un vrai dissipateur et l'alimentation"
    "# officielle. Vérifier ensuite \`vcgencmd get_throttled\` == 0x0."
    "#over_voltage=6"
    "#arm_freq=2000"
)
write_block "$BOOTDIR/config.txt" "${CFG_LINES[@]}"

# irqaffinity=0     : IRQ matérielles sur le cœur 0 → cœurs 1-3 pour l'émulation.
# consoleblank=0    : la console ne s'éteint jamais (écran noir en expo).
# quiet/logo.nologo : démarrage sans texte ni logo framboise.
cmdline_add irqaffinity=0 consoleblank=0 quiet loglevel=3 logo.nologo vt.global_cursor_default=0

# ---------------------------------------------------------------------------
#  5. Services inutiles à une borne hors ligne
# ---------------------------------------------------------------------------
log "extinction des services inutiles…"
# avahi-daemon est VOLONTAIREMENT conservé : c'est lui qui fait répondre
# `<hôte>.local`, seule façon commode de reprendre la main sur une borne sans
# écran de service. Son coût est négligeable devant l'émulation.
SERVICES_OFF="ModemManager triggerhappy cups cups-browsed lightdm gdm3
              apt-daily.timer apt-daily-upgrade.timer man-db.timer
              bluetooth hciuart"
for s in $SERVICES_OFF; do
    systemctl disable --now "$s" 2>/dev/null || true
done
[ "$KEEP_WIFI" = 0 ] && systemctl disable --now wpa_supplicant 2>/dev/null || true
# L'attente de réseau ajoute des secondes de boot pour rien sur une borne.
systemctl mask NetworkManager-wait-online.service systemd-networkd-wait-online.service 2>/dev/null || true
# Swap : sur carte SD, un swap-in au milieu d'une trame = un trou audio garanti.
if [ -x /usr/sbin/dphys-swapfile ]; then
    dphys-swapfile swapoff 2>/dev/null || true
    systemctl disable --now dphys-swapfile 2>/dev/null || true
fi
systemctl set-default multi-user.target >/dev/null

# ---------------------------------------------------------------------------
#  6. Réglages de la borne + service
# ---------------------------------------------------------------------------
# Le point d'entrée est copié dans /usr/local/bin (chemin fixe dans l'unité) ;
# la session, elle, est lue depuis le dépôt, qui reste la source de vérité.
install -m 755 "$HERE/pom1-kiosk.sh" /usr/local/bin/pom1-kiosk.sh
chmod 755 "$HERE/pom1-session.sh"

# Créé UNE SEULE FOIS : une réinstallation ne doit pas écraser les choix de
# l'exploitant (profil de machine, arguments).
if [ ! -f /etc/pom1-kiosk.conf ]; then
    log "création de /etc/pom1-kiosk.conf (réglages de la borne)"
    cat > /etc/pom1-kiosk.conf <<EOF
# Réglages de la borne POM1 — lus par pom1-kiosk.sh / pom1-session.sh.
# Après modification : sudo systemctl restart pom1-kiosk@${KIOSK_USER}

# Racine du dépôt : POM1 y résout roms/, software/, cassettes/, sdcard/…
POM1_ROOT="$ROOT"
POM1_BIN="$ROOT/build/POM1"

# Profil de machine au démarrage (index de --preset, cf. \`POM1 --list-presets\`).
# Vide = ce que dit ini/startup (défaut : POM1 Fantasy, le dernier profil).
POM1_PRESET=""

# Coussin audio en ms (--audio-latency, défaut POM1 ~17, borné [20,250]).
# Sur Pi 4 : 120 est un bon point de départ. Monter à 150 si le son craque
# encore ; descendre vers 60 s'il paraît en retard sur l'image.
POM1_AUDIO_LATENCY="120"

# Gestionnaire de fenêtres. VIDE = X nu + POM1 --fullscreen (recommandé : une
# recopie de trame en moins). "matchbox-window-manager" remet l'ancien montage
# si la bascule plein écran pose problème sur cette installation.
POM1_WM=""

# Arguments supplémentaires passés tels quels (ex. --cpu-max, --tape …).
POM1_EXTRA_ARGS=""
EOF
else
    # Le fichier n'est PAS réécrit (choix de l'exploitant), mais un POM1_ROOT
    # périmé — dépôt déplacé depuis la première installation — donnerait un écran
    # noir sans le moindre message : le service démarrerait un X qui ne trouve
    # ni session ni binaire.
    CONF_ROOT="$(. /etc/pom1-kiosk.conf 2>/dev/null; echo "${POM1_ROOT:-}")"
    if [ -n "$CONF_ROOT" ] && [ "$CONF_ROOT" != "$ROOT" ]; then
        log "ATTENTION : /etc/pom1-kiosk.conf pointe vers « $CONF_ROOT »"
        log "            alors que ce dépôt est « $ROOT ». Corriger POM1_ROOT/POM1_BIN"
        log "            ou supprimer le fichier pour le regénérer."
    fi
fi

install -m 644 "$HERE/pom1-kiosk@.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable "pom1-kiosk@${KIOSK_USER}.service" >/dev/null

# ---------------------------------------------------------------------------
#  Bilan
# ---------------------------------------------------------------------------
echo
log "installation terminée."
echo
if [ ! -x "$ROOT/build/POM1" ]; then
    echo "  ⚠ $ROOT/build/POM1 est ABSENT. Compiler d'abord :"
    echo "      packaging/raspberrypi/build_native_pi.sh --pgo"
    echo
fi
echo "  Réglages de la borne : /etc/pom1-kiosk.conf"
echo "  Sortie audio dispo   :"; aplay -l 2>/dev/null | sed 's/^/      /' || true
echo
echo "  Redémarrer pour appliquer config.txt/cmdline.txt :  sudo reboot"
echo "  Tester sans redémarrer                           :  sudo systemctl start pom1-kiosk@${KIOSK_USER}"
echo "  Journal (dont [Audio] et [CRT] GLSL …)           :  journalctl -u pom1-kiosk@${KIOSK_USER} -f"
echo "  Tout défaire                                     :  sudo $0 --uninstall --user ${KIOSK_USER}"
