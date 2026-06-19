; ============================================================================
; subbyte4.inc -- 7-phase sub-byte mask LUTs for 4-pixel-wide HGR blocks
; ============================================================================
; HGR's 7-pixels-per-byte layout means a 4-pixel-wide block straddles a
; byte boundary 3 cases out of 7 (gx%7 = 1, 3, 5). The LUTs below encode,
; for each grid column gx in [0..68] (covering 280 ÷ 4 = 70 columns
; addressing 280 px), the byte offset into the scanline + the OR-mask
; for that byte and (where applicable) the next byte.
;
; Pattern repeats every 7 grid columns (4 pixels × 7 columns = 28 pixels
; = 4 bytes wide). The 7-phase table:
;
;   gx%7  byte+0  mask1   mask2
;   0     +0      $0F     $00
;   1     +0      $70     $01
;   2     +1      $1E     $00
;   3     +1      $60     $03
;   4     +2      $3C     $00
;   5     +2      $40     $07
;   6     +3      $78     $00
;
; Bit 7 of every byte stays clear — that bit is the NTSC group selector
; (violet/green vs blue/orange in HGR colour mode), not a pixel. Code
; that wants colour control should set bit 7 separately on each byte.
;
; Idempotent: outer .ifndef guard.
;
; Lifted out of sketchs/gen2/game_maze/HGR_Maze.asm:711-749 (TODO6502 §HGR
; sub-byte sprite library). To use a different block width, copy this
; file as a template — the structure is the same but the mask values
; change.
; ============================================================================

.ifndef _SUBBYTE4_INC_LOADED_
_SUBBYTE4_INC_LOADED_ = 1

; --- Byte offset within the scanline for grid column gx (0..68) ------------
sb4_byte_off:
        .byte    0,  0,  1,  1,  2,  2,  3        ; gx 0-6
        .byte    4,  4,  5,  5,  6,  6,  7        ; gx 7-13
        .byte    8,  8,  9,  9, 10, 10, 11        ; gx 14-20
        .byte   12, 12, 13, 13, 14, 14, 15        ; gx 21-27
        .byte   16, 16, 17, 17, 18, 18, 19        ; gx 28-34
        .byte   20, 20, 21, 21, 22, 22, 23        ; gx 35-41
        .byte   24, 24, 25, 25, 26, 26, 27        ; gx 42-48
        .byte   28, 28, 29, 29, 30, 30, 31        ; gx 49-55
        .byte   32, 32, 33, 33, 34, 34, 35        ; gx 56-62
        .byte   36, 36, 37, 37, 38, 38            ; gx 63-68

; --- OR-mask for the first byte at byte_off ---------------------------------
sb4_mask1:
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 0-6
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 7-13
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 14-20
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 21-27
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 28-34
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 35-41
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 42-48
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 49-55
        .byte   $0F,$70,$1E,$60,$3C,$40,$78       ; gx 56-62
        .byte   $0F,$70,$1E,$60,$3C,$40           ; gx 63-68

; --- OR-mask for the second byte (0 = no straddle) --------------------------
sb4_mask2:
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 0-6
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 7-13
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 14-20
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 21-27
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 28-34
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 35-41
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 42-48
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 49-55
        .byte   $00,$01,$00,$03,$00,$07,$00       ; gx 56-62
        .byte   $00,$01,$00,$03,$00,$07           ; gx 63-68

.endif  ; _SUBBYTE4_INC_LOADED_
