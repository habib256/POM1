/*
 * GEN2Vectors.c — vitrine vectoriel + nombres HUD + sprite XOR de gen2c, en
 *                 double buffering avec rendu INCRÉMENTAL (rapide).
 *
 *   Démo Vecteurs/HUD GEN2 / VERHILLE Arnaud 2026
 *
 * Une balle (sprite disque) rebondit dans un cadre (contour de rectangle) ; un
 * compteur de rebonds s'affiche en HUD largeur fixe, sans scintillement.
 * Réunit quatre briques :
 *   E  gen2_hgr_rect / gen2_hgr_line                     (décor vectoriel)
 *   B  gen2_hgr_blit(..., GEN2_XOR)                      (sprite XOR)
 *   D  gen2_hgr_putu_field                               (nombre HUD stable)
 *   C  gen2_set_draw_page / gen2_show_page               (double buffering)
 *
 * CLEF DE LA VITESSE — on ne redessine PAS ce qui ne bouge pas :
 *   1. Le décor (cadre, séparateur, label) est tracé UNE FOIS dans chaque page.
 *      Plus aucun gen2_hgr_clear() de 8 Ko par frame.
 *   2. La balle est effacée par XOR : on la re-blit au même endroit en mode XOR,
 *      ce qui RESTAURE le fond pixel-parfait — pas de boîte d'effacement à laver,
 *      et la balle peut frôler le décor sans l'abîmer. blit XOR pour dessiner,
 *      blit XOR pour effacer (cf. gen2.h GEN2_XOR).
 *
 * Subtilités double-buffer :
 *   - chaque page n'est dessinée qu'une frame sur deux -> on garde l'ancienne
 *     position PAR PAGE (px[2]/py[2]) et on efface ce que CETTE page avait tracé ;
 *   - on dessine la balle dans LES DEUX pages au démarrage, sinon le tout premier
 *     XOR-erase (sur du noir) CRÉERAIT une balle fantôme au lieu d'en effacer une.
 *
 *   Build : make    -> "software/Graphic HGR/GEN2Vectors.bin" (+ .txt)
 *   Run   : build/POM1 --preset 12 \
 *               --load 6000:"software/Graphic HGR/GEN2Vectors.bin" --run 6000
 */
#include "gen2.h"

/* Balle : disque plein 16x16, MSB-first, 2 octets par ligne. */
static const unsigned char kBall[32] = {
    0x01,0x80, 0x0F,0xF0, 0x1F,0xF8, 0x3F,0xFC,
    0x7F,0xFE, 0x7F,0xFE, 0x7F,0xFE, 0xFF,0xFF,
    0xFF,0xFF, 0x7F,0xFE, 0x7F,0xFE, 0x7F,0xFE,
    0x3F,0xFC, 0x1F,0xF8, 0x0F,0xF0, 0x01,0x80,
};
#define BW   16u                      /* côté du sprite                       */

#define FL    4u                      /* cadre : gauche/haut/droite/bas       */
#define FT    4u
#define FR  275u
#define FB  150u
#define STEP  3u
/* bornes du coin haut-gauche : la balle reste >=2px DANS le cadre, donc le
 * sprite XOR est toujours sur du noir (apparition/effacement nets, le trait du
 * cadre n'est jamais inversé).                                                */
#define BXMIN (FL + 2u)
#define BXMAX (FR - BW - 2u)
#define BYMIN (FT + 2u)
#define BYMAX (FB - BW - 2u)
#define HUDX 150u
#define HUDY 162u
#define HUDW   5u

/* Le décor qui ne change jamais — tracé une fois par page. */
static void draw_static(void)
{
    gen2_hgr_rect(FL, FT, FR, FB);             /* cadre (contour)             */
    gen2_hgr_line(FL, 156u, FR, 156u);         /* séparateur play / HUD       */
    gen2_hgr_puts(FL, HUDY, "BOUNCES");        /* label fixe                  */
}

void main(void)
{
    unsigned char page = 1u, xp = 1u, yp = 1u, pidx, huddirty = 2u;
    unsigned      bx = 70u, by = 40u;          /* coin haut-gauche du sprite  */
    unsigned      px[2];                       /* ancien coin PAR page        */
    unsigned char py[2];
    unsigned      bounces = 0u;

    gen2_hgr_init();

    /* Décor + balle initiale dans LES DEUX pages (une seule fois). */
    gen2_set_draw_page(1u); gen2_hgr_clear(0u); draw_static();
    gen2_hgr_blit(bx, (unsigned char)by, BW, BW, kBall, GEN2_XOR);
    gen2_set_draw_page(2u); gen2_hgr_clear(0u); draw_static();
    gen2_hgr_blit(bx, (unsigned char)by, BW, BW, kBall, GEN2_XOR);
    px[0] = px[1] = bx; py[0] = py[1] = (unsigned char)by;

    for (;;) {
        gen2_graphics(); gen2_hires(); gen2_full();    /* mode (plug différé) */

        pidx = (unsigned char)(page - 1u);
        gen2_set_draw_page(page);

        /* Effacer l'ancienne balle de CETTE page par XOR (fond restauré), puis
         * la re-blitter par XOR à la nouvelle place. */
        gen2_hgr_blit(px[pidx], py[pidx], BW, BW, kBall, GEN2_XOR);
        gen2_hgr_blit(bx, (unsigned char)by, BW, BW, kBall, GEN2_XOR);
        px[pidx] = bx; py[pidx] = (unsigned char)by;

        /* Compteur : réécrit seulement quand il change (2 frames -> 2 pages). */
        if (huddirty) { gen2_hgr_putu_field(HUDX, HUDY, bounces, HUDW); --huddirty; }

        gen2_show_page();
        page = (page == 1u) ? 2u : 1u;

        /* Avancer + rebondir ; un rebond salit le HUD pour les deux pages. */
        if (xp) { bx += STEP; if (bx >= BXMAX) { bx = BXMAX; xp = 0u; ++bounces; huddirty = 2u; } }
        else    { if (bx <= BXMIN) { bx = BXMIN; xp = 1u; ++bounces; huddirty = 2u; } else bx -= STEP; }
        if (yp) { by += STEP; if (by >= BYMAX) { by = BYMAX; yp = 0u; ++bounces; huddirty = 2u; } }
        else    { if (by <= BYMIN) { by = BYMIN; yp = 1u; ++bounces; huddirty = 2u; } else by -= STEP; }
    }
}
