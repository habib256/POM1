/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef VIA_H
#define VIA_H

#include "utils.h"

#define VIA6522 1

extern volatile unsigned char *const VIA_PORTB;
extern volatile unsigned char *const VIA_PORTA;
extern volatile unsigned char *const VIA_DDRB;
extern volatile unsigned char *const VIA_DDRA;
extern volatile unsigned char *const VIA_T1CL;
extern volatile unsigned char *const VIA_T1CH;
extern volatile unsigned char *const VIA_T1LL;
extern volatile unsigned char *const VIA_T1LH;
extern volatile unsigned char *const VIA_T2CL;
extern volatile unsigned char *const VIA_T2CH;
extern volatile unsigned char *const VIA_SR;
extern volatile unsigned char *const VIA_ACR;
extern volatile unsigned char *const VIA_PCR;
extern volatile unsigned char *const VIA_IFR;
extern volatile unsigned char *const VIA_IER;
extern volatile unsigned char *const VIA_PORTANH;

extern unsigned char VIA_IFR_MASK_T1;
extern unsigned char VIA_IFR_MASK_T2;

#endif
