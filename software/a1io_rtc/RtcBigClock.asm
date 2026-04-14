; =============================================================
; Horloge P-LAB (A1-IO / DS3231) — date + heure, 40 colonnes, 5 s
; =============================================================
; Ligne centrée : JJ/MM/AAAA HH:MM:SS  (19 car.) dans 40 colonnes
; Année = 20xx (registre 5 = années depuis 2000, cf. A1IO_RTC)
; Registres virtuels : 0=h 1=min 2=s 3=jour 4=mois 5=an (depuis 2000)
;
; VIA : $2000 IRB, $2001 IRA (bit7 STROBE, bits0-4 = n° reg)
;
;   ca65 -o build/RtcBigClock.o software/a1io_rtc/RtcBigClock.asm
;   ld65 -C software/apple1_4k.cfg -o build/RtcBigClock.bin build/RtcBigClock.o
; =============================================================

ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010
VIA_IRB = $2000
VIA_IRA = $2001

COLS    = 40
; (40 - 19) / 2 = 10, reste 11 à droite
PADL_DT = 10
PADR_DT = 11

.segment "ZEROPAGE"
needreg:    .res 1
rtc_h:      .res 1
rtc_m:      .res 1
rtc_s:      .res 1
rtc_day:    .res 1
rtc_mon:    .res 1
rtc_yr:     .res 1
dig0:       .res 1
dig1:       .res 1
dig2:       .res 1
dig3:       .res 1
dig4:       .res 1
dig5:       .res 1
dig_dy0:    .res 1
dig_dy1:    .res 1
dig_mn0:    .res 1
dig_mn1:    .res 1
dig_yr0:    .res 1
dig_yr1:    .res 1
str_lo:     .res 1
str_hi:     .res 1
delay_i:    .res 1

.segment "CODE"

main:
        LDA #$8D
        JSR ECHO
        LDA #<msg_title
        LDX #>msg_title
        JSR print_str

@loop:
        LDX #$00
        JSR read_rtc_reg
        STA rtc_h
        LDX #$01
        JSR read_rtc_reg
        STA rtc_m
        LDX #$02
        JSR read_rtc_reg
        STA rtc_s
        LDX #$03
        JSR read_rtc_reg
        STA rtc_day
        LDX #$04
        JSR read_rtc_reg
        STA rtc_mon
        LDX #$05
        JSR read_rtc_reg
        STA rtc_yr

        LDA rtc_h
        CMP #$18
        BCC @hok
        LDA #$00
        STA rtc_h
@hok:   LDA rtc_m
        CMP #$3C
        BCC @mok
        LDA #$00
        STA rtc_m
@mok:   LDA rtc_s
        CMP #$3C
        BCC @sok
        LDA #$00
        STA rtc_s
@sok:   LDA rtc_day
        BEQ @dfx
        CMP #$20                  ; > 31
        BCC @dok
@dfx:   LDA #$01
@dok:   STA rtc_day
        LDA rtc_mon
        BEQ @mfx
        CMP #$0D                  ; > 12
        BCC @mnok
@mfx:   LDA #$01
@mnok:  STA rtc_mon
        LDA rtc_yr
        CMP #$64                  ; > 99
        BCC @yok
        LDA #$00
@yok:   STA rtc_yr

        LDA rtc_h
        JSR div10
        PHA
        TXA
        JSR clamp9
        STA dig0
        PLA
        JSR clamp9
        STA dig1

        LDA rtc_m
        JSR div10
        PHA
        TXA
        JSR clamp9
        STA dig2
        PLA
        JSR clamp9
        STA dig3

        LDA rtc_s
        JSR div10
        PHA
        TXA
        JSR clamp9
        STA dig4
        PLA
        JSR clamp9
        STA dig5

        LDA rtc_day
        JSR div10
        PHA
        TXA
        JSR clamp9
        STA dig_dy0
        PLA
        JSR clamp9
        STA dig_dy1

        LDA rtc_mon
        JSR div10
        PHA
        TXA
        JSR clamp9
        STA dig_mn0
        PLA
        JSR clamp9
        STA dig_mn1

        LDA rtc_yr
        JSR div10
        PHA
        TXA
        JSR clamp9
        STA dig_yr0
        PLA
        JSR clamp9
        STA dig_yr1

        JSR print_sep40
        LDA #$8D
        JSR ECHO

        JSR print_datetime40
        LDA #$8D
        JSR ECHO

        JSR print_sep40
        LDA #$8D
        JSR ECHO

        JSR delay_5s
        JSR poll_key_quit
        BCS @done
        JMP @loop
@done:  RTS

echo_a_x40:
        LDX #COLS
@e:     JSR ECHO
        DEX
        BNE @e
        RTS

print_sep40:
        LDA #$BD
        JMP echo_a_x40

; --- JJ/MM/AAAA HH:MM:SS sur 40 colonnes ---
print_datetime40:
        LDX #PADL_DT
        LDA #$A0
@lp:    JSR ECHO
        DEX
        BNE @lp

        LDA dig_dy0
        JSR echo_digit
        LDA dig_dy1
        JSR echo_digit
        LDA #$AF                ; '/' | $80
        JSR ECHO
        LDA dig_mn0
        JSR echo_digit
        LDA dig_mn1
        JSR echo_digit
        LDA #$AF
        JSR ECHO
        LDA #$B2                ; '2' — milliers / centaines (20xx)
        JSR ECHO
        LDA #$B0                ; '0'
        JSR ECHO
        LDA dig_yr0
        JSR echo_digit
        LDA dig_yr1
        JSR echo_digit
        LDA #$A0
        JSR ECHO

        LDA dig0
        JSR echo_digit
        LDA dig1
        JSR echo_digit
        LDA #$BA
        JSR ECHO
        LDA dig2
        JSR echo_digit
        LDA dig3
        JSR echo_digit
        LDA #$BA
        JSR ECHO
        LDA dig4
        JSR echo_digit
        LDA dig5
        JSR echo_digit

        LDX #PADR_DT
        LDA #$A0
@rp:    JSR ECHO
        DEX
        BNE @rp
        RTS

echo_digit:
        ORA #$B0
        JMP ECHO

read_rtc_reg:
        STX needreg
@poll:  LDA VIA_IRA
        BPL @poll
        AND #$1F
        CMP needreg
        BNE @poll
        LDA VIA_IRB
        RTS

div10:
        LDX #$FF
@d:     INX
        SEC
        SBC #$0A
        BCS @d
        ADC #$0A
        RTS

clamp9:
        CMP #$0A
        BCC @c9x
        LDA #$09
@c9x:   RTS

print_str:
        STA str_lo
        STX str_hi
        LDY #$00
@ps:    LDA (str_lo),Y
        BEQ @px
        ORA #$80
        JSR ECHO
        INY
        BNE @ps
@px:    RTS

delay_5s:
        LDA #$06
        STA delay_i
@blk:   LDX #$00
@ox:    LDY #$00
@iy:    NOP
        NOP
        NOP
        NOP
        INY
        BNE @iy
        INX
        BNE @ox
        DEC delay_i
        BNE @blk
        RTS

poll_key_quit:
        LDA KBDCR
        BPL @no
        LDA KBD
        SEC
        RTS
@no:    CLC
        RTS

msg_title:
        .byte $8D
        .asciiz "=== P-LAB RTC  date + heure  (40 cols) ==="
        .byte $8D
        .asciiz "  Touche : quitter"
        .byte $8D, $8D, $00
