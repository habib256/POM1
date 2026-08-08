#!/usr/bin/env bash
# =============================================================================
#  build_native_pi.sh — Compilation NATIVE de POM1 sur le Raspberry Pi.
#
#  POURQUOI plutôt qu'un `cmake .. && make` nu :
#
#   1. -mcpu=<le cœur exact>. GCC compile sinon pour un aarch64 générique (il
#      doit pouvoir tourner du Pi 3 au Pi 5) et se prive du modèle de coût du
#      cœur réel. La boucle chaude de POM1 est l'interpréteur M6502 — un switch
#      géant sur l'opcode —, exactement le genre de code qui y gagne.
#
#   2. -DPOM1_GLES=ON. Le V3D des Pi 4/5 (Mesa) ne monte qu'à OpenGL **3.1**
#      côté desktop : la demande 3.2 core échouait, d'où la vieille rustine
#      MESA_GL_VERSION_OVERRIDE=3.3 du lanceur. Le palier GLES 3.0 (contexte
#      EGL) est le chemin que ce pilote implémente vraiment. Cf. CLAUDE.md,
#      section « GL tier ». `--desktop-gl` revient au GL de bureau — POM1 sait
#      désormais redescendre tout seul en 3.1/GLSL 1.40 (main_imgui.cpp), donc
#      c'est un repli viable et non plus un écran noir.
#
#   3. --pgo : compilation guidée par profil. On compile une fois avec des
#      compteurs, on fait tourner un parcours représentatif (pgo_train.sh), puis
#      on recompile en donnant ce profil à GCC. Il sait alors quelle issue de
#      chaque branche est la fréquente et range les blocs en conséquence : moins
#      de sauts pris, cache d'instructions bien mieux utilisé — ce qui compte
#      double sur un Cortex-A72 (32 Ko de L1i, prédicteur modeste devant un cœur
#      x86 de bureau). Sur NeoST, d'où vient cette recette, PGO seul valait −20 %
#      et PGO+LTO −34 % de temps CPU. ⚠ le parcours d'entraînement de POM1 est
#      **headless** : il couvre le cœur (6502, bus, cartes, rastériseurs GEN2 /
#      TMS9918) mais PAS le code d'interface (ImGui, pile CRT), qui reste
#      compilé sans profil (-Wno-missing-profile rend ce cas muet).
#
#  Le LTO, lui, est déjà activé en Release par le CMakeLists (POM1_IPO). Sur un
#  Pi à moins de 2 Go on le coupe (-DPOM1_LTO=OFF) : le lien LTO y déclenche
#  l'OOM-killer, dont le symptôme est un « cc1plus: fatal error: Killed ».
#
#  Usage (SUR le Pi, dans le dépôt cloné) :
#      packaging/raspberrypi/build_native_pi.sh              # build → build/
#      packaging/raspberrypi/build_native_pi.sh --pgo        # 2 passes (RECOMMANDÉ)
#      packaging/raspberrypi/build_native_pi.sh --desktop-gl # sans le palier GLES
#
#  Variables : POM1_BUILD_DIR (défaut « build »), POM1_JOBS, CXX.
#
#  (c) 2000-2026 VERHILLE Arnaud — projet POM1.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
cd "$ROOT"

BUILD_DIR="${POM1_BUILD_DIR:-build}"
DO_PGO=0
USE_GLES=1
for a in "$@"; do
    case "$a" in
        --pgo)        DO_PGO=1 ;;
        --desktop-gl) USE_GLES=0 ;;
        *) echo "Option inconnue : $a  (--pgo, --desktop-gl)"; exit 1 ;;
    esac
done

# --- 1. Identifier le cœur ---------------------------------------------------
# Le modèle est dans /proc/device-tree/model (« Raspberry Pi 4 Model B Rev 1.5 »).
# On ne se fie PAS à -mcpu=native seul : sur certains noyaux 64 bits le MIDR lu
# par GCC est incomplet et la détection retombe sur un générique silencieux.
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo inconnu)"
case "$MODEL" in
    *"Raspberry Pi 5"*)            MCPU=cortex-a76 ;;
    *"Raspberry Pi 4"*|*"Pi 400"*) MCPU=cortex-a72 ;;
    *"Raspberry Pi 3"*)            MCPU=cortex-a53 ;;
    *)                             MCPU=native ;;
esac
# Garde-fou : si le compilateur refuse ce -mcpu (GCC trop ancien, hôte x86), on
# retombe sur générique plutôt que d'échouer 20 minutes plus tard sur un .cpp au
# hasard.
if ! echo 'int main(){}' | ${CXX:-g++} -x c++ -mcpu=$MCPU -o /dev/null - 2>/dev/null; then
    echo "[build_native_pi] AVERTISSEMENT : -mcpu=$MCPU refusé par $(${CXX:-g++} --version | head -1) → générique"
    MCPU=""
fi
ARCH_FLAGS=""
[ -n "$MCPU" ] && ARCH_FLAGS="-mcpu=$MCPU -mtune=$MCPU"

MEM_MB=$(($(awk '/MemTotal/{print $2}' /proc/meminfo) / 1024))
LTO=ON
[ "$MEM_MB" -lt 2000 ] && LTO=OFF

# Un Pi 4 a 4 cœurs mais souvent 2 Go : -j4 sur du C++17 lourd (ImGui + les
# éditeurs) part en OOM-kill. Une tâche par ~900 Mo.
JOBS="${POM1_JOBS:-}"
if [ -z "$JOBS" ]; then
    JOBS=$(( MEM_MB / 900 )); [ "$JOBS" -lt 1 ] && JOBS=1
    NPROC=$(nproc); [ "$JOBS" -gt "$NPROC" ] && JOBS=$NPROC
fi

echo "[build_native_pi] modèle : $MODEL  (${MEM_MB} Mo)"
echo "[build_native_pi] flags  : ${ARCH_FLAGS:-<génériques>} LTO=$LTO GLES=$USE_GLES PGO=$DO_PGO -j$JOBS"

configure() {                 # configure <drapeaux-supplémentaires> <LTO ON/OFF>
    cmake -S "$ROOT" -B "$BUILD_DIR" \
          -DCMAKE_BUILD_TYPE=Release \
          -DPOM1_GLES=$( [ "$USE_GLES" = 1 ] && echo ON || echo OFF ) \
          -DPOM1_LTO="$2" \
          -DPOM1_ENABLE_TESTS=OFF \
          -DCMAKE_C_FLAGS="$ARCH_FLAGS $1" \
          -DCMAKE_CXX_FLAGS="$ARCH_FLAGS $1" \
          -DCMAKE_EXE_LINKER_FLAGS="$1"
}

# ⚠ Le nom de la CIBLE est `pom1_imgui` (le binaire, lui, s'appelle POM1) :
# `--target POM1` ne construit rien, en silence.
build() { cmake --build "$BUILD_DIR" --target pom1_imgui -j"$JOBS"; }

if [ "$DO_PGO" = "1" ]; then
    PROFDIR="$ROOT/$BUILD_DIR-profile"
    # ⚠ LES DEUX PASSES PARTAGENT LE MÊME RÉPERTOIRE DE BUILD : GCC nomme les
    # fichiers .gcda d'après le chemin ABSOLU de l'objet compilé. Instrumenter
    # dans un répertoire et relire depuis un autre ne trouve AUCUN profil — et
    # -Wno-missing-profile (indispensable pour les objets d'interface, que le
    # parcours headless n'entraîne pas) rend l'échec totalement muet : binaire
    # sans le moindre gain et sans le moindre message. D'où le contrôle plus bas.
    rm -rf "$PROFDIR"; mkdir -p "$PROFDIR"

    echo "[build_native_pi] PGO passe 1/2 : binaire instrumenté (sans LTO)"
    configure "-fprofile-generate=$PROFDIR" OFF
    build

    echo "[build_native_pi] PGO : parcours d'entraînement (quelques minutes)"
    "$ROOT/packaging/raspberrypi/pgo_train.sh" "$BUILD_DIR/POM1" "$ROOT"
    for must in M6502 Memory GraphicsCard; do
        find "$PROFDIR" -name "*${must}*.gcda" | grep -q . \
            || { echo "ERREUR : aucun profil pour $must — l'entraînement n'a rien exécuté"; exit 1; }
    done

    echo "[build_native_pi] PGO passe 2/2 : build final (profil, LTO=$LTO)"
    configure "-fprofile-use=$PROFDIR -fprofile-correction -fprofile-partial-training -Wno-missing-profile" "$LTO"
    build
else
    configure "" "$LTO"
    build
fi

echo "[build_native_pi] OK : $BUILD_DIR/POM1"
echo "[build_native_pi] étape suivante : packaging/raspberrypi/install_kiosk.sh"
