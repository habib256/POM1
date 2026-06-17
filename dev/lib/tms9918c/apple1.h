/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef APPLE1_H
#define APPLE1_H

#include "utils.h"

#define WOZMON    0xFF1FU
#define ECHO      0xFFEFU
#define PRBYTE    0xFFDCU
#define KEY_DATA  0xD010U
#define KEY_CTRL  0xD011U
#define TERM_DATA 0xD012U
#define TERM_CTRL 0xD013U
#define INBUFFER  0x0200U
#define INBUFSIZE 0x80U

void woz_print_hex(unsigned char c);
void woz_print_hexword(word w);
void woz_putc(unsigned char c);
void woz_puts(const unsigned char *s);
void woz_mon(void);

unsigned char apple1_iskeypressed(void);
unsigned char apple1_getkey(void);
unsigned char apple1_readkey(void);
void apple1_input_line(unsigned char *buffer, unsigned char max);
void apple1_input_line_prompt(unsigned char *buffer, unsigned char max);

#ifndef INPUT_LINE_PROMPT_CHAR
#define INPUT_LINE_PROMPT_CHAR '>'
#endif

/* KickC jukebox helper — no-op under cc65 (DATA copied by crt0). */
void apple1_eprom_init(void);

#endif
