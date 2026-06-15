/*
 * GEN2Countdown.c — un compteur 20 -> 0 pour la carte GEN2 HGR d'Uncle Bernie,
 *                   puis retour au WOZ Monitor.
 *
 *   Compteur GEN2 HGR / VERHILLE Arnaud 2026
 *
 * Affiche un grand chiffre centré sur l'écran HIRES 280x192 de la carte GEN2,
 * en décomptant de 20 à 0 (un chiffre par ~seconde), affiche "LIFTOFF!" à zéro,
 * puis rend la main au moniteur Wozniak (le prompt "\" sur l'écran texte de
 * l'Apple-1).
 *
 *   Build : make            -> "software/Graphic HGR/GEN2Countdown.bin" (+ .txt)
 *   Run   : DevBench -> POM1 Bench -> nouveau sketch C / GEN2 HGR
 *           ou  build/POM1 --preset 12 \
 *                          --load 6000:"software/Graphic HGR/GEN2Countdown.bin" \
 *                          --run 6000
 *
 * Rendu INCREMENTAL (comme GEN2Snake) : le décor statique — le mode HGR, l'écran
 * noir, le titre — n'est tracé QU'UNE FOIS, avant la boucle. Chaque tick efface
 * UNIQUEMENT le rectangle du nombre précédent puis dessine le nouveau ; rien
 * d'autre n'est retouché (pas de gen2_hgr_clear par image, donc pas de
 * scintillement et un coût quasi nul). "LIFTOFF!" est en dehors de cette zone,
 * donc il subsiste une fois affiché.
 */
#include "gen2.h"
#include "apple1io.h"

/* Boucle d'attente grossière (~1 s à ~1 MHz). cc65 NE supprime PAS cette boucle
 * vide — le même procédé sert de cadence dans GEN2Snake. Augmenter pour ralentir
 * le décompte, diminuer pour l'accélérer. */
#define TICK_SPINS 55000u

/* Boîte englobante du grand chiffre centré. Le nombre est dessiné en cellules
 * 16x16 (police Beautiful Boot) à x=123 (2 chiffres) ou x=132 (1 chiffre), donc
 * il occupe au plus x 123..156, y 88..103. On efface 120..161 x 88..103 : assez
 * pour couvrir les deux positions sans jamais toucher le titre (y=16). */
#define NUM_X0  120u
#define NUM_Y0   88u
#define NUM_W    42u
#define NUM_H    16u

static void spin(unsigned int spins)
{
    unsigned int i;
    for (i = 0; i < spins; ++i) { /* busy spin */ }
}

/* Efface UNIQUEMENT la zone du nombre (le seul élément qui change d'un tick à
 * l'autre). Le titre et "LIFTOFF!" sont hors de cette boîte et restent intacts. */
static void erase_number(void)
{
    unsigned x;
    unsigned char y;
    for (y = NUM_Y0; y < NUM_Y0 + NUM_H; ++y) {
        for (x = NUM_X0; x < NUM_X0 + NUM_W; ++x) {
            gen2_hgr_unplot(x, y);
        }
    }
}

void main(void)
{
    unsigned char n;
    unsigned char s;

    /* --- Décor STATIQUE : tracé UNE SEULE FOIS --- */
    gen2_hgr_init();
    gen2_hgr_clear(0);
    gen2_hgr_puts(15, 16, "GEN2 COUNTDOWN");   /* 14 car. -> centré */

    /* Settle : re-asserter le mode le temps que la carte GEN2 finisse son
     * branchement différé (le DevBench branche les cartes ~15 images après le
     * lancement). On NE re-clear/redraw PAS — juste re-asserter les soft-switches
     * (ce qui les journalise et garde l'emulateur sur le chemin de rendu beam,
     * celui qui restitue les mises à jour incrementales — cf. GEN2Snake). */
    for (s = 0; s < 20u; ++s) { gen2_hgr_init(); spin(6000u); }

    /* --- Décompte 20, 19, ... 1, 0 --- n est non signé : on dessine puis on
     * attend AVANT de tester le zéro (sinon --n boucle sous zéro). */
    for (n = 20u; ; --n) {
        gen2_hgr_init();                                /* re-asserte le mode    */
        erase_number();                                 /* efface l'ancien seul  */
        gen2_hgr_putu((n >= 10u) ? 123u : 132u, 88, n); /* dessine le nouveau    */
        if (n == 0u) {
            gen2_hgr_puts(69, 150, "LIFTOFF!");         /* 8 car. -> centré      */
        }
        spin(TICK_SPINS);
        if (n == 0u) break;
    }

    /* Garde le "0" / LIFTOFF affiché un instant, puis rend la main au WOZ
     * Monitor (saut sans retour vers $FF1F — rien ne s'exécute après). */
    spin(TICK_SPINS);
    woz_mon();
}
