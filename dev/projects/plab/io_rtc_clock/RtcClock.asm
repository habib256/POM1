; =============================================================
; RtcClock.asm — P-LAB text clock (reference)
; English month names, 40 columns, 5 s pause.
; =============================================================
; P-LAB clock (A1-IO / DS3231) — month names (English), 40 cols, 5 s
; =============================================================
; Centred line: DD MONTH YYYY HH:MM:SS  (variable length)
; Month: JANUARY ... DECEMBER (uppercase ASCII)
; Year 20xx (register 5 = years since 2000)
;
;   Build: make   (ca65 + ld65 via apple1_4k.cfg; see this directory's Makefile)
; =============================================================

.include "apple1.inc"
.include "a1io.inc"

COLS    = 40

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
dig_yr0:    .res 1
dig_yr1:    .res 1
line_len:   .res 1
padl:       .res 1
padr:       .res 1
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
        CMP #$20
        BCC @dok
@dfx:   LDA #$01
@dok:   STA rtc_day
        LDA rtc_mon
        BEQ @mfx
        CMP #$0D
        BCC @mnok
@mfx:   LDA #$01
@mnok:  STA rtc_mon
        LDA rtc_yr
        CMP #$64
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

; line_len = 2 + 1 + Lmonth + 1 + 4 + 1 + 8 = 17 + Lmonth
compute_line_len:
        LDX rtc_mon
        DEX
        BMI @def
        CPX #$0C
        BCS @def
        LDA month_lens,X
        CLC
        ADC #$11              ; +17
        STA line_len
        RTS
@def:   LDA #$11
        CLC
        ADC #$07              ; January = 7
        STA line_len
        RTS

compute_padding:
        JSR compute_line_len
        LDA #COLS
        SEC
        SBC line_len
        PHA
        LSR A
        STA padl
        PLA
        SEC
        SBC padl
        STA padr
        RTS

print_spaces_padl:
        LDX padl
        BEQ @spxd
        LDA #$A0
@splp:  JSR ECHO
        DEX
        BNE @splp
@spxd:  RTS

print_spaces_padr:
        LDX padr
        BEQ @srxd
        LDA #$A0
@srlp:  JSR ECHO
        DEX
        BNE @srlp
@srxd:  RTS

print_month_name:
        LDX rtc_mon
        DEX
        BMI @bad
        CPX #$0C
        BCS @bad
        LDA month_lo,X
        STA str_lo
        LDA month_hi,X
        STA str_hi
        LDY #$00
@pm:    LDA (str_lo),Y
        BEQ @px
        ORA #$80
        JSR ECHO
        INY
        BNE @pm
@px:    RTS
@bad:   LDA #<m_jan
        STA str_lo
        LDA #>m_jan
        STA str_hi
        LDY #$00
        BEQ @pm                 ; always — print JANUARY

; --- DD MONTH YYYY HH:MM:SS centred on 40 columns ---
print_datetime40:
        JSR compute_padding
        JSR print_spaces_padl

        LDA dig_dy0
        JSR echo_digit
        LDA dig_dy1
        JSR echo_digit
        LDA #$A0
        JSR ECHO

        JSR print_month_name

        LDA #$A0
        JSR ECHO
        LDA #$B2
        JSR ECHO
        LDA #$B0
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

        JMP print_spaces_padr

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

; Lengths: January..December
month_lens:
        .byte 7, 8, 5, 5, 3, 4, 4, 6, 9, 7, 8, 8

month_lo:
        .byte <m_jan,<m_fev,<m_mar,<m_avr,<m_mai,<m_juin
        .byte <m_juil,<m_aou,<m_sep,<m_oct,<m_nov,<m_dec
month_hi:
        .byte >m_jan,>m_fev,>m_mar,>m_avr,>m_mai,>m_juin
        .byte >m_juil,>m_aou,>m_sep,>m_oct,>m_nov,>m_dec

m_jan:  .asciiz "JANUARY"
m_fev:  .asciiz "FEBRUARY"
m_mar:  .asciiz "MARCH"
m_avr:  .asciiz "APRIL"
m_mai:  .asciiz "MAY"
m_juin: .asciiz "JUNE"
m_juil: .asciiz "JULY"
m_aou:  .asciiz "AUGUST"
m_sep:  .asciiz "SEPTEMBER"
m_oct:  .asciiz "OCTOBER"
m_nov:  .asciiz "NOVEMBER"
m_dec:  .asciiz "DECEMBER"

msg_title:
        .byte $8D
        .asciiz "=== P-LAB RTC  English month names ==="
        .byte $8D
        .asciiz "  Touche : quitter"
        .byte $8D, $8D, $00
