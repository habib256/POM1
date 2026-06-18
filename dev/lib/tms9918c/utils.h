/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef UTILS_H
#define UTILS_H

typedef unsigned char byte;
typedef unsigned int word;
typedef unsigned long dword;

#define POKE(a, b) (*((volatile unsigned char *)(unsigned)(a)) = (unsigned char)(b))
#define PEEK(a)      (*((volatile unsigned char *)(unsigned)(a)))

#define HIBYTE(c) ((unsigned char)(((unsigned)(c)) >> 8))
#define LOBYTE(c) ((unsigned char)(((unsigned)(c)) & 0xFF))

/* Inter-access pacing read of the VDP status port ($CC01). On real TMS9918
 * silicon this gives the VDP time to finish its VRAM cycle between back-to-back
 * accesses; POM1 tolerates burst writes so it is functionally optional here.
 * The read is stored into tms_io_sink rather than cast to void: cc65 -Oirs
 * ELIDES a volatile read whose value is discarded by (void), which silently
 * turned this macro into a no-op (same trap fixed in gen2c via gen2_ss_sink). */
extern volatile unsigned char tms_io_sink;
#define TMS_IO_DELAY() (tms_io_sink = *(volatile unsigned char *)0xCC01)

#endif
