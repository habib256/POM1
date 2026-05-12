/*
 * chrome_dino — mini clone du T-Rex hors-ligne (saut + obstacles).
 * Preset POM1 8 (TMS9918 + CodeTank), Wozmon 4000R.
 */
#include "tms9918.h"
#include "screen1.h"
#include "sprites.h"
#include "apple1.h"
#include "dino_chrome_tms.h"

#define GROUND_Y       158
#define DINO_X_HOME    48U
#define DINO_X_MAX     110U
/* Obstacles (px / frame). Saut : impulsion + gravité +1 / frame ; avance H en l'air. */
#define SCROLL_STEP          1U
#define JUMP_IMPULSE         (-12)
#define JUMP_FORWARD         1U
/* Au sol : attente avant de reculer vers DINO_X_HOME, puis 1 px / frame. */
#define RETURN_DELAY_FRAMES  8U
#define RETURN_STEP          1U
/* Fond texte TMS = blanc (screen1) : éviter blanc/gris clair sur le sprite. */
#define DINO_COLOR     COLOR_DARK_RED

static void upload_sprite_patterns(void) {
    /* En mode 16x16, le HW lit la VRAM SPGT a name*8 (les deux bits bas du name
     * sont masques). PAT_CHROME_* sont deja des noms (multiples de 4), donc
     * l'offset octet est name*8, pas name*32. Symptome du bug initial : sprites
     * dessines comme un cadre vide ou avec des traits aleatoires. */
    tms_copy_to_vram(pat_chrome_run0,   32U, TMS_SPRITE_PATTERNS + (unsigned)PAT_CHROME_RUN0   * 8U);
    tms_copy_to_vram(pat_chrome_run1,   32U, TMS_SPRITE_PATTERNS + (unsigned)PAT_CHROME_RUN1   * 8U);
    tms_copy_to_vram(pat_chrome_jump,   32U, TMS_SPRITE_PATTERNS + (unsigned)PAT_CHROME_JUMP   * 8U);
    tms_copy_to_vram(pat_chrome_cactus, 32U, TMS_SPRITE_PATTERNS + (unsigned)PAT_CHROME_CACTUS * 8U);
}

static void draw_ground_row(void) {
    unsigned char x;
    screen1_locate(0U, 22U);
    for (x = 0U; x < 32U; ++x) {
        screen1_putc((unsigned char)'=');
    }
    screen1_locate(0U, 23U);
    for (x = 0U; x < 32U; ++x) {
        screen1_putc((unsigned char)'=');
    }
}

static unsigned char rnd(void) {
    static unsigned char s = 0xA7U;
    s = (unsigned char)(s * 5U + 37U);
    return s;
}

static unsigned char hit_test(unsigned char ox, unsigned char dino_x, int dino_py) {
    /* Boîtes 16x16 : dino (dino_x .. dino_x+15), obstacle (ox .. ox+15). */
    int dx0 = (int)dino_x;
    int dx1 = dx0 + 14;
    int ox0 = (int)ox;
    int ox1 = ox0 + 14;
    if (ox1 < dx0 || ox0 > dx1) {
        return 0U;
    }
    /* Lignes affichées : sprite commence à Y+1 (TMS). */
    {
        int d_top = dino_py + 1;
        int d_bot = dino_py + 16;
        int o_top = GROUND_Y + 1;
        int o_bot = GROUND_Y + 16;
        if (d_bot < o_top || d_top > o_bot) {
            return 0U;
        }
    }
    return 1U;
}

void main(void) {
    tms_sprite s0, s1, s2;

    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(FG_BG(COLOR_LIGHT_BLUE, COLOR_DARK_BLUE));
    screen1_prepare();
    screen1_load_font();
    upload_sprite_patterns();

    tms_set_sprite_double_size(1U);
    tms_set_sprite_magnification(0U);
    tms_clear_collisions();

    screen1_cls();
    screen1_puts((const unsigned char *)"T-REX (CHROME-LIKE)\r");
    screen1_puts((const unsigned char *)"ESPACE = SAUT\r\r");
    draw_ground_row();

    for (;;) {
        unsigned char obs0_x = 200U;
        unsigned char obs1_x = 255U;
        unsigned char gap0 = 0U;
        unsigned char gap1 = 0U;
        unsigned char alive = 1U;
        unsigned int score = 0U;
        unsigned int anim = 0U;
        int dino_py = GROUND_Y;
        int dino_vy = 0;
        unsigned char dino_x = DINO_X_HOME;
        unsigned char land_hold = 0U;
        unsigned char sp;

        screen1_locate(0U, 3U);
        screen1_puts((const unsigned char *)"PRET...           ");
        screen1_locate(0U, 4U);
        screen1_puts((const unsigned char *)"ESPACE = DEMARRER ");
        for (;;) {
            tms_wait_end_of_frame();
            if (apple1_iskeypressed() && apple1_readkey() == (unsigned char)' ') {
                break;
            }
        }
        screen1_locate(0U, 4U);
        screen1_puts((const unsigned char *)"                  ");

        while (alive) {
            unsigned char dino_pat;

            tms_wait_end_of_frame();
            ++anim;

            if (apple1_iskeypressed()) {
                sp = apple1_readkey();
                if (sp == (unsigned char)' ' && dino_py >= GROUND_Y && dino_vy == 0) {
                    dino_vy = JUMP_IMPULSE;
                    land_hold = 0U;
                }
            }

            dino_vy += 1;
            dino_py += dino_vy;
            if (dino_py > GROUND_Y) {
                dino_py = GROUND_Y;
                dino_vy = 0;
            }

            /* En l'air : léger déplacement avant. Au sol : pause puis retour doux vers la base. */
            if (dino_py < GROUND_Y) {
                land_hold = 0U;
                if (dino_x <= (unsigned char)(DINO_X_MAX - JUMP_FORWARD)) {
                    dino_x = (unsigned char)(dino_x + JUMP_FORWARD);
                } else if (dino_x < DINO_X_MAX) {
                    dino_x = DINO_X_MAX;
                }
            } else if (dino_vy == 0) {
                if (dino_x > DINO_X_HOME) {
                    if (land_hold < RETURN_DELAY_FRAMES) {
                        ++land_hold;
                    } else {
                        if (dino_x > (unsigned char)(DINO_X_HOME + RETURN_STEP)) {
                            dino_x = (unsigned char)(dino_x - RETURN_STEP);
                        } else {
                            dino_x = DINO_X_HOME;
                        }
                    }
                } else {
                    land_hold = 0U;
                }
            }

            if (gap0 > 0U) {
                --gap0;
            } else {
                if (obs0_x < SCROLL_STEP) {
                    obs0_x = 255U;
                    gap0 = (unsigned char)(60U + (rnd() & 63U));
                } else {
                    obs0_x = (unsigned char)(obs0_x - SCROLL_STEP);
                }
            }

            if (gap1 > 0U) {
                --gap1;
            } else {
                if (obs1_x < SCROLL_STEP) {
                    obs1_x = 255U;
                    gap1 = (unsigned char)(80U + (rnd() & 47U));
                } else {
                    obs1_x = (unsigned char)(obs1_x - SCROLL_STEP);
                }
            }

            if (dino_py < GROUND_Y) {
                dino_pat = (unsigned char)PAT_CHROME_JUMP;
            } else {
                dino_pat = ((anim >> 2) & 1U) != 0U ? (unsigned char)PAT_CHROME_RUN1
                                                  : (unsigned char)PAT_CHROME_RUN0;
            }

            s0.y = (signed char)dino_py;
            s0.x = dino_x;
            s0.name = dino_pat;
            s0.color = (unsigned char)(DINO_COLOR & 0x0FU);

            s1.y = (signed char)GROUND_Y;
            s1.x = obs0_x;
            s1.name = (unsigned char)PAT_CHROME_CACTUS;
            s1.color = (unsigned char)(COLOR_DARK_GREEN & 0x0FU);

            s2.y = (signed char)GROUND_Y;
            s2.x = obs1_x;
            s2.name = (unsigned char)PAT_CHROME_CACTUS;
            s2.color = (unsigned char)(COLOR_MEDIUM_GREEN & 0x0FU);

            tms_set_sprite(0U, &s0);
            tms_set_sprite(1U, &s1);
            tms_set_sprite(2U, &s2);
            tms_set_total_sprites(3U);

            if (hit_test(obs0_x, dino_x, dino_py) || hit_test(obs1_x, dino_x, dino_py)) {
                alive = 0U;
            }

            ++score;
            if ((score & 31U) == 0U) {
                unsigned int pts = score / 32U;
                unsigned char buf[6];
                unsigned char i = 0U;
                unsigned char j;
                unsigned int t;
                if (pts > 999U) {
                    pts = 999U;
                }
                t = pts;
                buf[i++] = (unsigned char)('0' + (unsigned char)(t / 100U));
                t %= 100U;
                buf[i++] = (unsigned char)('0' + (unsigned char)(t / 10U));
                buf[i++] = (unsigned char)('0' + (unsigned char)(t % 10U));
                buf[i] = 0U;
                /* Pas de centaine non significative : décale si < 100. */
                j = 0U;
                if (buf[0] == (unsigned char)'0') {
                    j = 1U;
                    if (buf[1] == (unsigned char)'0') {
                        j = 2U;
                    }
                }
                screen1_locate(18U, 3U);
                screen1_puts((const unsigned char *)"PTS ");
                screen1_puts(buf + j);
            }
        }

        screen1_locate(0U, 5U);
        screen1_puts((const unsigned char *)"GAME OVER  ESPACE");

        for (;;) {
            tms_wait_end_of_frame();
            if (apple1_iskeypressed()) {
                sp = apple1_readkey();
                if (sp == (unsigned char)' ') {
                    break;
                }
            }
        }

        screen1_cls();
        screen1_puts((const unsigned char *)"T-REX\rESPACE=SAUT\r\r");
        draw_ground_row();
    }
}
