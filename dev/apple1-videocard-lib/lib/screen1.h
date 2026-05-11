#ifndef SCREEN1_H
#define SCREEN1_H

#include "utils.h"
#include "tms9918.h"

extern const unsigned char SCREEN1_TABLE[8];

#define SCREEN1_SIZE (32U * 24U)

void screen1_load_font(void);
void screen1_cls(void);
void screen1_scroll_up(void);
void screen1_prepare(void);

#define CHR_BACKSPACE   8U
#define CHR_HOME        11U
#define CHR_CLS         12U
#define CHR_RETURN      13U
#define CHR_REVERSE_OFF 14U
#define CHR_REVERSE_ON  15U
#define CHR_SPACE       32U
#define CHR_REVSPACE    (32U + 128U)

#define HOME         "\x0b"
#define CLS          "\x0c"
#define REVERSE_OFF  "\x0e"
#define REVERSE_ON   "\x0f"

void screen1_putc(unsigned char c);
void screen1_puts(const unsigned char *s);
void screen1_locate(unsigned char x, unsigned char y);
void screen1_strinput(unsigned char *buffer, unsigned char max_length);

#endif
