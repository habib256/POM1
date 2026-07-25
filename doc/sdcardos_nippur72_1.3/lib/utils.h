#ifndef UTILS_H
#define UTILS_H

#define POKE(a,b) (*((byte *)(a))=(byte)(b))
#define PEEK(a)   (*((byte *)(a)))

#define POKEW(a,b) (*((word *)(a))=(word)(b))
#define PEEKW(a)   (*((word *)(a)))

#define HIBYTE(c) (BYTE1(c))
#define LOBYTE(c) (BYTE0(c))
#define EXPORT __export

#define NOP asm { nop }

#ifndef __KICKC__
// KickC defaults these, while other C compilers do not
// this makes syntax highlight work in common C editors
typedef unsigned char byte;
typedef unsigned int  word;
#endif

#endif
