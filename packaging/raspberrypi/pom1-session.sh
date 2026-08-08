#!/bin/sh
# =============================================================================
#  pom1-session.sh — Session X de la borne : lancé PAR startx, dans le serveur X.
#
#  Rôle : neutraliser ce qu'un X nu fait encore de gênant pour une borne
#  (économiseur, DPMS, damier gris), puis DEVENIR POM1 (exec → pas de shell qui
#  traîne en mémoire, et le code de sortie de POM1 est celui de la session, donc
#  le Restart=always de systemd relance vraiment).
#
#  Les réglages viennent de /etc/pom1-kiosk.conf (écrit par install_kiosk.sh).
#
#  (c) 2000-2026 VERHILLE Arnaud — projet POM1.
# =============================================================================
set -eu

[ -r /etc/pom1-kiosk.conf ] && . /etc/pom1-kiosk.conf

POM1_ROOT="${POM1_ROOT:-/home/pi/POM1}"
POM1_BIN="${POM1_BIN:-$POM1_ROOT/build/POM1}"
POM1_PRESET="${POM1_PRESET:-}"
# `-` et non `:-` : une valeur volontairement vide doit rester vide (elle
# supprime l'option — utile si POM1_BIN est un binaire antérieur à
# --audio-latency, qui refuserait le drapeau inconnu et ne démarrerait pas).
POM1_AUDIO_LATENCY="${POM1_AUDIO_LATENCY-120}"
POM1_WM="${POM1_WM:-}"
POM1_EXTRA_ARGS="${POM1_EXTRA_ARGS:-}"

# Pas d'extinction d'écran ni d'économiseur : une borne affiche en continu.
xset s off -dpms s noblank 2>/dev/null || true
# Fond noir : sans ça X affiche son damier gris pendant le chargement, et la
# moindre bordure non couverte par la fenêtre reste grise.
xsetroot -solid black 2>/dev/null || true

# Repli documenté : gestionnaire de fenêtres qui maximise (l'ancien montage de
# la borne). Inutile avec --fullscreen, gardé pour les installations où la
# bascule plein écran GLFW se comporte mal.
if [ -n "$POM1_WM" ] && command -v "$POM1_WM" >/dev/null 2>&1; then
    case "$POM1_WM" in
        matchbox*) "$POM1_WM" -use_titlebar no & ;;
        *)         "$POM1_WM" & ;;
    esac
fi

# POM1 résout roms/, software/, cassettes/, sdcard/, fonts/, pic/ PAR RAPPORT AU
# RÉPERTOIRE COURANT : la borne doit démarrer depuis la racine du dépôt.
cd "$POM1_ROOT"

set --
# Le plein écran vient de POM1 (CliPlan::fullscreen) et non d'un WM : une
# fenêtre de moins à composer par trame. Sauté si un WM est demandé, qui s'en
# charge alors lui-même.
[ -z "$POM1_WM" ] && set -- "$@" --fullscreen
[ -n "$POM1_PRESET" ] && set -- "$@" --preset "$POM1_PRESET"
[ -n "$POM1_AUDIO_LATENCY" ] && set -- "$@" --audio-latency "$POM1_AUDIO_LATENCY"
# shellcheck disable=SC2086  # POM1_EXTRA_ARGS est volontairement redécoupé
[ -n "$POM1_EXTRA_ARGS" ] && set -- "$@" $POM1_EXTRA_ARGS

echo "[pom1-session] exec $POM1_BIN $*"
exec "$POM1_BIN" "$@"
