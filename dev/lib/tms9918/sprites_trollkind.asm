; ============================================================================
; sprites_trollkind.asm  --  4 sprites (16x16, TMS9918 sprite mode)
; derived: dev/lib/gen2/sprites/sprites_trollkind_hgr.asm -- after editing this master rerun:
;   python3 tools/build_hgr_sprites.py --only trollkind
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Trollkind" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export troll_goblin_pat, troll_hobgoblin_pat, troll_cyclops_pat, troll_orc_pat

.segment "CODE"

; slot 01/4 of "Trollkind" row -- goblin  (was: grunt)
troll_goblin_pat:
        .byte $00, $00, $00, $00, $00, $77, $3D, $0F
        .byte $07, $08, $1F, $37, $27, $36, $32, $02
        .byte $00, $00, $00, $00, $00, $EE, $BC, $F0
        .byte $E0, $10, $F8, $EC, $E4, $6C, $4C, $40
; slot 02/4 of "Trollkind" row -- hobgoblin  (was: warrior)
troll_hobgoblin_pat:
        .byte $00, $00, $00, $37, $1F, $0B, $1E, $3C
        .byte $7C, $77, $78, $6F, $4F, $6F, $6C, $04
        .byte $00, $00, $00, $EC, $F8, $D0, $78, $3C
        .byte $BE, $EE, $1E, $F6, $F2, $F6, $36, $20
; slot 03/4 of "Trollkind" row -- cyclops  (was: chief)
troll_cyclops_pat:
        .byte $00, $01, $01, $07, $0F, $0C, $1E, $3F
        .byte $7C, $77, $78, $6F, $4F, $6F, $6C, $04
        .byte $00, $00, $80, $E0, $F0, $30, $78, $FC
        .byte $3E, $EE, $1E, $F6, $F2, $F6, $36, $20
; slot 04/4 of "Trollkind" row -- orc  (was: brute)
troll_orc_pat:
        .byte $00, $08, $13, $1F, $0F, $05, $1F, $38
        .byte $7B, $77, $78, $6F, $4F, $6F, $6C, $04
        .byte $00, $10, $C8, $F8, $F0, $A0, $F8, $5C
        .byte $1E, $EE, $1E, $F6, $F2, $F6, $36, $20
