; ============================================================================
; sprites_traps_devices.asm  --  12 sprites (16x16, TMS9918 sprite mode)
; derived: dev/lib/gen2/sprites/sprites_traps_devices_hgr.asm -- after editing this master rerun:
;   python3 tools/build_hgr_sprites.py --only traps_devices
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Traps & Devices" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export trap_grate_pat, trap_web_pat, trap_beartrap_ready_pat, trap_beartrap_used_pat, trap_trap_ready_pat
.export trap_trap_used_pat, trap_trapdoor_ready_pat, trap_trapdoor_used_pat, trap_button_ready_pat, trap_button_used_pat
.export trap_switch_left_pat, trap_switch_right_pat

.segment "CODE"

; slot 01/12 of "Traps & Devices" row -- grate  (was: net)
trap_grate_pat:
        .byte $00, $02, $12, $3F, $12, $12, $77, $12
        .byte $12, $7F, $12, $12, $3F, $12, $02, $00
        .byte $00, $40, $48, $FC, $48, $48, $FE, $48
        .byte $08, $1E, $08, $48, $FC, $48, $40, $00
; slot 02/12 of "Traps & Devices" row -- web
trap_web_pat:
        .byte $00, $10, $0B, $14, $12, $75, $2C, $2B
        .byte $25, $29, $16, $23, $5C, $07, $04, $00
        .byte $00, $20, $E0, $3A, $C4, $68, $94, $A4
        .byte $D4, $34, $AE, $48, $28, $D0, $08, $00
; slot 03/12 of "Traps & Devices" row -- beartrap_ready  (was: spikes)
trap_beartrap_ready_pat:
        .byte $00, $00, $00, $00, $00, $00, $07, $00
        .byte $55, $7E, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $E0, $00
        .byte $AA, $7E, $00, $00, $00, $00, $00, $00
; slot 04/12 of "Traps & Devices" row -- beartrap_used  (was: caltrops)
trap_beartrap_used_pat:
        .byte $00, $00, $00, $00, $02, $03, $02, $03
        .byte $02, $03, $00, $01, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $C0, $40, $C0, $40
        .byte $C0, $40, $00, $80, $00, $00, $00, $00
; slot 05/12 of "Traps & Devices" row -- trap_ready  (was: dart)
trap_trap_ready_pat:
        .byte $00, $00, $00, $00, $02, $06, $06, $00
        .byte $00, $04, $0C, $0C, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $10, $30, $30, $00
        .byte $00, $20, $60, $60, $00, $00, $00, $00
; slot 06/12 of "Traps & Devices" row -- trap_used  (was: pit)
trap_trap_used_pat:
        .byte $00, $3F, $60, $40, $42, $46, $46, $40
        .byte $40, $44, $4C, $4C, $40, $60, $3F, $00
        .byte $00, $FC, $06, $02, $12, $32, $32, $02
        .byte $02, $22, $62, $62, $02, $06, $FC, $00
; slot 07/12 of "Traps & Devices" row -- trapdoor_ready  (was: grate)
trap_trapdoor_ready_pat:
        .byte $00, $3F, $60, $4F, $52, $52, $5F, $52
        .byte $52, $5F, $52, $52, $4F, $60, $3F, $00
        .byte $00, $FC, $06, $F2, $4A, $4A, $FA, $4A
        .byte $4A, $FA, $4A, $4A, $F2, $06, $FC, $00
; slot 08/12 of "Traps & Devices" row -- trapdoor_used  (was: chest_open)
trap_trapdoor_used_pat:
        .byte $00, $3F, $60, $40, $40, $40, $40, $40
        .byte $40, $40, $40, $40, $4F, $60, $3F, $00
        .byte $00, $FC, $06, $02, $02, $02, $02, $02
        .byte $02, $02, $02, $02, $F2, $06, $FC, $00
; slot 09/12 of "Traps & Devices" row -- button_ready  (was: chest)
trap_button_ready_pat:
        .byte $00, $00, $00, $1F, $10, $17, $17, $16
        .byte $16, $17, $17, $10, $1F, $00, $00, $00
        .byte $00, $00, $00, $F8, $08, $E8, $E8, $68
        .byte $E8, $E8, $E8, $08, $F8, $00, $00, $00
; slot 10/12 of "Traps & Devices" row -- button_used  (was: button)
trap_button_used_pat:
        .byte $00, $00, $00, $1F, $10, $10, $13, $13
        .byte $13, $13, $13, $10, $1F, $00, $00, $00
        .byte $00, $00, $00, $F8, $08, $08, $E8, $E8
        .byte $28, $68, $E8, $08, $F8, $00, $00, $00
; slot 11/12 of "Traps & Devices" row -- switch_left  (was: lever_down)
trap_switch_left_pat:
        .byte $00, $00, $00, $60, $60, $10, $08, $04
        .byte $1A, $31, $3F, $3F, $3F, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $F8, $0C, $FC, $FC, $FC, $00, $00, $00
; slot 12/12 of "Traps & Devices" row -- switch_right  (was: lever_up)
trap_switch_right_pat:
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $1F, $30, $3F, $3F, $3F, $00, $00, $00
        .byte $00, $00, $00, $06, $06, $08, $10, $20
        .byte $58, $8C, $FC, $FC, $FC, $00, $00, $00
