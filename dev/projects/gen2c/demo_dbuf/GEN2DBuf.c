/*
 * GEN2DBuf.c — démonstration du DOUBLE BUFFERING (PAGE2) de la carte GEN2
 *              d'Uncle Bernie, en C avec le runtime gen2c.
 *
 *   Démo Double Buffer GEN2 / VERHILLE Arnaud 2026
 *
 * La carte a DEUX framebuffers HIRES : page 1 ($2000) et page 2 ($4000). Au lieu
 * de dessiner sur la page affichée (ce qui montre l'image à moitié tracée — du
 * scintillement / déchirure), on dessine la frame suivante dans la page CACHÉE,
 * puis on bascule l'affichage dessus. Le spectateur ne voit que des images
 * complètes : animation plein écran fluide.
 *
 *   gen2_set_draw_page(p) — choisit où écrivent les primitives (1 ou 2)
 *   gen2_show_page()      — affiche la page de dessin courante ($C254/$C255)
 *
 * Cette démo fait rebondir un bloc 16×16 sur tout l'écran. La boucle :
 *   1. dessine la frame entière (efface + titre + bloc) dans la page cachée,
 *   2. l'affiche d'un coup,
 *   3. alterne la page de dessin pour la frame suivante.
 *
 *   Build : make    -> "software/Graphic HGR/GEN2DBuf.bin" (+ .txt)
 *   Run   : build/POM1 --preset 11 \
 *               --load 6000:"software/Graphic HGR/GEN2DBuf.bin" --run 6000
 *
 * On ré-asserte le mode (graphics/hires/full) à chaque frame : c'est instantané
 * et ça couvre le branchement DIFFÉRÉ de la carte par le DevBench (~15 frames).
 * gen2_show_page() pose le commutateur de page ; pas besoin de gen2_hgr_init en
 * boucle (qui, lui, forcerait l'affichage de la page 1 et casserait le flip).
 */
#include "gen2.h"

#define BALL  16u                 /* bloc 16×16                              */
#define STEP   4u                 /* pixels par frame                        */
#define XMAX  (280u - BALL)       /* position x max (bord droit)             */
#define YMAX  (192u - BALL)       /* position y max (bord bas)               */

void main(void)
{
    unsigned char draw = 2u;      /* on commence par remplir la page cachée  */
    unsigned      bx = 40u;       /* position du bloc (x tient sur 16 bits)  */
    unsigned char by = 30u;
    unsigned char xpos = 1u;      /* sens : 1 = vers +, 0 = vers -           */
    unsigned char ypos = 1u;

    gen2_hgr_init();              /* HIRES, plein écran, affiche page 1      */

    for (;;) {
        /* Mode ré-asserté chaque frame (robuste au plug différé du DevBench). */
        gen2_graphics();
        gen2_hires();
        gen2_full();

        /* 1. Tracer la frame ENTIÈRE dans la page cachée. */
        gen2_set_draw_page(draw);
        gen2_hgr_clear(0u);
        gen2_hgr_puts(8u, 4u, "DOUBLE BUFFER");
        gen2_hgr_fill_pixrect(bx, by, BALL, BALL);

        /* 2. La basculer à l'écran (flip sans déchirure). */
        gen2_show_page();

        /* 3. La frame suivante ira dans l'AUTRE page. */
        draw = (draw == 1u) ? 2u : 1u;

        /* Avancer + rebondir sur les quatre bords. */
        if (xpos) { bx += STEP; if (bx >= XMAX) { bx = XMAX; xpos = 0u; } }
        else      { if (bx < STEP) { bx = 0u; xpos = 1u; } else bx -= STEP; }
        if (ypos) { by += STEP; if (by >= YMAX) { by = (unsigned char)YMAX; ypos = 0u; } }
        else      { if (by < STEP) { by = 0u; ypos = 1u; } else by -= STEP; }
    }
}
