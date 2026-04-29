; ============================================================================
; chess_tables.inc -- precomputed tables for move generation and evaluation
; ============================================================================
; Drop-in include after chess_common.inc. Pure data, no code.
;
; Tables provided:
;   - knight_offsets, king_offsets, rook_offsets, bishop_offsets
;     (8 / 8 / 4 / 4 signed bytes added to a 0x88 square)
;   - material_table[piece_type] (indexed 0..6)
;   - starting_position[64] (rank 1 .. rank 8 in 0x88 layout)
; ============================================================================

.ifndef _CHESS_TABLES_INC_LOADED_
_CHESS_TABLES_INC_LOADED_ = 1

; --- Direction offsets (signed 8-bit added to 0x88 square) -----------------
; Knight: 8 L-shaped jumps
; Format suitable for `LDA tbl,X` then `CLC / ADC sq`.
knight_offsets:
        .byte $1F, $21, $0E, $12, $E1, $DF, $F2, $EE
        ; +31 +33 +14 +18 -31 -33 -14 -18  (signed)

; King: 8 adjacent squares (also queen step pattern)
king_offsets:
        .byte $10, $11, $01, $F1, $F0, $EF, $FF, $0F
        ; N  NE  E  SE  S  SW  W  NW

; Rook: 4 cardinal directions
rook_offsets:
        .byte $10, $01, $F0, $FF
        ; N  E  S  W

; Bishop: 4 diagonal directions
bishop_offsets:
        .byte $11, $F1, $EF, $0F
        ; NE  SE  SW  NW

; Queen uses king_offsets (same 8 directions, but slides).
; Pawn moves are inlined in chess_engine (asymmetric per colour).

; --- Material values indexed by piece type (0..6) --------------------------
; material_table[PIECE_NONE] = 0
material_table:
        .byte 0, MAT_PAWN, MAT_KNIGHT, MAT_BISHOP, MAT_ROOK, MAT_QUEEN, MAT_KING, 0

; --- Starting position ---------------------------------------------------
; 64 entries in rank-major order: rank 1, rank 2, ..., rank 8
; Used by init_board to populate the 0x88 board.
starting_position:
        ; Rank 1 (white back rank)
        .byte WR, WN, WB, WQ, WK, WB, WN, WR
        ; Rank 2 (white pawns)
        .byte WP, WP, WP, WP, WP, WP, WP, WP
        ; Rank 3..6 (empty)
        .byte 0, 0, 0, 0, 0, 0, 0, 0
        .byte 0, 0, 0, 0, 0, 0, 0, 0
        .byte 0, 0, 0, 0, 0, 0, 0, 0
        .byte 0, 0, 0, 0, 0, 0, 0, 0
        ; Rank 7 (black pawns)
        .byte BP, BP, BP, BP, BP, BP, BP, BP
        ; Rank 8 (black back rank)
        .byte BR_, BN, BB, BQ, BK, BB, BN, BR_

; --- ASCII glyph per piece (text variant + status display) -----------------
; Indexed by piece_type 0..6. Caller adjusts case based on colour bit:
;   white piece -> uppercase as-is
;   black piece -> + $20 to lowercase (then ORA #$80 for ECHO)
piece_letters:
        .byte ' ', 'P', 'N', 'B', 'R', 'Q', 'K', '?'

.endif  ; _CHESS_TABLES_INC_LOADED_
