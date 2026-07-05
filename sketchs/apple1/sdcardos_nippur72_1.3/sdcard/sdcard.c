// *TODO IEC: wildcards in $
// *TODO IEC: verify command
// *TODO IEC: ERR, CMD command
// *TODO IEC: SAVE check for errors
// *TODO IEC: command DIR alias for $
// *TODO IEC: load basic programs
// *TODO IEC: save basic programs
// *TODO IEC: print info after load/save ?
// *TODO IEC: command to change drive number
// *TODO IEC: optimize strlen
// *TODO IEC: optimize VIA_Init() in INITPIA()
// *TODO IEC: optimize use woz mon variables?
// *TODO IEC: bug run basic cmd variabile
// *TODO IEC: dev decimal number
// *TODO IEC: comando @BR
// *TODO IEC: LOAD why does it retry on timeout?
// *TODO IEC: persisent @DEV
// *TODO IEC: comando @R
// *TODO IEC: check PLA/PLA in kernal_load before OP35: do nothing
// *TODO IEC: check initpia SEI 
// *TODO IEC: initpia reset status
// *TODO IEC: file info after all load save
// *TODO IEC: pause keys in dir
// *TODO IEC: STATUS persistent location
// *TODO: extract from apple1-videocard-lib into apple1-sdcard
// *TODO ripristinare easter egg
//
// TODO IEC: simulare load error
// TODO IEC: programma @PRINT o comandi per aprire inviare raw
// TODO IEC: recompile APPLESOFT BASIC
// TODO IEC: applesoft basic for IEC
// TODO IEC: optimize error messages
// TODO IEC: optimize woz_puts
// TODO IEC: optimize command list
// TODO IEC: help on IEC commands
// TODO IEC: apple1_readkey in ASM ?
// TODO IEC: pseudo-kernal entry points
// TODO IEC: BASIC SEQ
// TODO verifica sd card files
// TODO SD write on ST variable
// TODO silent commands?
// TODO applesoft/sd: READ A$,start
// TODO applesoft/sd: WRITE A$,start,end
// TODO applesoft/sd: DIR A$ (LS A$)
// TODO applesoft/sd: LOAD A$, RUN A$
// TODO applesoft/sd: SAVE A$, start, end (only binary)
// TODO applesoft/sd: TYPE A$
// TODO applesoft/sd: DUMP A$,start,end
// TODO applesoft/sd: DEL A$
// TODO applesoft/sd: MKDIR A$
// TODO applesoft/sd: RMDIR A$
// TODO applesoft/sd: CD A$
// TODO applesoft/sd: PWD
// TODO applesoft/sd: MOUNT
// TODO applesoft/sd: @S A$,start,end
// TODO applesoft/sd: @L/@V/@R A$,start
// TODO applesoft/sd: @DIR A$
// TODO applesoft/sd: @ERR A$
// TODO applesoft/sd: @CMD A$

// TODO comando REN
// TODO comando COPY

#define APPLE1_USE_WOZ_MONITOR 1          /* reserve WOZMON zero page locations */

#pragma start_address(0x8000)

#pragma zp_reserve(0x4a) 
#pragma zp_reserve(0x4b)
#pragma zp_reserve(0x4c)
#pragma zp_reserve(0x4d)
#pragma zp_reserve(0x4e)
#pragma zp_reserve(0x4f)

#pragma zp_reserve(0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f)
#pragma zp_reserve(0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f)
#pragma zp_reserve(0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f)
#pragma zp_reserve(0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f)
#pragma zp_reserve(0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f)
#pragma zp_reserve(0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f)
#pragma zp_reserve(0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf)
#pragma zp_reserve(0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf)
#pragma zp_reserve(0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf)
#pragma zp_reserve(0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf)
#pragma zp_reserve(0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef)
#pragma zp_reserve(0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff)

#include <string.h>
#include <utils.h>
#include <apple1.h>
#include <via.h>

#include "iec_lib_c_import.h"

#define CPU_STROBE(v) (*VIA_PORTB = (v))  /* CPU strobe is bit 0 OUTPUT */
#define MCU_STROBE    (*VIA_PORTB & 128)  /* MCU strobe is bit 7 INPUT  */

// global variables that are shared among the functions
__address( 3) byte TIMEOUT;
__address( 4) word TIMEOUT_MAX = 0x1388;
__address( 6) word TIMEOUT_CNT;
__address( 8) word tmpword;              // also FNADR for kernal, and BCD in BIN2BCD
__address(10) byte *start_address;       // also SAL/SAH for kernal
__address(12) byte *end_address;         // also EAL/EAH for kernal
__address(14) word len;                  // also C3P0/VERCK for kernal
__address(16) byte hex_to_word_ok;       
__address(0x11) byte cmd;                  
__address(18) byte *token_ptr;           // also BSOUR and R2D2 for kernal
__address(20) word TIMEOUT_RANGE = 0;

#pragma zp_reserve(22)   //__address(22) byte drive_number;         // also FA for kernal
#pragma zp_reserve(23)   //__address(23) byte warm_flag;
#pragma zp_reserve(26)   // STATUS for kernal

byte const *drive_number = (byte *) 22;
byte const *warm_flag    = (byte *) 23;

__address(24) word tmpword2;

/*
__address(0x24) word _MEMUSS;            // also FNLEN for kernal
__address(0x26) word _ptr;               // also ptr for kernal
__address(0x28) byte _FNLEN;             // also FNLEN for kernal
__address(0x29) byte _SA;                // also SA for kernal
__address(0x2a) byte _BSOUR1;            // also BSOUR1 for kernal
__address(0x2b) byte _COUNT;             // also COUNT for kernal
*/

#define MCU_STROBE_HIGH 128
#define MCU_STROBE_LOW  0

void wait_mcu_strobe(byte v) {
   if(TIMEOUT) return;

   TIMEOUT_CNT = 0;
   while(v ^ MCU_STROBE) {
      TIMEOUT_CNT++;
      if(TIMEOUT_CNT > TIMEOUT_RANGE) TIMEOUT_RANGE = TIMEOUT_CNT;
      if(TIMEOUT_CNT > TIMEOUT_MAX) {
         TIMEOUT = 1;
         break;
      }
   }
}

void send_byte_to_MCU(byte data) {
   *VIA_DDRA = 0xFF;                 // set port A as output
   *VIA_PORTA = data;                // deposit byte on the data port
   CPU_STROBE(1);                    // set strobe high
   wait_mcu_strobe(MCU_STROBE_HIGH); // wait for the MCU to read the data
   CPU_STROBE(0);                    // set strobe low
   wait_mcu_strobe(MCU_STROBE_LOW);  // wait for the MCU to set strobe low
}

byte receive_byte_from_MCU() {
   *VIA_DDRA = 0;                    // set port A as input
   CPU_STROBE(0);                    // set listen
   wait_mcu_strobe(MCU_STROBE_HIGH); // wait for the MCU to deposit data
   byte data = *VIA_PORTA;           // read data
   CPU_STROBE(1);                    // set strobe high
   wait_mcu_strobe(MCU_STROBE_LOW);  // wait for the MCU to set strobe low
   CPU_STROBE(0);                    // set strobe low
   return data;
}

// send a string to the MCU (0 terminator is sent as well)
void send_string_to_MCU(char *msg) {
   while(1) {
      byte data = *msg++;
      send_byte_to_MCU(data);
      if(TIMEOUT) break;
      if(data == 0) break;
   }
}

// receive a string sent by the MCU
void receive_string_from_MCU(char *dest) {
   while(1) {
      byte data = receive_byte_from_MCU();
      *dest++ = data;
      if(TIMEOUT) break;
      if(data == 0) break;  // string terminator
   }
}

// print a string sent by the MCU
void print_string_response() {
   while(1) {
      byte data = receive_byte_from_MCU();
      if(TIMEOUT) break;
      if(data == 0) break;  // string terminator
      else woz_putc(data);
   }
}

/*
// print a string sent by the MCU, breakable via keyboard
void print_string_response_brk() {
   byte print_on = 1;
   while(1) {
      byte data = receive_byte_from_MCU();
      if(TIMEOUT) break;
      if(data == 0) break;  // string terminator
      if(print_on) woz_putc(data);
      if(apple1_readkey()) {
         woz_puts("*BRK*\r");         
         print_on = 0;
      }      
   }
}
*/

/*
//
void print_string_response_linebyline() {
   byte paused = 0;
   byte print_on = 1;
   while(1) {
      // ask for a line of text        
      send_byte_to_MCU(OK_RESPONSE);
      if(TIMEOUT) break;

      // get MCU response byte
      byte data = receive_byte_from_MCU();
      if(TIMEOUT) break;
      if(data == ERR_RESPONSE) break;  // no more lines of text

      // next is a line of text as a string
      print_string_response();
      if(TIMEOUT) break;
   
      if(apple1_readkey()) {
         woz_puts("*BRK*\r");         
         print_on = 0;
      }      
   }
}
*/

void receive_word_from_mcu() {
   *((byte *)&tmpword)     = receive_byte_from_MCU();
   *((byte *)(&tmpword+1)) = receive_byte_from_MCU();   
}

void send_word_to_mcu() {
   send_byte_to_MCU( *((byte *)&tmpword)     );
   send_byte_to_MCU( *((byte *)(&tmpword+1)) );
}

#include "console.h"












