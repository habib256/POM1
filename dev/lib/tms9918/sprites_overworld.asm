; ============================================================================
; sprites_overworld.asm  --  13 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Overworld" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at base $3800 + slot*32.
; ============================================================================
.export world_grass_pat, world_pebbles_pat, world_bushes_pat, world_tree_pat, world_pine_pat
.export world_deadtree_pat, world_water_pat, world_hill_pat, world_tent_pat, world_hut_pat
.export world_castle_pat, world_dock_pat, world_boat_pat

.segment "CODE"

; slot 01/13 of "Overworld" row -- grass
world_grass_pat:
        .byte $00, $00, $00, $00, $00, $00, $02, $02
        .byte $08, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $A0
        .byte $80, $00, $00, $00, $00, $00, $00, $00
; slot 02/13 of "Overworld" row -- pebbles
world_pebbles_pat:
        .byte $00, $00, $00, $00, $00, $00, $02, $00
        .byte $08, $03, $03, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $C0, $E0, $60
        .byte $00, $20, $00, $00, $00, $00, $00, $00
; slot 03/13 of "Overworld" row -- bushes
world_bushes_pat:
        .byte $00, $00, $00, $00, $00, $01, $05, $06
        .byte $02, $01, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $80, $A0, $60
        .byte $40, $80, $00, $00, $00, $00, $00, $00
; slot 04/13 of "Overworld" row -- tree
world_tree_pat:
        .byte $00, $03, $0F, $1F, $3F, $3F, $7F, $7F
        .byte $7C, $3A, $0D, $01, $01, $01, $03, $00
        .byte $00, $C0, $F0, $F8, $FC, $FC, $FE, $FE
        .byte $3E, $5C, $B0, $80, $80, $80, $C0, $00
; slot 05/13 of "Overworld" row -- pine
world_pine_pat:
        .byte $00, $01, $03, $07, $00, $07, $0F, $1F
        .byte $00, $0F, $1F, $3F, $00, $01, $01, $00
        .byte $00, $80, $C0, $E0, $00, $E0, $F0, $F8
        .byte $00, $F0, $F8, $FC, $00, $80, $80, $00
; slot 06/13 of "Overworld" row -- deadtree
world_deadtree_pat:
        .byte $00, $00, $0A, $04, $1C, $04, $02, $7A
        .byte $27, $03, $01, $01, $01, $01, $03, $00
        .byte $00, $00, $50, $20, $38, $20, $40, $5E
        .byte $E4, $C0, $80, $80, $80, $80, $C0, $00
; slot 07/13 of "Overworld" row -- water
world_water_pat:
        .byte $00, $00, $0C, $33, $00, $00, $00, $0C
        .byte $33, $00, $00, $00, $0C, $33, $00, $00
        .byte $00, $00, $CC, $30, $00, $00, $00, $CC
        .byte $30, $00, $00, $00, $CC, $30, $00, $00
; slot 08/13 of "Overworld" row -- hill
world_hill_pat:
        .byte $00, $07, $0F, $0F, $1F, $1E, $1D, $1B
        .byte $03, $1B, $3D, $7E, $7E, $7E, $7E, $00
        .byte $00, $E0, $F0, $F8, $F8, $00, $F8, $FC
        .byte $FC, $FE, $FE, $FE, $F2, $EC, $1E, $00
; slot 09/13 of "Overworld" row -- tent
world_tent_pat:
        .byte $00, $00, $00, $03, $07, $0F, $0F, $1F
        .byte $1E, $1C, $1C, $1C, $1D, $03, $07, $00
        .byte $00, $00, $00, $C0, $E0, $F0, $F0, $F8
        .byte $78, $38, $38, $38, $B8, $C0, $E0, $00
; slot 10/13 of "Overworld" row -- hut
world_hut_pat:
        .byte $00, $00, $00, $01, $02, $05, $0B, $17
        .byte $2F, $1F, $12, $12, $1D, $03, $07, $00
        .byte $00, $00, $00, $90, $50, $B0, $D0, $E8
        .byte $F4, $F8, $48, $48, $B8, $C0, $E0, $00
; slot 11/13 of "Overworld" row -- castle
world_castle_pat:
        .byte $00, $00, $00, $2D, $3F, $3F, $00, $1F
        .byte $17, $16, $1C, $1C, $1D, $03, $07, $00
        .byte $00, $00, $00, $B4, $FC, $FC, $00, $F8
        .byte $E8, $68, $38, $38, $B8, $C0, $E0, $00
; slot 12/13 of "Overworld" row -- dock
world_dock_pat:
        .byte $00, $00, $00, $7F, $00, $08, $08, $00
        .byte $7F, $0F, $0E, $0F, $01, $01, $00, $00
        .byte $00, $00, $00, $FE, $02, $02, $02, $02
        .byte $FE, $1E, $EE, $1E, $50, $10, $E0, $00
; slot 13/13 of "Overworld" row -- boat
world_boat_pat:
        .byte $00, $02, $01, $03, $07, $37, $67, $47
        .byte $46, $44, $61, $7E, $7F, $3F, $00, $00
        .byte $00, $80, $80, $80, $80, $A0, $90, $08
        .byte $06, $0C, $1C, $F8, $F0, $E0, $00, $00
