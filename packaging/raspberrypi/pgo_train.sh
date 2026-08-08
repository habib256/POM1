#!/usr/bin/env bash
# =============================================================================
#  pgo_train.sh — Parcours d'ENTRAÎNEMENT pour la compilation guidée par profil.
#
#  Appelé par build_native_pi.sh --pgo entre les deux passes. Le binaire passé
#  en argument est la version INSTRUMENTÉE (-fprofile-generate) : chaque
#  exécution dépose des compteurs (.gcda), que la seconde passe relit.
#
#  POURQUOI le PGO compte ici : la boucle chaude de POM1 est l'interpréteur
#  M6502 — un branchement indirect sur l'opcode suivi d'un grand nombre de
#  branches conditionnelles rares (page-cross, BCD, flags) — plus les
#  rastériseurs GEN2 / TMS9918. Sans profil, GCC suppose les deux issues de
#  chaque test équiprobables ; avec, il range les blocs pour que le cas fréquent
#  tombe en séquence : moins de sauts pris, cache d'instructions bien mieux
#  utilisé. Sur un Cortex-A72 (32 Ko de L1i) c'est là que se joue le débit.
#
#  CE QUE LE PARCOURS COUVRE — un profil trop étroit est PIRE que pas de profil :
#  il fait déclarer « froid » du code qui ne l'est pas. On balaie donc les
#  grandes familles de charge :
#    · Woz Monitor nu (chemin le plus universel)
#    · Integer BASIC (interprète 6502 dense, arithmétique)
#    · GEN2 HGR : rastériseur bitmap + course au faisceau
#    · TMS9918 : machine à états VDP + sprites + rendu par ligne
#    · microSD / CFFA1 : VIA, ROM, chemins de bus différents
#
#  ⚠ LIMITE ASSUMÉE : tout se passe en **headless** (`--headless`, `--dump-*`),
#  donc les TU d'interface (ImGui, Screen_ImGui, pile CRT, éditeurs) ne sont PAS
#  entraînées et restent compilées sans profil. C'est justement ce que
#  -Wno-missing-profile rend silencieux côté build. Le gain porte sur le cœur.
#
#  Usage :  pgo_train.sh <chemin/POM1> [racine-du-dépôt]
#
#  (c) 2000-2026 VERHILLE Arnaud — projet POM1.
# =============================================================================
set -uo pipefail

POM1="${1:?usage: pgo_train.sh <build/POM1> [racine]}"
ROOT="${2:-$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)}"
cd "$ROOT"
POM1="$(cd "$(dirname "$POM1")" && pwd)/$(basename "$POM1")"
[ -x "$POM1" ] || { echo "ERREUR : $POM1 introuvable ou non exécutable"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Une charge d'entraînement ne DOIT PAS faire échouer la compilation : un
# programme absent (dépôt partiel) se solde par un profil un peu moins riche,
# pas par un build cassé.
run() {                      # run <libellé> <arguments…>
    local label="$1"; shift
    printf '  [pgo] %-40s' "$label"
    if "$POM1" "$@" >/dev/null 2>&1; then echo "ok"; else echo "ignoré"; fi
}

# Charges sans point d'arrêt naturel : POM1 --headless tourne jusqu'à SIGTERM.
# `timeout` en envoie un, que POM1 traite proprement (pom1_headless_signal_
# handler → retour de main), donc les .gcda sont bien écrits — un `kill -9` les
# perdrait en silence.
run_timed() {                # run_timed <secondes> <libellé> <arguments…>
    local secs="$1" label="$2"; shift 2
    printf '  [pgo] %-40s' "$label"
    if timeout -s TERM "$secs" "$POM1" "$@" >/dev/null 2>&1; then echo "ok"
    else echo "ok (arrêt sur délai)"; fi
}

have() { [ -f "$1" ]; }

echo "[pgo_train] parcours d'entraînement — $POM1"

# --- 1. Woz Monitor nu : le chemin le plus universel -------------------------
run_timed 12 "Woz Monitor (Apple-1 nu, 8 Ko)"      --headless --preset 3 --cpu-max
run_timed 12 "Replica-1 + Krusader (64 Ko)"        --headless --preset 6 --cpu-max

# --- 2. Integer BASIC : interprète 6502 dense --------------------------------
# Une boucle de calcul entière, tapée au clavier émulé : elle martèle ADC/SBC,
# les comparaisons et le parcours de la table des variables.
cat > "$TMP/basic.txt" <<'EOF'
E000R
10 S=0
20 FOR I=1 TO 3000
30 S=S+I*3-1
40 NEXT I
50 PRINT S
60 GOTO 10
RUN
EOF
run_timed 20 "Integer BASIC (boucle de calcul)" \
    --headless --preset 4 --cpu-max --paste "$TMP/basic.txt"

# --- 3. GEN2 HGR : rastériseur bitmap + décodage NTSC ------------------------
# --dump-gen2-frame s'arrête tout seul (capture puis exit) : pas de timeout.
have tests/gfx/hgr_testcard.bin && \
    run "GEN2 HGR (mire de test, rendu complet)" \
        --preset 11 --load "0xE000:tests/gfx/hgr_testcard.bin" --run 0xE000 \
        --dump-after-cycles 2000000 --dump-gen2-frame "$TMP/gen2a.png"

for p in "software/Graphic HGR/GEN2Bounces.txt" \
         "software/Graphic HGR/HGR_Maze3D.txt" \
         "software/Graphic HGR/GEN2Snake.txt"; do
    have "$p" && run "GEN2 HGR — $(basename "$p" .txt)" \
        --preset 11 --load "0x0300:$p" \
        --dump-after-cycles 6000000 --dump-gen2-frame "$TMP/$(basename "$p" .txt).png"
done

# --- 4. TMS9918 : machine à états VDP, sprites, rendu par ligne --------------
for p in "software/Graphic TMS9918/TMS_Plasma.txt" \
         "software/Graphic TMS9918/TMS_Galaga.txt" \
         "software/Graphic TMS9918/TMS_Stars.txt"; do
    have "$p" && run "TMS9918 — $(basename "$p" .txt)" \
        --preset 9 --load "0x0300:$p" \
        --dump-after-cycles 6000000 --dump-tms-frame "$TMP/$(basename "$p" .txt).png"
done

# --- 5. Autres chemins de bus (VIA, ROM d'extension) -------------------------
run_timed 12 "microSD + Applesoft Lite"  --headless --preset 8  --cpu-max
run_timed 12 "CFFA1 + Applesoft Lite"    --headless --preset 7  --cpu-max
run_timed 12 "Fantasy (toutes cartes)"   --headless --preset 12 --cpu-max

echo "[pgo_train] terminé."
