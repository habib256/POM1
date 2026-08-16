#!/bin/bash
# ===========================================================================
# ensure_imgui.sh — garantit que <dir> est un checkout Dear ImGui utilisable.
#
# imgui/ n'est PAS vendoré : il est dans .gitignore, chaque poste en a son
# propre clone. Deux pièges, tous deux déjà rencontrés :
#
#   1. Tester la seule présence du DOSSIER laisse en place une copie master
#      périmée. L'échec sort alors au milieu de la compilation, sur
#      ImGuiWindowFlags_NoDocking — une dépendance manquante déguisée en bug
#      POM1.
#   2. Tester la seule présence du docking ne suffit pas non plus : un vieux
#      tag docking (v1.90.x) définit bien ImGuiWindowFlags_NoDocking, passe le
#      test, puis casse la compilation plus loin sur ImGuiChildFlags_Borders
#      (renommé en amont dans la 1.91.1, utilisé par src/bench/CodeBench.cpp).
#      Le patch du sampler Metal dans CMakeLists.txt est lui aussi versionné.
#
# On vérifie donc les DEUX : branche docking ET IMGUI_VERSION_NUM >= le pin.
#
# Usage :  tools/ensure_imgui.sh [dir]          (dir par défaut : ./imgui)
# Réglages par variable d'environnement : IMGUI_TAG, IMGUI_MIN_VERSION_NUM,
# IMGUI_URL — c'est ainsi que les conteneurs de packaging imposent leur pin.
# ===========================================================================
set -euo pipefail

IMGUI_DIR="${1:-imgui}"
IMGUI_TAG="${IMGUI_TAG:-v1.92.9-docking}"
# ImGui encode sa version en MAJOR*10000 + MINOR*100 + PATCH*10 : la 1.92.9
# vaut 19290. À garder aligné sur IMGUI_TAG ci-dessus.
IMGUI_MIN_VERSION_NUM="${IMGUI_MIN_VERSION_NUM:-19290}"
IMGUI_URL="${IMGUI_URL:-https://github.com/ocornut/imgui.git}"

hdr="$IMGUI_DIR/imgui.h"

imgui_version_num() {
    [ -f "$hdr" ] || return 1
    awk '/^#define[ \t]+IMGUI_VERSION_NUM[ \t]/ { print $3; exit }' "$hdr"
}

imgui_has_docking() {
    [ -f "$hdr" ] && grep -q 'ImGuiWindowFlags_NoDocking' "$hdr"
}

imgui_is_usable() {
    local num
    num="$(imgui_version_num 2>/dev/null || true)"
    [ -n "$num" ] || return 1
    case "$num" in ''|*[!0-9]*) return 1 ;; esac
    imgui_has_docking || return 1
    [ "$num" -ge "$IMGUI_MIN_VERSION_NUM" ]
}

fail() { echo "ERREUR : $*" >&2; exit 1; }

if [ ! -d "$IMGUI_DIR" ]; then
    echo "Dear ImGui absent — clonage de $IMGUI_TAG..."
    git clone --depth 1 --branch "$IMGUI_TAG" "$IMGUI_URL" "$IMGUI_DIR" ||
        fail "le clonage de Dear ImGui a échoué."
elif imgui_is_usable; then
    echo "Dear ImGui déjà présent ($(imgui_version_num), docking)."
    exit 0
else
    have="$(imgui_version_num 2>/dev/null || echo 'inconnue')"
    dock="non"; imgui_has_docking && dock="oui"
    echo "$IMGUI_DIR inutilisable (version $have, docking $dock) —" \
         "mise à niveau vers $IMGUI_TAG..."

    git -C "$IMGUI_DIR" rev-parse --git-dir >/dev/null 2>&1 ||
        fail "$IMGUI_DIR n'est pas un dépôt git. Supprimez-le et relancez."

    # --quiet HEAD, pas --porcelain : ce dernier compte les fichiers NON
    # SUIVIS, donc un simple .DS_Store déposé par le Finder bloquerait la mise
    # à niveau — précisément dans le cas qu'on cherche à réparer. HEAD (et non
    # l'index seul) couvre aussi les modifications déjà `git add`-ées.
    git -C "$IMGUI_DIR" diff --quiet HEAD ||
        fail "$IMGUI_DIR contient des modifications locales — rien n'a été touché.
         Sauvegardez-les, puis relancez."

    # --depth 1 sur un clone COMPLET le convertit en clone superficiel et lui
    # fait perdre son historique au prochain gc. On ne l'impose donc qu'à un
    # dépôt déjà superficiel.
    fetch_args=(origin tag "$IMGUI_TAG")
    if [ "$(git -C "$IMGUI_DIR" rev-parse --is-shallow-repository)" = "true" ]; then
        fetch_args=(--depth 1 "${fetch_args[@]}")
    fi
    git -C "$IMGUI_DIR" fetch "${fetch_args[@]}" ||
        fail "impossible de récupérer le tag $IMGUI_TAG."
    git -C "$IMGUI_DIR" checkout --quiet "$IMGUI_TAG" ||
        fail "impossible de basculer sur $IMGUI_TAG."
fi

imgui_is_usable ||
    fail "après installation, $IMGUI_DIR n'est toujours pas utilisable
         (docking + IMGUI_VERSION_NUM >= $IMGUI_MIN_VERSION_NUM attendus)."

echo "Dear ImGui prêt ($(imgui_version_num), $IMGUI_TAG)."
