; ============================================================================
; sd.inc -- equates for the P-LAB microSD card (65C22 + emulated ATMEGA + SD)
; ============================================================================
; The card pairs a 65C22 VIA at $A000-$A00F with an ATMEGA running an
; SD-card filesystem (FAT32 over a virtual disk image rooted at sdcard/).
; Above the VIA, an SD CARD OS ROM at $8000-$9FFF speaks an ASCII command
; line ("D", "LOAD <name>", "WRITE …") on top of a byte-level half-duplex
; handshake. This lib provides the low-level handshake — the ROM
; equivalent for projects that want to bypass the OS prompt (e.g. for
; the CAT/HEXDUMP/XCOPY utilities mentioned in TODO6502.md).
;
; Bus protocol:
;   $A000  PORTB   bit 0 = CPU_STROBE out, bit 7 = MCU_STROBE in
;   $A001  PORTA   8-bit data bus, direction set by DDRA
;   $A002  DDRB    Data Direction B (CPU_STROBE bit must be output)
;   $A003  DDRA    $FF = output (CPU sends), $00 = input (CPU receives)
;
; Half-duplex handshake (per byte):
;   Send:   set DDRA = $FF, write byte to PORTA, raise CPU_STROBE,
;           wait for MCU_STROBE = 1, drop CPU_STROBE.
;   Recv:   set DDRA = $00, raise CPU_STROBE to request, wait for
;           MCU_STROBE = 1, read PORTA, drop CPU_STROBE.
;
; Higher-level command framing (above byte-level) is the SD CARD OS
; ROM's job — see CMD_* constants below for the protocol contract.
;
; Idempotent: outer .ifndef guard.
; ============================================================================

.ifndef _SD_INC_LOADED_
_SD_INC_LOADED_ = 1

; --- VIA 65C22 register addresses ($A000 + 0..15) ---------------------------
SD_VIA_PORTB    = $A000     ; bit 0: CPU_STROBE, bit 7: MCU_STROBE
SD_VIA_PORTA    = $A001     ; 8-bit data bus
SD_VIA_DDRB     = $A002     ; Data Direction B
SD_VIA_DDRA     = $A003     ; Data Direction A ($FF = out, $00 = in)
SD_VIA_T1CL     = $A004     ; Timer 1 counter low (read), latch low (write)
SD_VIA_T1CH     = $A005     ; Timer 1 counter high
SD_VIA_T1LL     = $A006     ; Timer 1 latch low
SD_VIA_T1LH     = $A007     ; Timer 1 latch high
SD_VIA_T2CL     = $A008     ; Timer 2 counter low
SD_VIA_T2CH     = $A009     ; Timer 2 counter high
SD_VIA_SR       = $A00A     ; Shift register
SD_VIA_ACR      = $A00B     ; Auxiliary Control Register
SD_VIA_PCR      = $A00C     ; Peripheral Control Register
SD_VIA_IFR      = $A00D     ; Interrupt Flag Register
SD_VIA_IER      = $A00E     ; Interrupt Enable Register
SD_VIA_PORTA_NH = $A00F     ; PORTA without handshake

; --- Strobe bits in PORTB ---------------------------------------------------
SD_CPU_STROBE   = $01       ; bit 0 — CPU drives this
SD_MCU_STROBE   = $80       ; bit 7 — MCU drives this

; --- DDRB — must include SD_CPU_STROBE as output, others as input -----------
SD_DDRB_INIT    = $01       ; only bit 0 (CPU_STROBE) is CPU-driven

; --- MCU command IDs (sent as the FIRST byte after MOUNT/handshake) --------
SD_CMD_READ   = 0           ; READ <NAME\0> → response: 1 byte status, then file
SD_CMD_WRITE  = 1           ; WRITE <NAME\0> + 2-byte len + data
SD_CMD_DIR    = 2           ; list directory (multi-byte response, terminator)
SD_CMD_LOAD   = 4           ; LOAD <NAME\0> — like READ but at tagged addr
SD_CMD_DEL    = 11          ; DEL <NAME\0>
SD_CMD_LS     = 12          ; alias for DIR
SD_CMD_CD     = 13          ; CD <PATH\0>
SD_CMD_MKDIR  = 14          ; MKDIR <NAME\0>
SD_CMD_RMDIR  = 15          ; RMDIR <NAME\0>
SD_CMD_PWD    = 19          ; PWD → response: cwd string
SD_CMD_TEST   = 20          ; echo a single byte (for handshake validation)
SD_CMD_MOUNT  = 23          ; reset MCU state, prepare for new commands

; --- Standard MCU response codes -------------------------------------------
SD_OK_RESPONSE  = $00
SD_ERR_RESPONSE = $FF

.endif  ; _SD_INC_LOADED_
