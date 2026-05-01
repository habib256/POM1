; ===== Expression emotes (12x 16x16, from SCROLL-O-SPRITES by Quale, CC-BY-3.0) =====
; Source: pic/undefined - Imgur.png  -- tools/extract_scroll_expressions.py
;
; --- shape_table entries (paste before the $FF terminator) ---
        .byte "NORMAL"
        .byte 32
        .word normal_pat
        .byte "HAPPY "
        .byte 32
        .word happy_pat
        .byte "SUPER "
        .byte 32
        .word super_pat
        .byte "SAD   "
        .byte 32
        .word sad_pat
        .byte "UPSET "
        .byte 32
        .word upset_pat
        .byte "ANGRY "
        .byte 32
        .word angry_pat
        .byte "GRUMPY"
        .byte 32
        .word grumpy_pat
        .byte "PERV  "
        .byte 32
        .word perv_pat
        .byte "SICK  "
        .byte 32
        .word sick_pat
        .byte "SLEEP "
        .byte 32
        .word sleep_pat
        .byte "PIRATE"
        .byte 32
        .word pirate_pat
        .byte "SHADES"
        .byte 32
        .word shades_pat

; --- pattern data (place near the other 16x16 patterns) ---
; NORMAL -- neutral / default expression
normal_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $63, $77
        .byte $77, $7F, $7C, $7F, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $E2, $F6
        .byte $F6, $FE, $0E, $FE, $FC, $F8, $00, $00
; HAPPY -- happy
happy_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $63, $77
        .byte $77, $7F, $7D, $7E, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $E2, $F6
        .byte $F6, $FE, $EE, $1E, $FC, $F8, $00, $00
; SUPER -- super happy, big open mouth
super_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $77, $77
        .byte $77, $7F, $7C, $7C, $3E, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $FA, $FA
        .byte $FA, $FE, $0E, $0E, $1C, $F8, $00, $00
; SAD -- sad / frown
sad_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $73, $67
        .byte $77, $7F, $7E, $7D, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $E6, $F2
        .byte $F6, $FE, $1E, $EE, $FC, $F8, $00, $00
; UPSET -- upset / disappointed
upset_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $67, $73
        .byte $67, $7F, $7E, $7C, $3C, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $F2, $E6
        .byte $F2, $FE, $1E, $0E, $0C, $F8, $00, $00
; ANGRY -- angry, frowning brows
angry_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $67, $73
        .byte $77, $7F, $7E, $7D, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $F2, $E6
        .byte $F6, $FE, $1E, $EE, $FC, $F8, $00, $00
; GRUMPY -- grumpy, tongue out
grumpy_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $67, $73
        .byte $77, $7F, $7E, $7C, $3C, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $F2, $E6
        .byte $F6, $FE, $0E, $0E, $1C, $F8, $00, $00
; PERV -- pervy / lewd
perv_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $7F, $63
        .byte $6F, $7F, $7E, $7F, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $FE, $E2
        .byte $EE, $FE, $FE, $3E, $FC, $F8, $00, $00
; SICK -- queasy / about to throw up (X eyes)
sick_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $7F, $7B
        .byte $7F, $7E, $7E, $7E, $3D, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $FE, $EE
        .byte $FE, $DE, $1E, $DE, $EC, $F8, $00, $00
; SLEEP -- asleep
sleep_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $7F, $7B
        .byte $67, $7F, $7F, $7F, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $FE, $FA
        .byte $E6, $FE, $3E, $FE, $FC, $F8, $00, $00
; PIRATE -- pirate (one eye shut)
pirate_pat:
        .byte $00, $00, $0F, $33, $7C, $7F, $67, $73
        .byte $77, $7F, $7C, $7F, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $3C, $C2, $C2
        .byte $E2, $FE, $0E, $FE, $FC, $F8, $00, $00
; SHADES -- wearing shades / sunglasses
shades_pat:
        .byte $00, $00, $1F, $3F, $7F, $7F, $00, $6E
        .byte $6E, $71, $7F, $7F, $3F, $1F, $00, $00
        .byte $00, $00, $F8, $FC, $FE, $FE, $00, $DC
        .byte $DC, $E2, $3E, $FE, $FC, $F8, $00, $00
