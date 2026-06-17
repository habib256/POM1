/*
 * GEN2Lores.c — démonstration du mode LORES (40×48, 16 couleurs) de la carte
 *               GEN2 d'Uncle Bernie, écrite en C avec le runtime gen2c.
 *
 *   Démo LORES GEN2 / VERHILLE Arnaud 2026
 *
 * Contrairement au HIRES (couleur = artefact NTSC du motif de bits), le mode
 * LORES a une VRAIE couleur par bloc : une grille 40×48 de blocs 7px×4px, 16
 * couleurs. Cette démo :
 *   1. affiche les 16 couleurs de la palette en barres verticales,
 *   2. trace un cadre blanc autour de l'écran,
 *   3. dessine une diagonale arc-en-ciel,
 * puis garde l'image affichée.
 *
 *   Build : make    -> "software/Graphic HGR/GEN2Lores.bin" (+ .txt)
 *   Run   : build/POM1 --preset 8 \
 *               --load 6000:"software/Graphic HGR/GEN2Lores.bin" --run 6000
 *           ou : DevBench -> POM1 Bench -> nouveau sketch C / GEN2 HGR,
 *                coller ce fichier (le runtime gen2c gère LORES et HIRES).
 *
 * On DESSINE D'ABORD : le dessin écrit la page texte $0400 (RAM), qui PERSISTE.
 * On boucle ensuite en ré-assertant le mode LORES — c'est instantané et ça gère
 * le branchement DIFFÉRÉ de la carte par le DevBench (~15 images) : dès que la
 * carte est là, le mode prend et l'image déjà dessinée apparaît. Pas de longue
 * boucle d'attente AVANT le tracé (sinon l'écran reste noir trop longtemps).
 */
#include "gen2.h"

void main(void)
{
    unsigned char x, y, c;

    /* --- Mode + dessin, tout de suite (le tracé persiste en RAM $0400). --- */
    gen2_lores_init();
    gen2_lores_clear(GEN2_LO_BLACK);

    /* 1. Les 16 couleurs en barres verticales pleine hauteur, 2 colonnes
     *    chacune (16*2 = 32 colonnes, centrées : départ colonne 4). */
    for (c = 0; c < 16u; ++c) {
        x = (unsigned char)(4u + c * 2u);
        gen2_lores_vlin(x,                       0u, 47u, c);
        gen2_lores_vlin((unsigned char)(x + 1u), 0u, 47u, c);
    }

    /* 2. Cadre blanc tout autour de l'écran (HLIN/VLIN, bornes incluses). */
    gen2_lores_hlin(0u, 39u, 0u,  GEN2_LO_WHITE);
    gen2_lores_hlin(0u, 39u, 47u, GEN2_LO_WHITE);
    gen2_lores_vlin(0u,  0u, 47u, GEN2_LO_WHITE);
    gen2_lores_vlin(39u, 0u, 47u, GEN2_LO_WHITE);

    /* 3. Diagonale arc-en-ciel par blocs individuels (setblock). */
    for (y = 2u; y < 46u; ++y) {
        x = (unsigned char)(2u + y);            /* x = 4..47 -> clip à 39 */
        gen2_lores_setblock(x, y, (unsigned char)(y & 0x0Fu));
    }

    /* Garde l'image : on ré-asserte le mode LORES en boucle. C'est instantané
     * (4 lectures de soft-switch) et ça couvre le plug différé du DevBench —
     * l'image, elle, est déjà tracée. */
    for (;;) { gen2_lores_init(); }
}
