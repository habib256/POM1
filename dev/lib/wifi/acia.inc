; ============================================================================
; acia.inc -- equates for the W65C51N ACIA on the P-LAB Wi-Fi Modem card
; ============================================================================
; The card pairs a 65C51 ACIA at $B000-$B003 with an ESP8266 doing TCP +
; AT-command framing. From the 6502's perspective it's a plain serial
; UART — write bytes to ACIA_DATA, the ESP8266 forwards them as TCP
; payload (or interprets them as AT commands when not connected).
;
; Idempotent: outer .ifndef guard.
; ============================================================================

.ifndef _ACIA_INC_LOADED_
_ACIA_INC_LOADED_ = 1

; --- Register addresses ----------------------------------------------------
ACIA_DATA       = $B000     ; +0 read = Rx byte (clears RDRF), write = Tx byte
ACIA_STATUS     = $B001     ; +1 read-only status; write triggers reset
ACIA_COMMAND    = $B002     ; +2 write-only command register
ACIA_CONTROL    = $B003     ; +3 write-only control register (baud + framing)

; --- Status register bits (W65C51N datasheet) ------------------------------
ACIA_ST_PARITY_ERR  = $01   ; bit 0
ACIA_ST_FRAMING_ERR = $02   ; bit 1
ACIA_ST_OVERRUN     = $04   ; bit 2 — Rx buffer dropped a byte
ACIA_ST_RDRF        = $08   ; bit 3 — Receiver Data Register Full (byte ready)
ACIA_ST_TDRE        = $10   ; bit 4 — Transmitter Data Register Empty (always
                            ;          set on POM1, mirrors W65C51N silicon bug)
ACIA_ST_DCD         = $20   ; bit 5 — Data Carrier Detect (active LOW: 0 = connected)
ACIA_ST_DSR         = $40   ; bit 6 — Data Set Ready (active LOW)
ACIA_ST_IRQ         = $80   ; bit 7 — IRQ pending

; --- Command register bits -------------------------------------------------
ACIA_CMD_DTR        = $01   ; bit 0 — Data Terminal Ready (set this to enable Tx/Rx)
ACIA_CMD_IRQ_DIS    = $02   ; bit 1 — disable Rx IRQ (we poll, so keep this set)
ACIA_CMD_TX_BREAK   = $0C   ; bits 2-3 — Tx control (0=disable, 4=normal, 8=break, C=normal+IRQ off)
ACIA_CMD_ECHO       = $10   ; bit 4 — local echo
ACIA_CMD_PARITY_EN  = $20   ; bit 5 — parity enable
                            ; bits 6-7 = parity type (00=odd, 01=even, 10=mark, 11=space)

; --- Control register bits (baud rate selector in low nibble) --------------
; The high nibble selects framing: 8N1 = $00. Bit 4 selects internal baud
; clock (1 = use internal divider; 0 = external clock). Most projects use
; internal clock at 8N1 → high nibble = $10.
ACIA_CTL_8N1_INT    = $10   ; 8 data bits, 1 stop, no parity, internal clock

; Baud rate equates (low nibble of CONTROL, OR with framing bits above).
ACIA_BAUD_50    = $01
ACIA_BAUD_75    = $02
ACIA_BAUD_109   = $03       ; 109.92
ACIA_BAUD_134   = $04       ; 134.58
ACIA_BAUD_150   = $05
ACIA_BAUD_300   = $06
ACIA_BAUD_600   = $07
ACIA_BAUD_1200  = $08
ACIA_BAUD_1800  = $09
ACIA_BAUD_2400  = $0A
ACIA_BAUD_3600  = $0B
ACIA_BAUD_4800  = $0C
ACIA_BAUD_7200  = $0D
ACIA_BAUD_9600  = $0E       ; most common BBS rate
ACIA_BAUD_19200 = $0F       ; max on the W65C51N

.endif  ; _ACIA_INC_LOADED_
