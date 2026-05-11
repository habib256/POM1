#include "via.h"

volatile unsigned char *const VIA_PORTB   = (volatile unsigned char *)0xA000u;
volatile unsigned char *const VIA_PORTA   = (volatile unsigned char *)0xA001u;
volatile unsigned char *const VIA_DDRB    = (volatile unsigned char *)0xA002u;
volatile unsigned char *const VIA_DDRA    = (volatile unsigned char *)0xA003u;
volatile unsigned char *const VIA_T1CL   = (volatile unsigned char *)0xA004u;
volatile unsigned char *const VIA_T1CH   = (volatile unsigned char *)0xA005u;
volatile unsigned char *const VIA_T1LL   = (volatile unsigned char *)0xA006u;
volatile unsigned char *const VIA_T1LH   = (volatile unsigned char *)0xA007u;
volatile unsigned char *const VIA_T2CL   = (volatile unsigned char *)0xA008u;
volatile unsigned char *const VIA_T2CH   = (volatile unsigned char *)0xA009u;
volatile unsigned char *const VIA_SR     = (volatile unsigned char *)0xA00Au;
volatile unsigned char *const VIA_ACR    = (volatile unsigned char *)0xA00Bu;
volatile unsigned char *const VIA_PCR    = (volatile unsigned char *)0xA00Cu;
volatile unsigned char *const VIA_IFR     = (volatile unsigned char *)0xA00Du;
volatile unsigned char *const VIA_IER     = (volatile unsigned char *)0xA00Eu;
volatile unsigned char *const VIA_PORTANH = (volatile unsigned char *)0xA00Fu;

unsigned char VIA_IFR_MASK_T1 = 0x40U;
unsigned char VIA_IFR_MASK_T2 = 0x20U;
