#ifndef VIA_H
#define VIA_H

#define VIA6522 1

byte *const VIA_PORTB   = (byte *) 0xA000;
byte *const VIA_PORTA   = (byte *) 0xA001;
byte *const VIA_DDRB    = (byte *) 0xA002;
byte *const VIA_DDRA    = (byte *) 0xA003;
byte *const VIA_T1CL    = (byte *) 0xA004;
byte *const VIA_T1CH    = (byte *) 0xA005;
byte *const VIA_T1LL    = (byte *) 0xA006;
byte *const VIA_T1LH    = (byte *) 0xA007;
byte *const VIA_T2CL    = (byte *) 0xA008;
byte *const VIA_T2CH    = (byte *) 0xA009;
byte *const VIA_SR      = (byte *) 0xA00A;
byte *const VIA_ACR     = (byte *) 0xA00B;
byte *const VIA_PCR     = (byte *) 0xA00C;
byte *const VIA_IFR     = (byte *) 0xA00D;
byte *const VIA_IER     = (byte *) 0xA00E;
byte *const VIA_PORTANH = (byte *) 0xA00F;

byte VIA_IFR_MASK_T1 = 0b01000000;
byte VIA_IFR_MASK_T2 = 0b00100000;

#endif