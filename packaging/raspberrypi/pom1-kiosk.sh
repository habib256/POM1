#!/bin/bash
# ---------------------------------------------------------------------------
# pom1-kiosk.sh — lanceur POM1 pour le mode borne (kiosk) sur Raspberry Pi 400.
#
# Rôle :
#   1. Poser les surcharges Mesa nécessaires au GPU VideoCore VI / V3D du Pi.
#   2. Se placer à la racine du dépôt (POM1 résout roms/, software/, fonts/,
#      cassettes/, sdcard/, pic/ par rapport au répertoire courant).
#   3. Lancer le binaire ./build/POM1.
#
# Le script se localise tout seul : il déduit la racine du dépôt à partir de
# son propre chemin (packaging/raspberrypi/ → ../../). Aucun chemin en dur, il
# marche quel que soit l'endroit où le dépôt est cloné.
#
# --- Le piège OpenGL du Pi -------------------------------------------------
# POM1 (src/main_imgui.cpp) demande un contexte OpenGL 3.2 « Core ». Le pilote
# Mesa/V3D du Pi 4/400 n'expose en OpenGL desktop que la version 3.1 : sans
# surcharge, la création de la fenêtre échoue. MESA_GL_VERSION_OVERRIDE fait
# remonter la version rapportée à 3.3 et laisse passer la requête 3.2 Core ;
# ImGui n'utilise que des fonctionnalités présentes dès 3.1, donc tout rend
# correctement. GLSL est figé à 150 (ce que POM1 compile côté desktop).
# ---------------------------------------------------------------------------

set -euo pipefail

# Surcharges Mesa — indispensables sur le GPU V3D du Pi (cf. en-tête ci-dessus).
export MESA_GL_VERSION_OVERRIDE="${MESA_GL_VERSION_OVERRIDE:-3.3}"
export MESA_GLSL_VERSION_OVERRIDE="${MESA_GLSL_VERSION_OVERRIDE:-150}"

# Racine du dépôt = deux niveaux au-dessus de ce script.
SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BIN="${REPO_ROOT}/build/POM1"
if [ ! -x "${BIN}" ]; then
    echo "ERREUR : ${BIN} introuvable. Lance d'abord packaging/raspberrypi/install.sh." >&2
    exit 1
fi

# POM1 doit tourner depuis la racine (chemins de données relatifs au cwd).
cd "${REPO_ROOT}"
exec "${BIN}" "$@"
