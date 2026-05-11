#ifndef SCREEN2_H
#define SCREEN2_H

#include "utils.h"
#include "tms9918.h"

extern const unsigned char SCREEN2_TABLE[8];
#define SCREEN2_SIZE (32U * 24U)

extern unsigned char tms_global_mulf_initialized;
extern unsigned char screen2_plot_mode;

#define PLOT_MODE_RESET  0U
#define PLOT_MODE_SET    1U
#define PLOT_MODE_INVERT 2U

void screen2_init_bitmap(unsigned char color);
void screen2_putc(unsigned char ch, unsigned char x, unsigned char y, unsigned char col);
void screen2_puts(const char *s, unsigned char x, unsigned char y, unsigned char col);
void screen2_plot(unsigned char x, unsigned char y);
unsigned char screen2_point(unsigned char x, unsigned char y);
void screen2_line(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1);
void screen2_ellipse_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1);
void screen2_circle(unsigned char xm, unsigned char ym, unsigned char r);

#endif
