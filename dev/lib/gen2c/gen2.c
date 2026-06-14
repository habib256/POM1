/* gen2.c — minimal C runtime for Uncle Bernie's GEN2 HGR. See gen2.h. */
#include "gen2.h"

void gen2_hgr_init(void)
{
    gen2_graphics();   /* TEXT off  */
    gen2_hires();      /* RES=HIRES */
    gen2_page1();      /* page 1    */
    gen2_full();       /* no MIXED  */
}

void gen2_hgr_clear(unsigned char fill)
{
    unsigned char *p = GEN2_HGR1;
    unsigned i = 0x2000;
    do { *p++ = fill; } while (--i);
}

void gen2_hgr_plot(unsigned x, unsigned char y)
{
    unsigned char *p;
    if (x > 279 || y > 191) return;
    /* Apple II interleaved HIRES scanline base:
       $2000 + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*40 */
    p = (unsigned char *)(0x2000u
        + ((unsigned)(y & 7) << 10)
        + ((unsigned)((y >> 3) & 7) << 7)
        + (unsigned)(y >> 6) * 40u);
    p[x / 7u] |= (unsigned char)(1u << (x % 7u));   /* bit 7 left 0 = no colour shift */
}

/* Double-sampled HST0 (the burst notch can zero one of two close reads). */
static unsigned char gen2_blank(void)
{
    return (unsigned char)((GEN2_SS[7] | GEN2_SS[7]) & GEN2_HST0);
}

void gen2_wait_vbl(void)
{
    unsigned n;
    while (gen2_blank()) { }            /* wait for the live raster */
    for (;;) {
        while (!gen2_blank()) { }        /* wait for a blanking edge */
        for (n = 0; n < 400u && gen2_blank(); ++n) { }
        if (n >= 400u) return;          /* sustained blank => V-blank */
        /* short blank => it was just an H-blank; find the next edge */
    }
}
