; =============================================
; TMS_Wozmon_demo -- preuve de concept M1->M2.
; VERHILLE Arnaud - 2026
;
; Initialise le TMS9918 en Text Mode F1 (40x24, mêmes dimensions que
; l'écran Apple-1 d'origine), uploade le charmap Apple-1, efface l'écran,
; puis imprime un texte qui valide :
;   - une bannière courte (rendu glyphes + ligne unique)
;   - une ligne de plus de 40 caractères (validation du wrap colonne 40)
;   - une rafale de plus de 24 lignes (validation du scroll)
;   - une ligne finale "TMS9918 CONSOLE OK" (point de repère pour le test)
;
; Termine par JMP $FF1F (WOZ_RST) -- retour silencieux à Wozmon stock.
; L'écran TMS9918 garde son contenu, l'écran $D012 reste utilisable en
; parallèle (la redirection ECHO sera l'objet de la livraison M3).
;
; Assemble :
;   make            (depuis dev/projects/tms9918_wozmon/)
;   ou : python3 emit_TMS_Wozmon_demo_txt.py
;
; Charge :
;   ./build/POM1 --preset 2 \
;                --load 0300:software/tms9918/TMS_Wozmon_demo.bin \
;                --run 0300
; =============================================

.include "apple1.inc"

; --- driver Text Mode F1 ---------------------------------------------------
.import   init_vdp_text, upload_charmap, clear_screen_text
.import   print_at_rc_text
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

; --- runtime console -------------------------------------------------------
.import   console_init, console_putc, console_print_ax
.importzp cur_row, cur_col

; ---------------------------------------------------------------------------
.segment "ZEROPAGE"
; The driver and console import `tmp` from the project. One ZP byte is
; enough -- name_at_rc_text and the bit-reverse loop in upload_charmap are
; the only consumers, and they are never reentrant.
tmp:    .res 1
.exportzp tmp

; ---------------------------------------------------------------------------
.segment "CODE"

start:
        ; --- Bring up the TMS9918 in Text Mode F1 ---
        JSR     init_vdp_text
        JSR     upload_charmap
        JSR     clear_screen_text
        JSR     console_init

        ; --- Banner on row 0 (validates glyph rendering) ---
        LDA     #<msg_banner
        LDX     #>msg_banner
        JSR     console_print_ax

        ; --- Validate column-40 wrap: 50 'X' on a single logical line ---
        LDA     #<msg_wrap_intro
        LDX     #>msg_wrap_intro
        JSR     console_print_ax
        LDX     #50
@wrap_lp:
        LDA     #'X'
        JSR     console_putc
        DEX
        BNE     @wrap_lp
        LDA     #$0D
        JSR     console_putc

        ; --- Validate scroll: print 30 numbered lines (NN <text>) ---
        LDA     #<msg_scroll_intro
        LDX     #>msg_scroll_intro
        JSR     console_print_ax
        LDX     #0
@scroll_lp:
        TXA
        PHA                     ; save loop counter
        ; print "LINE "
        LDA     #<msg_line
        LDX     #>msg_line
        JSR     console_print_ax
        ; print 2-digit decimal counter (00..29 -- single LSR/AND OK)
        PLA
        PHA
        ; tens digit
        LDX     #'0'-1
@tens:  INX
        SEC
        SBC     #10
        BCS     @tens
        ADC     #10             ; back-correct
        PHA                     ; save units
        TXA
        JSR     console_putc    ; tens
        PLA
        CLC
        ADC     #'0'
        JSR     console_putc    ; units
        ; trailing newline
        LDA     #$0D
        JSR     console_putc
        PLA
        CLC
        ADC     #1              ; counter++
        TAX
        CPX     #30
        BNE     @scroll_lp

        ; --- Final OK marker at the bottom ---
        LDA     #<msg_ok
        LDX     #>msg_ok
        JSR     console_print_ax

        ; --- Hand control back to Wozmon stock (silent warm restart) ---
        JMP     WOZ_RST

; ---------------------------------------------------------------------------
; Strings (NUL-terminated). All literal ASCII -- console_putc strips bit 7
; if present, and our Apple-1 charmap renders codes $20..$7E correctly.
; ---------------------------------------------------------------------------
msg_banner:
        .byte   "TMS9918 TEXT MODE F1 -- 40x24"
        .byte   $0D, 0
msg_wrap_intro:
        .byte   "WRAP TEST (50 X, expect 1.25 lines):"
        .byte   $0D, 0
msg_scroll_intro:
        .byte   "SCROLL TEST (30 lines):"
        .byte   $0D, 0
msg_line:
        .byte   "LINE ", 0
msg_ok:
        .byte   "TMS9918 CONSOLE OK"
        .byte   $0D, 0
