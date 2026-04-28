; ============================================================================
; sd.asm -- byte-level VIA handshake for the P-LAB microSD card
; ============================================================================
; Half-duplex byte channel over the 65C22 VIA at $A000. Mirrors the
; protocol the SD CARD OS ROM uses internally, but exposed as Wozmon-
; callable subroutines so a project can talk to the MCU directly without
; going through the ROM's command prompt.
;
;   sd_via_init    -- one-time setup: DDRB = $01 (CPU_STROBE output),
;                     DDRA = $FF (default output), CPU_STROBE = 0,
;                     PORTA = $00. Doesn't reset the MCU phase — call
;                     sd_send_byte with SD_CMD_MOUNT first if you've
;                     just powered up or hard-reset.
;                     Clobbers A. X, Y preserved.
;
;   sd_send_byte   -- A = byte. Writes to PORTA, raises CPU_STROBE,
;                     waits for MCU_STROBE acknowledgement, drops
;                     CPU_STROBE. Returns when the MCU has consumed
;                     the byte.
;                     Clobbers A. X, Y preserved.
;
;   sd_recv_byte   -- request the next byte from the MCU. Sets DDRA=$00
;                     (input), raises CPU_STROBE, waits for MCU_STROBE,
;                     reads PORTA, drops CPU_STROBE, restores DDRA=$FF.
;                     Returns A = received byte.
;                     Clobbers A. X, Y preserved.
;
;   sd_send_str    -- A=lo, X=hi → ASCIIZ string. Sends each byte
;                     including the trailing NUL (most SD commands
;                     expect a NUL-terminated argument).
;                     Clobbers A, Y. X preserved.
;
; ZP usage: 2 bytes (sd_str_lo / sd_str_hi). Default declared via
; .ifndef so ZP-tight projects can alias to existing scratch.
;
; Caller responsibility: sd.inc included (this module includes it).
;
; Timing: each handshake takes a few hundred CPU cycles (the emulator's
; MCU responds within one 6502 instruction; real hardware would be
; slower but well under 1 ms per byte). The poll loops below are
; bounded by the MCU's response — no timeout. If the card is unplugged
; or the MCU is wedged, sd_send_byte / sd_recv_byte will spin forever.
; Add a guard with a counter if your project needs a timeout.
; ============================================================================

.include "sd.inc"

.ifndef sd_str_lo
.segment "ZEROPAGE"
sd_str_lo:      .res 1
sd_str_hi:      .res 1
.endif

.segment "CODE"

; ----------------------------------------------------------------------------
; sd_via_init -- one-time VIA setup: DDRB = bit-0 only, DDRA = output,
;                CPU_STROBE = 0, PORTA = 0.
; ----------------------------------------------------------------------------
sd_via_init:
        LDA     #SD_DDRB_INIT           ; bit 0 (CPU_STROBE) = output
        STA     SD_VIA_DDRB
        LDA     #$FF
        STA     SD_VIA_DDRA             ; PORTA all output by default
        LDA     #$00
        STA     SD_VIA_PORTB            ; CPU_STROBE = 0
        STA     SD_VIA_PORTA
        RTS

; ----------------------------------------------------------------------------
; sd_send_byte -- A = byte. Send via half-duplex handshake.
; ----------------------------------------------------------------------------
sd_send_byte:
        STA     SD_VIA_PORTA            ; latch byte on PORTA (DDRA = $FF set elsewhere)
        LDA     #$FF
        STA     SD_VIA_DDRA             ; ensure output
        LDA     #SD_CPU_STROBE
        STA     SD_VIA_PORTB            ; raise CPU_STROBE → MCU sees rising edge
@wait:  LDA     SD_VIA_PORTB
        AND     #SD_MCU_STROBE
        BEQ     @wait                   ; loop until MCU acknowledges
        LDA     #$00
        STA     SD_VIA_PORTB            ; drop CPU_STROBE → completes handshake
        RTS

; ----------------------------------------------------------------------------
; sd_recv_byte -- request next byte from MCU. Returns A = byte.
; ----------------------------------------------------------------------------
sd_recv_byte:
        LDA     #$00
        STA     SD_VIA_DDRA             ; PORTA = input
        LDA     #SD_CPU_STROBE
        STA     SD_VIA_PORTB            ; raise CPU_STROBE → request byte
@wait:  LDA     SD_VIA_PORTB
        AND     #SD_MCU_STROBE
        BEQ     @wait                   ; wait for MCU to put byte on PORTA
        LDA     SD_VIA_PORTA            ; read byte
        PHA
        LDA     #$00
        STA     SD_VIA_PORTB            ; drop CPU_STROBE → ack receipt
        LDA     #$FF
        STA     SD_VIA_DDRA             ; restore default output
        PLA
        RTS

; ----------------------------------------------------------------------------
; sd_send_str -- A=lo, X=hi → ASCIIZ. Sends every byte including the NUL.
; ----------------------------------------------------------------------------
sd_send_str:
        STA     sd_str_lo
        STX     sd_str_hi
        LDY     #$00
@lp:    LDA     (sd_str_lo),Y
        JSR     sd_send_byte
        BEQ     @done                   ; stop AFTER sending the NUL
        INY
        BNE     @lp
@done:  RTS
