; ============================================================================
; sprites_music.asm  --  6 sprites (16x16, TMS9918 sprite mode)
; derived: dev/lib/gen2/sprites/sprites_music_hgr.asm -- after editing this master rerun:
;   python3 tools/build_hgr_sprites.py --only music
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Music" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export music_lute_pat, music_harp_pat, music_panpipe_pat, music_horn_pat, music_flute_pat
.export music_singer_pat

.segment "CODE"

; slot 01/6 of "Music" row -- lute
music_lute_pat:
        .byte $00, $00, $00, $00, $00, $00, $1F, $3F
        .byte $7F, $73, $50, $69, $77, $3B, $1F, $00
        .byte $00, $10, $2E, $1E, $38, $70, $E0, $C0
        .byte $80, $40, $C0, $C0, $C0, $80, $00, $00
; slot 02/6 of "Music" row -- harp
music_harp_pat:
        .byte $00, $3E, $3F, $01, $12, $12, $12, $12
        .byte $12, $12, $12, $12, $12, $13, $3F, $00
        .byte $00, $00, $00, $80, $C0, $78, $84, $A4
        .byte $AC, $B8, $B8, $F0, $F0, $E0, $E0, $00
; slot 03/6 of "Music" row -- panpipe
music_panpipe_pat:
        .byte $00, $00, $0D, $0F, $00, $0D, $0D, $0D
        .byte $0D, $0D, $0D, $0C, $0C, $0C, $00, $00
        .byte $00, $00, $50, $F8, $00, $50, $40, $40
        .byte $00, $00, $00, $00, $00, $00, $00, $00
; slot 04/6 of "Music" row -- horn
music_horn_pat:
        .byte $00, $00, $00, $00, $00, $03, $1F, $23
        .byte $21, $24, $26, $10, $08, $07, $00, $00
        .byte $00, $06, $0E, $04, $E0, $F0, $F0, $F0
        .byte $E0, $E0, $40, $40, $40, $80, $00, $00
; slot 05/6 of "Music" row -- flute
music_flute_pat:
        .byte $00, $40, $20, $10, $08, $66, $66, $60
        .byte $6D, $6D, $6D, $6C, $61, $0C, $60, $00
        .byte $00, $02, $04, $08, $10, $66, $66, $06
        .byte $B6, $B0, $86, $30, $80, $00, $00, $00
; slot 06/6 of "Music" row -- singer
music_singer_pat:
        .byte $00, $10, $08, $07, $0B, $1D, $1F, $0F
        .byte $17, $18, $1F, $1F, $0F, $07, $00, $00
        .byte $00, $08, $10, $E0, $D0, $B8, $F8, $F0
        .byte $E8, $18, $F8, $F8, $F0, $E0, $00, $00
