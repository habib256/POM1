#ifndef APPLE1_H
#define APPLE1_H

#pragma encoding(ascii)    // encode strings in plain ascii

#ifdef APPLE1_USE_WOZ_MONITOR
   #pragma zp_reserve(0x24) // XAML  Last "opened" location Low
   #pragma zp_reserve(0x25) // XAMH  Last "opened" location High
   #pragma zp_reserve(0x26) // STL   Store address Low
   #pragma zp_reserve(0x27) // STH   Store address High
   #pragma zp_reserve(0x28) // L     Hex value parsing Low
   #pragma zp_reserve(0x29) // H     Hex value parsing High
   #pragma zp_reserve(0x2A) // YSAV  Used to see if hex value is given
   #pragma zp_reserve(0x2B) // MODE  $00=XAM, $7F=STOR, $AE=BLOCK XAM
#endif

// APPLE1   
const word WOZMON    = 0xFF1F;      // enters monitor
const word ECHO      = 0xFFEF;      // output ascii character in A (A not destroyed)
const word PRBYTE    = 0xFFDC;      // print hex byte in A (A destroyed)
const word PRHEX     = 0xFFE5;      // print hex digit in lower A (A destroyed)
const word KEY_DATA  = 0xd010;      // read key
const word KEY_CTRL  = 0xd011;      // control port
const word TERM_DATA = 0xd012;      // write ascii
const word TERM_CTRL = 0xd013;      // control port
const word INBUFFER  = 0x0200;      // woz monitor input buffer
const word INBUFSIZE = 0x80;        // woz monitor input buffer size

// prints a hex byte using the WOZMON routine
void woz_print_hex(byte c) {
   asm {
      lda c
      jsr PRBYTE
   };
}

// print hex word
void woz_print_hexword(word w) {
   woz_print_hex(*((byte *)&w+1));
   woz_print_hex(*((byte *)&w));
}

// puts a character on the apple1 screen using the WOZMON routine
void woz_putc(byte c) {
   asm {
      lda c
      jsr ECHO
   }
}

// puts a 0-terminated string on the apple1 screen
void woz_puts(byte *s) {
   byte c;
   while(c=*s++) woz_putc(c);
}

// returns to WOZMON prompt
void woz_mon() {
   asm {
      jmp WOZMON
   }
}

// returns nonzero if a key has been pressed
inline byte apple1_iskeypressed() {   
   return PEEK(KEY_CTRL) & 0x80;
}

// blocking keyboard read
// reads a key from the apple-1 keyboard
byte apple1_getkey() {
   asm {
      __wait:
      lda KEY_CTRL
      bpl __wait
   }
   return PEEK(KEY_DATA) & 0x7f;
}

// non blocking keyboard read
// reads a key and return 0 if no key is pressed
byte apple1_readkey() {
   if((PEEK(KEY_CTRL) & 0x80)==0) return 0;
   else return PEEK(KEY_DATA) & 0x7f;
}

void apple1_input_line(byte *buffer, byte max) {
   byte x=0;

   while(1) {
      byte c = apple1_getkey();
      buffer[x] = c;
      if(c==13) {
         // RETURN ends input
         break;
      }
      else if(c==27) {
         // ESC clears the string
         x=0;
         break;
      }
      else if(c==8 || c=='_') {
         // BACKSPACE
         if(x != 0) {
            woz_putc('_');
            x--;
         }
      }
      else {
         // character input
         if(x<max) {
            woz_putc(c);
            x++;
         }
      }
   }
   buffer[x]=0;
}

#ifndef INPUT_LINE_PROMPT_CHAR
#define INPUT_LINE_PROMPT_CHAR '>'
#endif 

void apple1_input_line_prompt(byte *buffer, byte max) {
   byte x=0;

   woz_putc(INPUT_LINE_PROMPT_CHAR);

   while(1) {
      byte c = apple1_getkey();
      buffer[x] = c;
      if(c==13) {
         // RETURN ends input
         break;
      }
      else if(c==27) {
         // ESC clears the string
         x=0;
         break;
      }
      else if(c==8 || c=='_') {
         // BACKSPACE
         if(x != 0) {
            x--;
            buffer[x] = 0;
            woz_putc('\r');
            woz_putc(INPUT_LINE_PROMPT_CHAR);
            woz_puts(buffer);
         }
      }
      else {
         // character input
         if(x<max) {
            woz_putc(c);
            x++;
         }
      }
   }
   buffer[x]=0;
}

#include <stdlib.h> // for memcpy

#define LOWRAM_START 0x280
#define LOWRAM_END   0x7FF
#define LOWRAM_SIZE  (LOWRAM_END - LOWRAM_START + 1)
#define DATAINCODE   (0x8000 - LOWRAM_SIZE)

inline void apple1_eprom_init() {
   // copy the initializaton data from ROM to lowram where "Data" segment is allocated
   memcpy((byte *)LOWRAM_START, (byte *)DATAINCODE, LOWRAM_SIZE);
}

#endif
