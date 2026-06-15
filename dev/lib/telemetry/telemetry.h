/*
 * telemetry.h — C companion to telemetry.inc for POM1's telemetry side channel.
 *
 * Header-only (cc65, C89-friendly): every helper is a `static` inline-ish
 * function so a C program can emit telemetry frames with the same register
 * window the asm macros drive ($C440-$C443). Mirrors dev/lib/telemetry/
 * telemetry.inc byte-for-byte. Full design: doc/TELEMETRY_SIDE_CHANNEL.md.
 * Python harness: tools/pom1_telemetry.py.
 *
 * ----------------------------------------------------------------------------
 * WIRE FORMAT (self-describing schema frames, v1)
 * ----------------------------------------------------------------------------
 * Two frame kinds share the $C440-$C443 window. Both flush a payload that was
 * accumulated by writing bytes to TELE_DATA ($C440), then writing one control
 * opcode to TELE_CTRL ($C441):
 *
 *   DATA frame    on the wire:  0xAA  len_lo  len_hi  payload[len]
 *                 payload = the field VALUES in schema order, each byte-sized
 *                 by its declared type. Closed with opcode TELE_END (0x01).
 *
 *   SCHEMA frame  on the wire:  0xA5  len_lo  len_hi  payload[len]
 *                 payload = a sequence of field descriptors, each descriptor =
 *                 [type:1 byte][field name: ASCII bytes][0x00 terminator].
 *                 Closed with opcode TELE_SCHEMA (0x03). Never parks lock-step.
 *
 * Field type codes (see TELE_T_* below):
 *   1 = U8   (1 byte unsigned)        4 = S16  (2 bytes LE signed)
 *   2 = S8   (1 byte signed)          5 = BOOL (1 byte, 0=false else true)
 *   3 = U16  (2 bytes LE unsigned)    6 = CHAR (1 byte ASCII)
 *
 * Consumers (the POM1 C++ side / tools/pom1_telemetry.py) keep the LAST schema
 * seen and decode subsequent DATA frames field-by-field. With no schema seen,
 * they fall back to raw bytes. Emit the schema ONCE at startup, then one DATA
 * frame per game tick.
 *
 * Typical use:
 *   #include "telemetry.h"
 *   // at startup, declare the schema once:
 *   tele_field(TELE_T_U8,   "head_x");
 *   tele_field(TELE_T_U8,   "head_y");
 *   tele_field(TELE_T_U8,   "length");
 *   tele_field(TELE_T_BOOL, "alive");
 *   tele_schema_close();                 // emits the 0xA5 schema frame
 *   tele_freerun();                      // live play, no lock-step
 *   // per tick, emit the matching DATA frame:
 *   tele_put(head_x); tele_put(head_y); tele_put(length); tele_put(alive);
 *   tele_frame();                        // emits the 0xAA data frame
 * ----------------------------------------------------------------------------
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

/* --- Registers ($C440-$C443) --- */
#define TELE_DATA_REG   (*(volatile unsigned char *)0xC440U) /* W: push one byte  */
#define TELE_CTRL_REG   (*(volatile unsigned char *)0xC441U) /* W: control opcode */
#define TELE_STAT_REG   (*(volatile unsigned char *)0xC441U) /* R: status bits    */
#define TELE_IN_REG     (*(volatile unsigned char *)0xC442U) /* R: pop inbound    */
#define TELE_INLEN_REG  (*(volatile unsigned char *)0xC443U) /* R: inbound count  */

/* --- TELE_CTRL opcodes (write) --- */
#define TELE_FREERUN    0x00    /* disarm lock-step (free-running tap)            */
#define TELE_END        0x01    /* end-of-DATA-frame: delimit + flush (0xAA)      */
#define TELE_LOCKSTEP   0x02    /* arm deterministic lock-step                    */
#define TELE_SCHEMA     0x03    /* end-of-SCHEMA-frame: delimit + flush (0xA5)    */

/* --- TELE_STAT bits (read) --- */
#define TELE_CONNECTED  0x80    /* b7: a harness is connected                     */
#define TELE_INAVAIL    0x01    /* b0: at least one inbound byte waiting          */

/* --- Schema field type codes --- */
#define TELE_T_U8       1       /* 1 byte unsigned                                */
#define TELE_T_S8       2       /* 1 byte signed                                  */
#define TELE_T_U16      3       /* 2 bytes little-endian unsigned                 */
#define TELE_T_S16      4       /* 2 bytes little-endian signed                   */
#define TELE_T_BOOL     5       /* 1 byte, 0 = false, else true                   */
#define TELE_T_CHAR     6       /* 1 byte ASCII                                   */

/* Push one byte into the current frame's payload. */
static void tele_put(unsigned char v)
{
    TELE_DATA_REG = v;
}

/* Push a 16-bit value little-endian (low byte first) — for U16 / S16 fields. */
static void tele_put16(unsigned v)
{
    TELE_DATA_REG = (unsigned char)(v & 0xFFU);
    TELE_DATA_REG = (unsigned char)((v >> 8) & 0xFFU);
}

/* Close the current DATA frame: delimit (0xAA) + flush. In lock-step this parks
 * the CPU on this write until the harness ACKs. */
static void tele_frame(void)
{
    TELE_CTRL_REG = TELE_END;
}

/* Arm deterministic lock-step (one frame per harness ACK). */
static void tele_arm(void)
{
    TELE_CTRL_REG = TELE_LOCKSTEP;
}

/* Disarm lock-step — free-running fire-hose tap (live play). */
static void tele_freerun(void)
{
    TELE_CTRL_REG = TELE_FREERUN;
}

/* Push one schema field descriptor into the (schema) frame: [type][name][0x00].
 * Call once per field, in declaration order, then tele_schema_close(). */
static void tele_field(unsigned char type, const char *name)
{
    TELE_DATA_REG = type;
    while (*name != 0) {
        TELE_DATA_REG = (unsigned char)*name;
        ++name;
    }
    TELE_DATA_REG = 0x00;        /* name terminator */
}

/* Close the schema frame: delimit (0xA5) + flush. Never parks lock-step. */
static void tele_schema_close(void)
{
    TELE_CTRL_REG = TELE_SCHEMA;
}

/* Read the status byte (bit7 = harness connected, bit0 = inbound available). */
static unsigned char tele_stat(void)
{
    return TELE_STAT_REG;
}

/* Pop one inbound byte from the harness (0 if none). */
static unsigned char tele_in(void)
{
    return TELE_IN_REG;
}

/* Number of inbound bytes pending (saturates at 255). */
static unsigned char tele_inlen(void)
{
    return TELE_INLEN_REG;
}

#endif /* TELEMETRY_H */
