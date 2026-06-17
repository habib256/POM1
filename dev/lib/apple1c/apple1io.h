/*
 * apple1io.h — shared Apple-1 text + keyboard I/O for C (cc65), card-neutral.
 *
 * The common base every Apple-1 C program builds on, independent of any graphics
 * card. Output goes through the WOZ Monitor ECHO routine ($FFEF); input reads the
 * PIA keyboard ($D010/$D011). The two graphics C runtimes add their own layer ON
 * TOP of this shared text base:
 *   - TMS9918 (Antonino "Nino" Porcino) — apple1-videocard-lib screen1.h / tms9918.h
 *   - GEN2 HGR (Uncle Bernie)           — dev/lib/gen2c/gen2.h
 *
 * Bit-7 rule (PIA 6821): the keyboard sets bit 7 as a data-valid strobe;
 * apple1_getkey already masks the key down to 7 bits. woz_putc hands the byte to
 * the ROM ECHO routine, which drives the display the same way Wozmon does.
 *
 * The asm shim (apple1io_asm.s) is ported verbatim from nippur72/apple1-videocard-lib
 * (Antonino "Nino" Porcino) — preserve attribution. Upstream license unspecified.
 */
#ifndef APPLE1IO_H
#define APPLE1IO_H

#define WOZMON    0xFF1AU   /* Wozmon prompt entry — prints "\" + CR, then line editor */
#define ECHO      0xFFEFU
#define KBD_DATA  0xD010U
#define KBD_CTRL  0xD011U

/* ---- Output (via the WOZ Monitor ECHO routine at $FFEF) ---- */
void woz_putc(unsigned char c);          /* print one character                 */
void woz_puts(const unsigned char *s);   /* print a NUL-terminated string        */
void woz_print_hex(unsigned char c);     /* print a byte as two hex digits       */
void woz_print_hexword(unsigned w);      /* print a 16-bit word as four hex digits*/
void woz_mon(void);                      /* return to the WOZ Monitor "\" prompt  */

/* ---- Keyboard (PIA $D010 data / $D011 control) ---- */
unsigned char apple1_iskeypressed(void); /* nonzero (bit 7) if a key is waiting   */
unsigned char apple1_getkey(void);       /* block until a key, return key & 0x7F  */
unsigned char apple1_readkey(void);      /* 0 if no key, else key & 0x7F (no wait) */

#endif /* APPLE1IO_H */
