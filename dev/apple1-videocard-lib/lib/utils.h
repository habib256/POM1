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

#define TMS_IO_DELAY() ((void)(*(volatile unsigned char *)0xCC01))

#endif
