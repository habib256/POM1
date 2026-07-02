/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
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

/* Inter-access pacing delay between back-to-back VDP accesses.
 *
 * Historically this READ THE STATUS PORT ($CC01) as a spacer. On real TMS9918
 * silicon (and in POM1 since juillet 2026) a status read latch-clears the
 * F (frame), 5S (5th sprite) AND C (collision) sticky flags — TI datasheet
 * §2.2 — so pacing-by-status-read silently destroyed every flag the program
 * might poll later (tms_wait_end_of_frame, COLLISION_BIT, FIVESPR_BIT).
 * Re-implemented as a status-NEUTRAL volatile read-modify-write of a RAM
 * sink: comparable cycle cost (LDA abs / ADC / STA abs + cc65 overhead),
 * zero VDP side effects. tms_clear_collisions() keeps its deliberate status
 * read. The volatile sink also defeats cc65 -Oirs read-elision (same trap
 * fixed in gen2c via gen2_ss_sink). */
extern volatile unsigned char tms_io_sink;
#define TMS_IO_DELAY() (tms_io_sink = (unsigned char)(tms_io_sink + 1U))

#endif
