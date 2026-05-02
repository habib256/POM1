; ============================================================================
; sprites_trollkind.asm  --  4 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Trollkind" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at base $3800 + slot*32.
; ============================================================================
.export troll_grunt_pat, troll_warrior_pat, troll_chief_pat, troll_brute_pat

.segment "CODE"

; slot 01/4 of "Trollkind" row -- grunt
troll_grunt_pat:
        .byte $00, $00, $00, $00, $00, $77, $3D, $0F
        .byte $07, $08, $1F, $37, $27, $36, $32, $02
        .byte $00, $00, $00, $00, $00, $EE, $BC, $F0
        .byte $E0, $10, $F8, $EC, $E4, $6C, $4C, $40
; slot 02/4 of "Trollkind" row -- warrior
troll_warrior_pat:
        .byte $00, $00, $00, $37, $1F, $0B, $1E, $3C
        .byte $7C, $77, $78, $6F, $4F, $6F, $6C, $04
        .byte $00, $00, $00, $EC, $F8, $D0, $78, $3C
        .byte $BE, $EE, $1E, $F6, $F2, $F6, $36, $20
; slot 03/4 of "Trollkind" row -- chief
troll_chief_pat:
        .byte $00, $01, $01, $07, $0F, $0C, $1E, $3F
        .byte $7C, $77, $78, $6F, $4F, $6F, $6C, $04
        .byte $00, $00, $80, $E0, $F0, $30, $78, $FC
        .byte $3E, $EE, $1E, $F6, $F2, $F6, $36, $20
; slot 04/4 of "Trollkind" row -- brute
troll_brute_pat:
        .byte $00, $08, $13, $1F, $0F, $05, $1F, $38
        .byte $7B, $77, $78, $6F, $4F, $6F, $6C, $04
        .byte $00, $10, $C8, $F8, $F0, $A0, $F8, $5C
        .byte $1E, $EE, $1E, $F6, $F2, $F6, $36, $20
