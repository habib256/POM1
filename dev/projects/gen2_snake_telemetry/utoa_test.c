/* utoa_test.c — verify gen2_hgr_putu (asm bin->dec) renders the SAME pixels as
 * the literal decimal string drawn by gen2_hgr_puts. For each value, putu is
 * drawn on row R and the reference string on row R+24; a correct utoa makes the
 * two rows byte-identical. (Throwaway harness — not shipped.) */
#include "gen2.h"

void main(void)
{
    gen2_hgr_init();
    gen2_hgr_clear(0);

    gen2_hgr_putu(10,   0, 0);        gen2_hgr_puts(10,  24, "0");
    gen2_hgr_putu(10,  48, 7);        gen2_hgr_puts(10,  72, "7");
    gen2_hgr_putu(10,  96, 12345);    gen2_hgr_puts(10, 120, "12345");
    gen2_hgr_putu(10, 144, 60900);    gen2_hgr_puts(10, 168, "60900");

    for (;;) { }
}
