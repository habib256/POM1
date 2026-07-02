; ============================================================================
; sprites_building.asm  --  23 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Building" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export bldg_brick_wall_pat, bldg_brick_wall2_pat, bldg_brick_wall3_pat, bldg_cobble_pat, bldg_cobble2_pat
.export bldg_cobble3_pat, bldg_window_pat, bldg_window_grate_pat, bldg_door_locked_pat, bldg_door_pat
.export bldg_stairs_down_pat, bldg_stairs_up_pat, bldg_door_wood_pat, bldg_pillar_pat, bldg_rubble_pat
.export bldg_dome_pat, bldg_tent_pat, bldg_signpost_pat, bldg_stool_pat, bldg_bench_pat
.export bldg_shelf_pat, bldg_bridge_pat, bldg_arch_pat

.segment "CODE"

; slot 01/23 of "Building" row -- brick_wall
bldg_brick_wall_pat:
        .byte $DF, $DF, $DF, $DF, $DF, $00, $FD, $FD
        .byte $FD, $FD, $FD, $FD, $FD, $00, $DF, $DF
        .byte $DF, $DF, $DF, $DF, $DF, $00, $FD, $FD
        .byte $FD, $FD, $FD, $FD, $FD, $00, $DF, $DF
; slot 02/23 of "Building" row -- brick_wall2
bldg_brick_wall2_pat:
        .byte $DF, $DF, $DF, $DF, $DF, $00, $DD, $DD
        .byte $DD, $C0, $DD, $DD, $DD, $00, $DF, $DF
        .byte $DF, $DF, $DF, $DF, $DF, $00, $FD, $FD
        .byte $FD, $01, $DD, $DD, $DD, $00, $DF, $DF
; slot 03/23 of "Building" row -- brick_wall3
bldg_brick_wall3_pat:
        .byte $DF, $DF, $DF, $DF, $DF, $00, $FC, $FC
        .byte $FC, $FC, $FC, $FC, $FC, $00, $DF, $DF
        .byte $DF, $DF, $DF, $DF, $DF, $00, $01, $FD
        .byte $FD, $FD, $FD, $FD, $FD, $00, $DF, $DF
; slot 04/23 of "Building" row -- cobble
bldg_cobble_pat:
        .byte $C9, $81, $00, $0C, $9C, $18, $01, $83
        .byte $CB, $C3, $01, $18, $BC, $3C, $18, $C0
        .byte $BD, $99, $00, $C0, $CC, $0C, $C0, $E1
        .byte $E9, $E0, $C0, $18, $D8, $C0, $18, $3C
; slot 05/23 of "Building" row -- cobble2
bldg_cobble2_pat:
        .byte $C9, $81, $1C, $3E, $BE, $3E, $1C, $87
        .byte $CF, $DF, $1F, $1F, $9F, $1F, $0F, $C0
        .byte $BD, $99, $00, $18, $3C, $3C, $18, $C1
        .byte $E1, $E0, $EC, $EE, $E6, $C0, $98, $3C
; slot 06/23 of "Building" row -- cobble3
bldg_cobble3_pat:
        .byte $DF, $BF, $7F, $7F, $7F, $1F, $EF, $E0
        .byte $DF, $3F, $7F, $7F, $7F, $3F, $1F, $C0
        .byte $3D, $BD, $FC, $D8, $C6, $86, $38, $7D
        .byte $7D, $BC, $D8, $E3, $E7, $E6, $D8, $3C
; slot 07/23 of "Building" row -- window
bldg_window_pat:
        .byte $FF, $80, $BF, $BF, $B0, $B0, $B3, $B3
        .byte $B3, $B3, $B3, $B0, $BF, $BF, $80, $FF
        .byte $FF, $01, $FD, $FD, $0D, $0D, $ED, $ED
        .byte $ED, $ED, $ED, $0D, $FD, $FD, $01, $FF
; slot 08/23 of "Building" row -- window_grate
bldg_window_grate_pat:
        .byte $FF, $80, $BF, $A0, $A0, $A2, $A5, $A2
        .byte $A5, $A2, $A5, $A2, $A0, $BF, $80, $FF
        .byte $FF, $01, $FD, $05, $05, $A5, $55, $A5
        .byte $55, $A5, $55, $A5, $05, $FD, $01, $FF
; slot 09/23 of "Building" row -- door_locked
bldg_door_locked_pat:
        .byte $FF, $80, $BF, $9F, $8F, $87, $83, $81
        .byte $81, $82, $84, $88, $90, $A0, $80, $FF
        .byte $FF, $01, $FD, $F9, $F1, $E1, $C1, $81
        .byte $81, $41, $21, $11, $09, $05, $01, $FF
; slot 10/23 of "Building" row -- door
bldg_door_pat:
        .byte $FF, $80, $BF, $B8, $BB, $BA, $BA, $BA
        .byte $BA, $BA, $BB, $B8, $B8, $BF, $80, $FF
        .byte $FF, $01, $FD, $1D, $9D, $9D, $9D, $9D
        .byte $9D, $9D, $9D, $1D, $1D, $FD, $01, $FF
; slot 11/23 of "Building" row -- stairs going DOWN (mis-labelled "house"
; in the Quale source — the sprite shows a tile with a stepped descent
; into the floor, not a building exterior; sister sprite
; bldg_stairs_up_pat (slot 12) covers ascending stairs).
bldg_stairs_down_pat:
        .byte $00, $7F, $40, $40, $40, $40, $40, $40
        .byte $43, $43, $43, $7B, $7B, $7B, $7F, $00
        .byte $00, $FE, $02, $02, $02, $1E, $1E, $1E
        .byte $DE, $DE, $DE, $DE, $DE, $DE, $FE, $00
; slot 12/23 of "Building" row -- stairs going UP (renamed from
; bldg_stairs_pat for clarity now that the sister sprite at slot 11
; is bldg_stairs_down_pat). Steps ascend toward the right side, so
; the consumer should anchor the right side against a wall.
bldg_stairs_up_pat:
        .byte $00, $00, $00, $03, $03, $03, $7B, $7B
        .byte $7B, $7B, $78, $78, $40, $40, $7F, $00
        .byte $1E, $1E, $1E, $DE, $DE, $DE, $DE, $DE
        .byte $C2, $C2, $02, $02, $02, $02, $FE, $00
; slot 13/23 of "Building" row -- wooden door (was mis-labelled "battlement"
; in the Quale source — the sprite actually depicts a planked wooden door
; with iron bands; the keep-wall battlement sprite isn't in this set).
bldg_door_wood_pat:
        .byte $00, $0B, $3B, $7B, $7B, $7B, $7B, $7B
        .byte $4F, $30, $4B, $7B, $7B, $00, $7F, $00
        .byte $00, $D0, $DC, $DE, $DE, $DE, $DE, $DE
        .byte $FE, $00, $DE, $DE, $DE, $00, $FE, $00
; slot 14/23 of "Building" row -- pillar
bldg_pillar_pat:
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $0C, $1E, $1E, $1E, $0E, $16, $1A
        .byte $1C, $1E, $0E, $16, $0A, $04, $02, $00
; slot 15/23 of "Building" row -- rubble
bldg_rubble_pat:
        .byte $00, $08, $02, $03, $07, $07, $0D, $0E
        .byte $18, $18, $2F, $20, $3F, $1F, $20, $1F
        .byte $00, $40, $60, $20, $80, $E0, $70, $A8
        .byte $08, $18, $F4, $04, $FC, $F8, $04, $F8
; slot 16/23 of "Building" row -- dome
bldg_dome_pat:
        .byte $00, $0F, $10, $21, $42, $C3, $C1, $C0
        .byte $E0, $B0, $DF, $6F, $30, $1F, $0F, $00
        .byte $00, $F0, $08, $84, $42, $C3, $83, $03
        .byte $07, $0D, $FB, $F6, $0C, $F8, $F0, $00
; slot 17/23 of "Building" row -- tent
bldg_tent_pat:
        .byte $00, $00, $07, $0F, $1F, $1C, $1F, $18
        .byte $1F, $1F, $1F, $1F, $1B, $16, $00, $00
        .byte $00, $00, $E0, $F0, $F8, $38, $F8, $18
        .byte $F8, $F8, $F8, $F8, $78, $68, $00, $00
; slot 18/23 of "Building" row -- signpost
bldg_signpost_pat:
        .byte $00, $01, $7F, $7F, $60, $7F, $70, $7F
        .byte $7F, $7F, $00, $01, $01, $01, $01, $00
        .byte $00, $80, $FE, $FE, $06, $FE, $0E, $FE
        .byte $FE, $FE, $00, $80, $80, $80, $80, $00
; slot 19/23 of "Building" row -- stool
bldg_stool_pat:
        .byte $00, $00, $00, $00, $00, $07, $0F, $0F
        .byte $07, $00, $06, $0C, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $E0, $F0, $F0
        .byte $E0, $00, $60, $30, $00, $00, $00, $00
; slot 20/23 of "Building" row -- bench
bldg_bench_pat:
        .byte $00, $00, $7F, $7F, $7F, $7F, $7F, $7F
        .byte $00, $7F, $5D, $5F, $5F, $40, $00, $00
        .byte $00, $00, $FE, $FE, $FE, $FE, $FE, $FE
        .byte $00, $FE, $5E, $7E, $7E, $02, $00, $00
; slot 21/23 of "Building" row -- shelf
bldg_shelf_pat:
        .byte $00, $7F, $7F, $7F, $40, $54, $55, $7F
        .byte $40, $5A, $5A, $7F, $40, $5E, $5E, $7F
        .byte $00, $FE, $FE, $FE, $02, $02, $82, $FE
        .byte $02, $BA, $BA, $FE, $02, $AA, $AA, $FE
; slot 22/23 of "Building" row -- bridge
bldg_bridge_pat:
        .byte $00, $3F, $7F, $7F, $7F, $7F, $7F, $7F
        .byte $3F, $40, $3F, $00, $30, $30, $30, $00
        .byte $00, $FC, $FE, $FE, $FE, $FE, $FE, $FE
        .byte $FC, $02, $FC, $00, $0C, $0C, $0C, $00
; slot 23/23 of "Building" row -- arch
bldg_arch_pat:
        .byte $00, $07, $37, $37, $20, $1F, $3F, $3F
        .byte $3F, $3F, $00, $3F, $00, $30, $30, $00
        .byte $00, $E0, $EC, $EC, $04, $F8, $FC, $FC
        .byte $FC, $FC, $00, $FC, $00, $0C, $0C, $00
