; APPLE-1 SID KEYBOARD PROGRAM
; by Claudio Parmigiani - 2019
; Ported to ca65 for POM1 emulator
;
; Loads at $0600 — run with 0600R
; Keyboard: Z X C V B N M , = notes C D E F G A B C(next)
;           S D   G H J   = sharps C# D#  F# G# A#
; Waveforms: P=pulse O=noise T=triangle W=sawtooth
; Octaves: 1-8
; *=Theremin mode (reads paddles $C819/$C81A)

.setcpu "6502"
.segment "CODE"

; SID registers (voice 1)
FREQLO1  = $C800        ; frequency LSB
FREQHI1  = $C801        ; frequency MSB
PWM1     = $C802        ; PWM LO byte
PWM2     = $C803        ; PWM HO (4 bit)
CR1      = $C804        ; Waveform and Gate
AD1      = $C805        ; Attack/Decay
SR1      = $C806        ; Sustain/Release
VOLUME   = $C818        ; master volume

; Apple 1 I/O
KBD      = $D010
KBDCR    = $D011
ECHO     = $FFEF

; RAM variables
DELAY_OL = $0280        ; delay outer loop counter
DELAY_IL = $0281        ; delay inner loop counter
WAVEFORM = $0282        ; waveform + gate byte
OCTAVE   = $0283        ; octave number (1-8)
FREQ_HI  = $0284        ; note frequency MSB
FREQ_LO  = $0285        ; note frequency LSB
LASTKEY  = $0286        ; last key pressed
ATDEC    = $0288        ; Attack/Decay parameter
SUSREL   = $0289        ; Sustain/Release parameter
DUR_OL   = $0290        ; duration outer loop
DUR_IL   = $0291        ; duration inner loop
EXCEPT   = $0292        ; exception bit (B note)
NEXTOCT  = $0293        ; next octave bit (, key)
PWMLO    = $0294        ; PWM LO parameter
PWMHI    = $0295        ; PWM HI parameter

; Welcome screen text at $0850
WELCOME1 = $0850
WELCOME2 = $094F

; ---- Entry point ----

        JSR DEFAULT
        JSR READ_1       ; display welcome screen
        JSR INIT
        JSR LOOP_NOTE    ; never returns (reset to exit)

; ---- Main loop ----

LOOP_NOTE:
        JSR KBDIN
        JSR PARSER
        JSR SET_OCTAVE
        LDA FREQ_HI
        STA FREQHI1      ; store MSB freq on SID
        LDA FREQ_LO
        STA FREQLO1      ; store LSB freq on SID
        LDA WAVEFORM
        STA CR1           ; play the note
        JSR DELAY
        JSR KILLER
        LDA #$00
        STA EXCEPT        ; clear the exception bit
        LDA #$00
        STA NEXTOCT       ; clear the next octave bit
        JMP LOOP_NOTE

; ---- Keyboard input ----

KBDIN:
        LDA KBDCR         ; read key from keyboard
        BPL KBDIN
        LDA KBD
        STA LASTKEY
        RTS

; ---- Key parser ----

PARSER:
        JSR ECHO           ; print the char
        LDA LASTKEY

        CMP #$AA           ; * key, Theremin
        BEQ THEREMIN
        CMP #$DA           ; Z key, C note (do)
        BEQ CNOTE
        CMP #$D8           ; X key, D note (re)
        BEQ DNOTE
        CMP #$C3           ; C key, E note (mi)
        BEQ ENOTE
        CMP #$D6           ; V key, F note (fa)
        BEQ FNOTE
        CMP #$C2           ; B key, G note (sol)
        BEQ GNOTE
        CMP #$CE           ; N key, A note (la)
        BEQ ANOTE
        CMP #$CD           ; M key, B note (si)
        BEQ BNOTE
        CMP #$AC           ; , key, C note of next octave
        BEQ CNEXT

        CMP #$D3           ; S key, C# note (do#)
        BEQ CSNOTE
        CMP #$C4           ; D key, D# note (re#)
        BEQ DSNOTE
        CMP #$C7           ; G key, F# note (fa#)
        BEQ FSNOTE
        CMP #$C8           ; H key, G# note (sol#)
        BEQ GSNOTE
        CMP #$CA           ; J key, A# note (la#)
        BEQ ASNOTE

        CMP #$CF           ; O key, noise waveform
        BEQ NOI
        CMP #$D0           ; P key, pulse waveform
        BEQ PUL
        CMP #$D4           ; T key, triangle waveform
        BEQ TRI
        CMP #$D7           ; W key, sawtooth waveform
        BEQ SAW

        CMP #$B9           ; numeral (1-8)?
        BMI NUMERAL
        RTS

; ---- Numeral (octave select) ----

NUMERAL:
        SBC #$AF
        STA OCTAVE
        JMP SET_OCTAVE

; ---- Theremin mode ----

THEREMIN:
        JMP THEREMIN_2

; ---- Waveform select ----

NOI:    JMP NOI_2
PUL:    JMP PUL_2
TRI:    JMP TRI_2
SAW:    JMP SAW_2

; ---- Note dispatchers ----

CNOTE:  JMP CNOTE_2
DNOTE:  JMP DNOTE_2
ENOTE:  JMP ENOTE_2
FNOTE:  JMP FNOTE_2
GNOTE:  JMP GNOTE_2
ANOTE:  JMP ANOTE_2
BNOTE:  LDA #$01           ; set exception bit
        STA EXCEPT
        JMP BNOTE_2

; ---- Sharp note dispatchers ----

CSNOTE: JMP CSNOTE_2
DSNOTE: JMP DSNOTE_2
FSNOTE: JMP FSNOTE_2
GSNOTE: JMP GSNOTE_2
ASNOTE: JMP ASNOTE_2
CNEXT:  JMP CNEXT_2

; ---- Waveform setters ----

NOI_2:
        LDA #$81           ; noise + gate
        STA WAVEFORM
        RTS

PUL_2:
        LDA #$41           ; pulse + gate
        STA WAVEFORM
        RTS

TRI_2:
        LDA #$11           ; triangle + gate
        STA WAVEFORM
        RTS

SAW_2:
        LDA #$21           ; sawtooth + gate
        STA WAVEFORM
        RTS

; ---- Note frequency setters (octave 8 base frequencies) ----

CNOTE_2:                    ; Z key, C note (do)
        LDA #$89
        STA FREQ_HI
        LDA #$2B
        STA FREQ_LO
        RTS

CNEXT_2:                    ; , key, C note (do) next octave
        LDA #$01
        STA NEXTOCT         ; set next octave bit
        LDA #$89
        STA FREQ_HI
        LDA #$2B
        STA FREQ_LO
        RTS

DNOTE_2:                    ; X key, D note (re)
        LDA #$99
        STA FREQ_HI
        LDA #$F7
        STA FREQ_LO
        RTS

ENOTE_2:                    ; C key, E note (mi)
        LDA #$AC
        STA FREQ_HI
        LDA #$D2
        STA FREQ_LO
        RTS

FNOTE_2:                    ; V key, F note (fa)
        LDA #$B7
        STA FREQ_HI
        LDA #$19
        STA FREQ_LO
        RTS

GNOTE_2:                    ; B key, G note (sol)
        LDA #$CD
        STA FREQ_HI
        LDA #$85
        STA FREQ_LO
        RTS

ANOTE_2:                    ; N key, A note (la)
        LDA #$E6
        STA FREQ_HI
        LDA #$B0
        STA FREQ_LO
        RTS

BNOTE_2:                    ; M key, B note (si)
        LDA #$02
        STA FREQ_HI
        LDA #$F0
        STA FREQ_LO
        RTS

; ---- Sharp note frequency setters ----

CSNOTE_2:                   ; S key, C# note (do#)
        LDA #$91
        STA FREQ_HI
        LDA #$53
        STA FREQ_LO
        RTS

DSNOTE_2:                   ; D key, D# note (re#)
        LDA #$A3
        STA FREQ_HI
        LDA #$1F
        STA FREQ_LO
        RTS

FSNOTE_2:                   ; G key, F# note (fa#)
        LDA #$C1
        STA FREQ_HI
        LDA #$FC
        STA FREQ_LO
        RTS

GSNOTE_2:                   ; H key, G# note (sol#)
        LDA #$D9
        STA FREQ_HI
        LDA #$BD
        STA FREQ_LO
        RTS

ASNOTE_2:                   ; J key, A# note (la#)
        LDA #$F4
        STA FREQ_HI
        LDA #$67
        STA FREQ_LO
        RTS

; ---- Delay routine ----

DELAY:
        PHA
        LDA DUR_OL         ; outer loop
        STA DELAY_OL
L1:     LDA DUR_IL          ; inner loop
        STA DELAY_IL
L2:     DEC DELAY_IL
        BNE L2
        DEC DELAY_OL
        BNE L1
        PLA
        RTS

; ---- Kill note (gate off) ----

KILLER:
        LDA #$00
        STA CR1             ; kills the note
        RTS

; ---- Octave division ----

SET_OCTAVE:
        LDY OCTAVE
        JSR CHECK_NEXT_OCT
        CPY #$01
        BNE DIV_LOOP
        RTS

DIV_LOOP:
        JSR DIV_BY_2
        CPY #$01
        BNE DIV_LOOP
        RTS

DIV_BY_2:
        LSR EXCEPT          ; exception bit
        ROR FREQ_HI         ; Shift the MSB
        ROR FREQ_LO         ; Rotate the LSB
        DEY
        RTS

CHECK_NEXT_OCT:
        LDA NEXTOCT
        CMP #$00
        BEQ DO_NO_CHANGE_OCT
        DEY
        RTS

DO_NO_CHANGE_OCT:
        RTS

; ---- Default parameters ----

DEFAULT:
        LDA #$50
        STA DUR_OL          ; delay, outer loop
        LDA #$AE
        STA DUR_IL          ; delay, inner loop
        LDA #$08
        STA ATDEC            ; Attack/Decay
        LDA #$44
        STA SUSREL           ; Sustain/Release
        LDA #$FF
        STA PWMLO            ; PWM1
        LDA #$08
        STA PWMHI            ; PWM2
        LDA #$41
        STA WAVEFORM         ; default: pulse + gate
        LDA #$04
        STA OCTAVE           ; default: octave 4
        RTS

; ---- Initialize SID ----

INIT:
        LDA PWMLO
        STA PWM1             ; store PWM1 on SID
        LDA PWMHI
        STA PWM2             ; store PWM2 on SID
        LDA ATDEC
        STA AD1              ; Attack/Decay
        LDA SUSREL
        STA SR1              ; Sustain/Release
        LDA #$0F
        STA VOLUME           ; max volume
        RTS

; ---- Theremin mode ----

THEREMIN_2:
        LDA WAVEFORM
        STA CR1              ; play the note (non stop)
        JSR THERLOOP
        ; (never returns — reset to exit)

THERLOOP:
        LDA $C819            ; read paddle X
        STA FREQHI1          ; store MSB freq in SID
        LDA $C81A            ; read paddle Y
        STA FREQLO1          ; store LSB freq in SID
        JSR THERLOOP         ; infinite recursion (will eventually overflow stack)

; ---- Welcome screen display ----

READ_1:
        LDX #$00
@r1:    LDA WELCOME1,X
        JSR ECHO
        INX
        CPX #$FF             ; read first 255 chars
        BNE @r1
READ_2:
        LDA WELCOME2,X
        JSR ECHO
        INX
        CPX #$6F             ; read remaining 111
        BNE READ_2
        RTS

; ---- Welcome screen data ----
; Apple 1 characters with bit 7 set

.segment "SCREEN"

; Row 0: "0600: 20"
.byte $8D, $8D
; "A P P L E - 1   S I D   P I A N O"
.byte $C1, $A0, $D0, $A0, $D0, $A0, $CC, $A0, $C5, $A0, $AD, $A0, $B1, $A0
.byte $8D
; "------------------------------"
.byte $A0, $D3, $A0, $C9, $A0, $C4, $A0, $A0, $D0, $A0, $C9, $A0, $C1, $A0
.byte $CE, $A0, $CF, $8D
.byte $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD
.byte $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD, $AD
.byte $AD, $AD, $8D
; "BY CLAUDIO PARMIGIANI - 2019"
.byte $C2, $D9, $A0, $C3, $CC, $C1, $D5, $C4, $C9, $CF, $A0
.byte $D0, $C1, $D2, $CD, $C9, $C7, $C9, $C1, $CE, $C9, $A0
.byte $AD, $A0, $B2, $B0, $B1, $B9, $8D
; "1..8: OCTAVE    *: THEREMIN"
.byte $B1, $AE, $AE, $B8, $BA, $A0, $CF, $C3, $D4, $C1, $D6, $C5
.byte $A0, $A0, $A0, $A0, $AA, $BA, $A0
.byte $D4, $C8, $C5, $D2, $C5, $CD, $C9, $CE, $8D
; "P: PULSE       W: SAWTOOTH"
.byte $D0, $BA, $A0, $D0, $D5, $CC, $D3, $C5
.byte $A0, $A0, $A0, $A0, $A0, $A0, $A0
.byte $D7, $BA, $A0, $D3, $C1, $D7, $D4, $CF, $CF, $D4, $C8, $8D
; "O: NOISE       T: TRIANGLE"
.byte $CF, $BA, $A0, $CE, $CF, $C9, $D3, $C5
.byte $A0, $A0, $A0, $A0, $A0, $A0, $A0
.byte $D4, $BA, $A0, $D4, $D2, $C9, $C1, $CE, $C7, $CC, $C5, $8D
; "MEMORY LOCATIONS:"
.byte $CD, $C5, $CD, $CF, $D2, $D9, $A0, $CC, $CF, $C3, $C1, $D4, $C9, $CF
.byte $CE, $D3, $BA, $8D
; "$0282: WAVEFORM"
.byte $A4, $B0, $B2, $B8, $B2, $BA, $A0, $D7, $C1, $D6, $C5, $C6, $CF, $D2, $CD, $8D
; "$0283: ATTACK/DECAY"
.byte $A4, $B0, $B2, $B8, $B8, $BA, $A0, $C1, $D4, $D4, $C1, $C3, $CB, $AF
.byte $C4, $C5, $C3, $C1, $D9, $8D
; "$0289: SUSTAIN/RELEASE"
.byte $A4, $B0, $B2, $B8, $B9, $BA, $A0, $D3, $D5, $D3, $D4, $C1, $C9, $CE, $AF
.byte $D2, $C5, $CC, $C5, $C1, $D3, $C5, $8D
; "$0290/291: DURATION"
.byte $A4, $B0, $B2, $B9, $B0, $AF, $B2, $B9, $B1, $BA, $A0
.byte $C4, $D5, $D2, $C1, $D4, $C9, $CF, $CE, $8D
; "$0294/295: PWM LO-HI"
.byte $A4, $B0, $B2, $B9, $B4, $AF, $B2, $B9, $B5, $BA, $A0
.byte $D0, $D7, $CD, $A0, $CC, $CF, $AD, $C8, $C9, $8D
; "0606R FOR SOFT ENTRY"
.byte $B0, $B6, $B0, $B6, $D2, $A0, $C6, $CF, $D2, $A0
.byte $D3, $CF, $C6, $D4, $A0, $C5, $CE, $D4, $D2, $D9, $8D
; "KEYBOARD:  S D    G H J"
.byte $CB, $C5, $D9, $C2, $CF, $C1, $D2, $C4, $BA
.byte $A0, $A0, $D3, $A0, $C4, $A0, $A0, $A0, $A0, $C7, $A0, $C8, $A0, $CA, $8D
; "          Z X C V B N M ,"
.byte $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0
.byte $DA, $A0, $D8, $A0, $C3, $A0, $D6, $A0, $C2, $A0, $CE, $A0, $CD, $A0, $AC, $8D
; cursor
.byte $C0, $8D
