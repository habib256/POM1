/* hello.c — the smallest useful Apple-1 program in C. Prints a string through
 * the WOZ Monitor, then returns to it.
 *
 * COPY THIS FOLDER to start your own C project. Build by hand (the Makefile
 * builds the asm Hello.asm; for C use the command in README.md), then load the
 * .bin in POM1 and run with 0300R.
 *
 * Notes (see dev/Programming_Apple1_C.md):
 *   - void main(void), not int main() — there's no OS to return to.
 *   - No <stdio.h>/printf — print with woz_puts / woz_putc.
 *   - apple1io.h is the shared, card-neutral text/keyboard base; the same header
 *     works on the GEN2 HGR card too.
 */
#include "apple1io.h"          /* needs -I ../../lib/apple1c */

void main(void) {
    woz_puts((const unsigned char *)"\rHELLO WORLD (C)\r");
    woz_mon();                 /* back to the '\' prompt */
}
