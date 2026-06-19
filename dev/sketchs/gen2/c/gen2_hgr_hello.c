/* HELLO WORLD on Uncle Bernie's GEN2 HIRES.
   DevBench target: Uncle Bernie GEN2 HGR (C), entry $6000. */

#include "gen2.h"

void main(void) {
    gen2_hgr_init();
    gen2_hgr_clear(0);
    gen2_hgr_puts(42, 80, "HELLO WORLD");

    for (;;) {
        /* idle */
    }
}

