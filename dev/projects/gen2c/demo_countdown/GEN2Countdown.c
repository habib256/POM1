/*
 * GEN2Countdown.c — un compteur 20 -> 0 pour la carte GEN2 HGR d'Uncle Bernie,
 *                   puis retour au WOZ Monitor.
 *
 *   Compteur GEN2 HGR / VERHILLE Arnaud 2026
 *
 * Affiche un grand chiffre centré sur l'écran HIRES 280x192 de la carte GEN2,
 * en décomptant de 20 à 0 (un chiffre par ~seconde), puis rend la main au
 * moniteur Wozniak (le prompt "\" sur l'écran texte de l'Apple-1).
 *
 *   Build : make            -> "software/Graphic HGR/GEN2Countdown.bin" (+ .txt)
 *   Run   : DevBench -> POM1 Bench -> nouveau sketch C / GEN2 HGR
 *           ou  build/POM1 --preset 12 \
 *                          --load 6000:"software/Graphic HGR/GEN2Countdown.bin" \
 *                          --run 6000
 *
 * Rendu INCREMENTAL : le décor statique (le titre) n'est tracé QU'UNE FOIS,
 * avant la boucle. Chaque tick efface UNIQUEMENT la bande du chiffre puis y
 * dessine le nouveau ; le titre n'est jamais retouché.
 *
 * ATTENTION (leçon apprise) : l'effacement passe par gen2_hgr_fill_rect — un
 * remplissage d'OCTETS entiers du framebuffer écrit en ASSEMBLEUR (gen2_blit.s),
 * PAS pixel par pixel. gen2_hgr_unplot fait une division logicielle (x/7, x%7)
 * par pixel — ~700 appels par effacement, lent et inutile ici. Le chiffre tient
 * dans des colonnes-octets connues, donc zéro-er ces octets est plus rapide ET
 * sans ambiguïté. De plus, AUCUN texte statique ne doit toucher la bande du
 * chiffre (sinon l'effacement en mangerait une partie -> "fantômes").
 */
#include "gen2.h"
#include "apple1io.h"

/* Boucle d'attente grossière (~1 s à ~1 MHz). cc65 NE supprime PAS cette boucle
 * vide. Surchargée par le harnais de test via -D ; valeur d'expédition ~1 s. */
#ifndef TICK_SPINS
#define TICK_SPINS 45000u
#endif
/* Nombre de re-assertions du mode pendant le settle (plug différé du DevBench). */
#ifndef SETTLE_ITERS
#define SETTLE_ITERS 12u
#endif
#ifndef SETTLE_SPINS
#define SETTLE_SPINS 4000u
#endif

/* --- Bande du chiffre (en COLONNES-OCTETS et scanlines HIRES) ---------------
 * Le chiffre est dessiné en cellules 16x16 (police Beautiful Boot) à x=123 (2
 * chiffres) ou x=132 (1 chiffre), donc il occupe au plus x 123..156 sur les
 * scanlines y 88..103. En colonnes-octets (7 px/octet) : x123..156 -> octets
 * 17..22. On efface 16..23 (x112..167) pour une marge sûre, sans jamais
 * atteindre le titre (y=16..31). */
#define BAND_Y0   88u
#define BAND_Y1  104u    /* exclusif : scanlines 88..103                       */
#define BAND_C0   16u
#define BAND_C1   24u     /* exclusif : octets 16..23                          */

static void spin(unsigned int spins)
{
    unsigned int i;
    for (i = 0; i < spins; ++i) { /* busy spin */ }
}

/* Efface UNIQUEMENT la bande du chiffre via le remplisseur assembleur de la lib
 * (octets entiers, pas de division par pixel). Le titre est hors de cette bande
 * et reste intact. */
static void clear_band(void)
{
    gen2_hgr_fill_rect(BAND_Y0, BAND_Y1 - BAND_Y0, BAND_C0, BAND_C1 - BAND_C0, 0);
}

void main(void)
{
    unsigned char n;
    unsigned char s;

    /* --- Décor STATIQUE : tracé UNE SEULE FOIS. Le titre est à y=16, loin de
     * la bande du chiffre (y=88..103). --- */
    gen2_hgr_init();
    gen2_hgr_clear(0);
    gen2_hgr_puts(15, 16, "GEN2 COUNTDOWN");   /* 14 car., centré, hors bande */

    /* Settle : re-asserter le mode le temps que la carte GEN2 finisse son
     * branchement différé (le DevBench branche les cartes ~15 images après le
     * lancement). On NE redessine PAS — juste re-asserter les soft-switches. */
    for (s = 0; s < SETTLE_ITERS; ++s) { gen2_hgr_init(); spin(SETTLE_SPINS); }

    /* --- Décompte 20, 19, ... 1, 0 --- n est non signé : on dessine puis on
     * attend AVANT de tester le zéro (sinon --n boucle sous zéro). */
    for (n = 20u; ; --n) {
        clear_band();                                   /* efface l'ancien seul  */
        gen2_hgr_putu((n >= 10u) ? 123u : 132u, 88, n); /* dessine le nouveau    */
        spin(TICK_SPINS);
        if (n == 0u) break;
    }

    /* Garde le "0" affiché un instant, puis rend la main au WOZ Monitor
     * (saut sans retour vers $FF1F — rien ne s'exécute après). */
    spin(TICK_SPINS);
    woz_mon();
}
