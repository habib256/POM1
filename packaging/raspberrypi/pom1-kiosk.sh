#!/bin/sh
# =============================================================================
#  pom1-kiosk.sh — Point d'entrée du service systemd : démarre un X NU.
#
#  « X nu » = un serveur X SANS gestionnaire de fenêtres et SANS compositeur.
#  C'est le cœur du gain sur un Pi : sur un bureau (labwc/wayfire/mutter), chaque
#  trame de POM1 est recopiée une fois de plus par le compositeur avant
#  d'atteindre l'écran. Sans WM, la fenêtre plein écran de POM1 (--fullscreen)
#  est page-flippée directement par KMS.
#
#  Le plein écran vient donc de POM1 lui-même et non plus d'un
#  `matchbox-window-manager` qui maximise la fenêtre. Si jamais --fullscreen
#  posait problème sur une installation, POM1_WM="matchbox" dans
#  /etc/pom1-kiosk.conf remet l'ancien montage (cf. pom1-session.sh).
#
#  -keeptty    : le service fournit déjà /dev/tty1 (TTYPath=), X ne doit pas en
#                ouvrir un autre — sinon il rate son VT et sort sur « no screens ».
#  -novtswitch : la borne ne doit pas pouvoir basculer sur une console texte.
#  -nocursor   : pas de pointeur (unclutter devient inutile).
#
#  (c) 2000-2026 VERHILLE Arnaud — projet POM1.
# =============================================================================
set -eu

[ -r /etc/pom1-kiosk.conf ] && . /etc/pom1-kiosk.conf
POM1_ROOT="${POM1_ROOT:-/home/pi/POM1}"

exec /usr/bin/startx "$POM1_ROOT/packaging/raspberrypi/pom1-session.sh" -- \
     :0 vt1 -keeptty -novtswitch -nolisten tcp -nocursor
