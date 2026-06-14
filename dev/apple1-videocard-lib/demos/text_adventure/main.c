/*
 * text_adventure — jeu d'aventure textuelle (CodeTank + TMS9918, preset 7).
 * Inspire de la structure de Little Tower (salles, clef, torche, piece
 * sombre, mot sur un tableau, paralysie, dague, vampire).
 * Build: make -> software/Apple-1_TMS_CC65/text_adventure.{bin,txt}
 * Run:  Wozmon 4000R
 */
#include "tms9918.h"
#include "screen1.h"
#include "apple1.h"

#define MAXLINE  72U
#define MAXW     28U

static unsigned char room;
static unsigned char door_open;
static unsigned char has_key;
static unsigned char has_torch;
static unsigned char has_dagger;
static unsigned char room5_lit;
static unsigned char paralysis;
static unsigned char want_desc;

static void upcase(unsigned char *s) {
    unsigned char c;
    while ((c = *s) != 0) {
        if (c >= 'a' && c <= 'z') {
            *s = (unsigned char)(c - 32U);
        }
        ++s;
    }
}

static void trim_trail(unsigned char *s) {
    unsigned char i = 0;
    while (s[i] != 0) {
        ++i;
    }
    while (i > 0 && (s[i - 1] == ' ' || s[i - 1] == '\t')) {
        s[--i] = 0;
    }
}

static unsigned char ustrcmp(const unsigned char *a, const char *b) {
    unsigned char ca, cb;
    for (;;) {
        ca = *a++;
        cb = (unsigned char)*b++;
        if (cb == 0) {
            return (unsigned char)(ca != 0);
        }
        if (ca != cb) {
            return 1;
        }
    }
}

static void split2(unsigned char *line, unsigned char *w1, unsigned char *w2) {
    unsigned char i;
    while (*line == ' ') {
        ++line;
    }
    i = 0;
    while (*line != 0 && *line != ' ' && i < MAXW - 1U) {
        w1[i++] = *line++;
    }
    w1[i] = 0;
    while (*line == ' ') {
        ++line;
    }
    i = 0;
    while (*line != 0 && *line != ' ' && i < MAXW - 1U) {
        w2[i++] = *line++;
    }
    w2[i] = 0;
}

static void say(const char *s) {
    screen1_puts((const unsigned char *)s);
}

static void prompt(void) {
    say("\n> ");
}

static void print_inv(void) {
    unsigned char n = 0;
    say("\nVotre sac :\n");
    if (has_key != 0) {
        say("- clef rouillee\n");
        n = 1;
    }
    if (has_torch != 0) {
        say("- torche\n");
        n = 1;
    }
    if (has_dagger != 0) {
        say("- dague d'argent\n");
        n = 1;
    }
    if (n == 0) {
        say("(vide)\n");
    }
}

static void print_help(void) {
    say("\nCommandes (anglais) :\n");
    say("1 ou 2 mots max.\n");
    say("\nDirections :\n");
    say("N NORTH  S SOUTH  W WEST\n");
    say("E EAST  UP  DOWN\n");
    say("\nActions :\n");
    say("LOOK  EXAMINE obj\n");
    say("GET TAKE  USE  OPEN\n");
    say("ENTER  SAY mot\n");
    say("KILL ATTACK  INVENTORY I\n");
    say("HELP RESTART  QUIT EXIT\n");
    say("\nObjets :\n");
    say("DOOR BOAT SKELETON TORCH\n");
    say("PICTURE BED DESK BOOK\n");
    say("VAMPIRE MAN\n");
}

static void dont_understand(void) {
    say("\nJe ne comprends pas.\n");
    say("Tapez HELP.\n");
}

static void cant_go(void) {
    say("\nImpossible par la.\n");
}

static void describe_room(void) {
    want_desc = 1;
}

static void room_banner(void) {
    say("\n--- ");
    screen1_putc((unsigned char)('0' + room));
    say(" ---\n");
}

static void show_room(void) {
    want_desc = 0;
    room_banner();
    switch (room) {
    case 1:
        say("Foret sombre, vent dans les\n");
        say("arbres. Tour et porte close.\n");
        say("Sud : lac dans la brume.\n");
        say("Objets : porte.\n");
        say("Sorties : S, ENTER.\n");
        break;
    case 2:
        say("Berge du lac.\n");
        say("Squelette contre un rocher.\n");
        say("Vieille barque sur l'eau.\n");
        say("Objets : squelette, barque.\n");
        say("Sorties : N, S.\n");
        break;
    case 3:
        say("Atelier : planches, etabli.\n");
        say("Torches sur le banc. Porte\n");
        say("a l'ouest, escalier en haut.\n");
        say("Objets : torche.\n");
        say("Sorties : W, UP.\n");
        break;
    case 4:
        say("Chambre dans la tour.\n");
        say("Tableaux au mur, lit defait.\n");
        say("Porte au sud.\n");
        say("Objets : tableau, lit.\n");
        say("Sorties : UP, DOWN, S.\n");
        break;
    case 5:
        if (room5_lit == 0) {
            say("Noir complet.\n");
            say("Lueur vers le nord (porte).\n");
            say("Objets : ?\n");
            say("Sorties : N.\n");
        } else {
            say("Bibliotheque.\n");
            say("Rayons, bureau en bois.\n");
            say("Chaise au milieu de la piece.\n");
            say("Objets : bureau, livres.\n");
            say("Sorties : N.\n");
        }
        break;
    case 6:
        say("Grenier en ruine, toit ouvert\n");
        say("sur le ciel noir.\n");
        say("Silhouette pale, yeux rouges,\n");
        say("fixe sur vous.\n");
        say("Objets : ?\n");
        say("Sorties : DOWN.\n");
        say("\n      /\\\n");
        say("     /  \\\n");
        say("    |o  o|\n");
        say("    | ^^ |\n");
        say("     \\vv/\n");
        break;
    default:
        say("\nErreur de salle.\n");
        break;
    }
}

static void do_quit(void) {
    say("\nAu revoir.\n");
    woz_mon();
}

static void do_restart(void) {
    room = 1;
    door_open = 0;
    has_key = 0;
    has_torch = 0;
    has_dagger = 0;
    room5_lit = 0;
    paralysis = 1;
    want_desc = 1;
    say("\n*** Nouvelle partie ***\n");
}

static void try_directions(const unsigned char *w1, const unsigned char *w2) {
    (void)w2;
    if (ustrcmp(w1, "N") == 0 || ustrcmp(w1, "NORTH") == 0) {
        if (room == 2) {
            room = 1;
            describe_room();
        } else if (room == 5) {
            room = 4;
            describe_room();
        } else {
            cant_go();
        }
        return;
    }
    if (ustrcmp(w1, "S") == 0 || ustrcmp(w1, "SOUTH") == 0) {
        if (room == 1) {
            room = 2;
            describe_room();
        } else if (room == 2) {
            say("\nLa barque coule !\n");
            say("C'est la fin.\n");
            do_quit();
        } else if (room == 4) {
            room = 5;
            describe_room();
        } else {
            cant_go();
        }
        return;
    }
    if (ustrcmp(w1, "W") == 0 || ustrcmp(w1, "WEST") == 0) {
        if (room == 3) {
            room = 1;
            describe_room();
        } else {
            cant_go();
        }
        return;
    }
    if (ustrcmp(w1, "E") == 0 || ustrcmp(w1, "EAST") == 0) {
        cant_go();
        return;
    }
    if (ustrcmp(w1, "UP") == 0) {
        if (room == 3) {
            room = 4;
            describe_room();
        } else if (room == 4) {
            room = 6;
            describe_room();
        } else {
            cant_go();
        }
        return;
    }
    if (ustrcmp(w1, "DOWN") == 0) {
        if (room == 4) {
            room = 3;
            describe_room();
        } else if (room == 6) {
            if (paralysis != 0) {
                say("\nVous ne pouvez pas fuir.\n");
                say("Le vampire boit votre sang.\n");
                say("Fin.\n");
                do_quit();
            }
            room = 4;
            paralysis = 1;
            describe_room();
        } else {
            cant_go();
        }
        return;
    }
    dont_understand();
}

static void analyse(const unsigned char *w1, const unsigned char *w2) {
    if (w1[0] == 0) {
        return;
    }

    if (ustrcmp(w1, "HELP") == 0 || ustrcmp(w1, "?") == 0) {
        print_help();
        return;
    }
    if (ustrcmp(w1, "QUIT") == 0 || ustrcmp(w1, "EXIT") == 0) {
        do_quit();
        return;
    }
    if (ustrcmp(w1, "RESTART") == 0) {
        do_restart();
        return;
    }
    if (ustrcmp(w1, "LOOK") == 0) {
        if (w2[0] != 0) {
            if (room == 4 && (ustrcmp(w2, "PICTURE") == 0 || ustrcmp(w2, "PICTURES") == 0)) {
                say("\nSignes sur le mur. Mot lisible :\n");
                say("KORVAX (leve la paralysie).\n");
            } else {
                dont_understand();
            }
            return;
        }
        describe_room();
        return;
    }
    if (ustrcmp(w1, "INVENTORY") == 0 || ustrcmp(w1, "I") == 0) {
        print_inv();
        return;
    }

    if (ustrcmp(w1, "SAY") == 0) {
        if (room == 6 && ustrcmp(w2, "KORVAX") == 0) {
            say("\nLe sort se brise.\n");
            say("Vos jambes vous obeissent.\n");
            paralysis = 0;
        } else {
            dont_understand();
        }
        return;
    }

    if (ustrcmp(w1, "EXAMINE") == 0) {
        if (w2[0] == 0) {
            dont_understand();
            return;
        }
        if (room == 5 && room5_lit == 0) {
            say("\nTrop noir : rien a distinguer.\n");
            return;
        }
        if (room == 1 && ustrcmp(w2, "DOOR") == 0) {
            if (door_open != 0) {
                say("\nPorte ouverte. Tapez ENTER.\n");
            } else {
                say("\nPorte close, enorme serrure.\n");
            }
            return;
        }
        if (room == 2 && ustrcmp(w2, "BOAT") == 0) {
            say("\nPlanche vermolue : un trou\n");
            say("sous la ligne de flottaison.\n");
            return;
        }
        if (room == 2 && ustrcmp(w2, "SKELETON") == 0) {
            if (has_key == 0) {
                say("\nSous le squelette : une clef.\n");
                say("Elle va dans votre sac.\n");
                has_key = 1;
            } else {
                say("\nVieux ossements.\n");
                say("Plus rien d'interet.\n");
            }
            return;
        }
        if (room == 3 && ustrcmp(w2, "TORCH") == 0) {
            if (has_torch == 0 && room5_lit == 0) {
                say("\nBonne torche pour une piece\n");
                say("sans lumiere.\n");
            } else {
                say("\nIl ne reste que des cendres.\n");
            }
            return;
        }
        if (room == 4 && (ustrcmp(w2, "PICTURE") == 0 || ustrcmp(w2, "PICTURES") == 0)) {
            say("\nSignes sur le mur. Mot lisible :\n");
            say("KORVAX (leve la paralysie).\n");
            return;
        }
        if (room == 4 && ustrcmp(w2, "BED") == 0) {
            say("\nRien sur le lit ni dessous.\n");
            return;
        }
        if (room == 5 && room5_lit != 0) {
            if (ustrcmp(w2, "DESK") == 0) {
                if (has_dagger == 0) {
                    say("\nDague d'argent sur le bureau.\n");
                    say("Vous la ramassez.\n");
                    has_dagger = 1;
                } else {
                    say("\nLe bureau est vide.\n");
                }
                return;
            }
            if (ustrcmp(w2, "BOOK") == 0) {
                say("\nLivres sur les vampires et\n");
                say("comment les detruire.\n");
                return;
            }
        }
        if (room == 6 && (ustrcmp(w2, "VAMPIRE") == 0 || ustrcmp(w2, "MAN") == 0)) {
            say("\nPeau de cire, yeux rouges.\n");
            say("Un froid dans le dos.\n");
            return;
        }
        dont_understand();
        return;
    }

    if (ustrcmp(w1, "GET") == 0 || ustrcmp(w1, "TAKE") == 0) {
        if (room == 3 && ustrcmp(w2, "TORCH") == 0) {
            if (has_torch != 0) {
                say("\nVous l'avez deja.\n");
            } else if (room5_lit != 0) {
                say("\nPlus de torche utile ici.\n");
            } else {
                say("\nTorche au sac.\n");
                has_torch = 1;
            }
            return;
        }
        dont_understand();
        return;
    }

    if (ustrcmp(w1, "USE") == 0) {
        if (room == 1 && (ustrcmp(w2, "KEY") == 0)) {
            if (door_open != 0) {
                say("\nLa porte est deja ouverte.\n");
            } else if (has_key == 0) {
                say("\nPas de clef !\n");
            } else {
                say("\nLa serrure cede.\n");
                say("La porte s'ouvre.\n");
                door_open = 1;
            }
            return;
        }
        if (room == 5 && ustrcmp(w2, "TORCH") == 0) {
            if (room5_lit != 0) {
                say("\nLa piece est deja eclairee.\n");
            } else if (has_torch == 0) {
                say("\nSans torche, impossible.\n");
            } else {
                say("\nFlamme stable : la piece\n");
                say("s'eclaire. Torche epuisee.\n");
                room5_lit = 1;
                has_torch = 0;
                describe_room();
            }
            return;
        }
        if (room == 6 && ustrcmp(w2, "DAGGER") == 0) {
            if (has_dagger == 0) {
                say("\nPas de dague !\n");
            } else if (paralysis != 0) {
                say("\nVous ne pouvez pas bouger.\n");
            } else {
                say("\nLa creature fond en poussiere\n");
                say("sous la lame d'argent !\n");
                say("\n*** BRAVO - VOUS GAGNEZ ***\n");
                do_quit();
            }
            return;
        }
        dont_understand();
        return;
    }

    if (ustrcmp(w1, "OPEN") == 0 && ustrcmp(w2, "DOOR") == 0) {
        if (room != 1) {
            dont_understand();
            return;
        }
        if (door_open != 0) {
            say("\nDeja ouverte. Tapez ENTER.\n");
        } else if (has_key == 0) {
            say("\nPas de clef !\n");
        } else {
            say("\nClef dans la serrure.\n");
            say("La porte grince et s'ouvre.\n");
            door_open = 1;
        }
        return;
    }

    if (ustrcmp(w1, "ENTER") == 0) {
        if (room != 1) {
            dont_understand();
            return;
        }
        if (door_open == 0) {
            say("\nLa porte est close.\n");
        } else {
            room = 3;
            describe_room();
        }
        return;
    }

    if (ustrcmp(w1, "KILL") == 0 || ustrcmp(w1, "ATTACK") == 0) {
        if (room == 6) {
            if (has_dagger == 0) {
                say("\nIl vous faut une dague.\n");
            } else if (paralysis != 0) {
                say("\nVous ne pouvez pas bouger.\n");
            } else {
                say("\nLa creature fond en poussiere\n");
                say("sous la lame d'argent !\n");
                say("\n*** BRAVO - VOUS GAGNEZ ***\n");
                do_quit();
            }
        } else {
            dont_understand();
        }
        return;
    }

    /* directions (one word) */
    if (w2[0] == 0) {
        try_directions(w1, w2);
        return;
    }

    dont_understand();
}

void main(void) {
    static unsigned char line[MAXLINE];
    static unsigned char w1[MAXW];
    static unsigned char w2[MAXW];

    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_LIGHT_YELLOW);
    screen1_prepare();
    screen1_load_font();

    say(CLS);
    say("\nPETITE TOUR\n");
    say("Apple-1 / Little Tower\n");
    say("(cc65 videocard-lib)\n\n");
    say("1 PLAY   2 HELP\n");
    for (;;) {
        unsigned char k = apple1_getkey();
        if (k == '1') {
            screen1_putc('1');
            break;
        }
        if (k == '2') {
            screen1_putc('2');
            print_help();
            say("\nTapez 1 pour commencer.\n");
        }
    }

    do_restart();
    say("\nTapez HELP pour les commandes.\n");
    say("Bonne chance !\n");

    for (;;) {
        if (want_desc != 0) {
            show_room();
        }
        prompt();
        screen1_strinput(line, MAXLINE - 1U);
        say("\n");
        upcase(line);
        trim_trail(line);
        split2(line, w1, w2);
        analyse(w1, w2);
    }
}
