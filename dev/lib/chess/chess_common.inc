; ============================================================================
; chess_common.inc -- equates and macros shared across all Chess variants
; ============================================================================
; Drop-in include for Apple-1 Chess (text / TMS9918 / HGR variants).
;
; Provides:
;   - 0x88 board layout equates and macros
;   - Piece/colour encoding constants
;   - Move encoding (3 bytes: from, to, flags)
;   - Castling-rights bit flags
;   - Material values for evaluation
;
; Idempotent. Caller must `.include "apple1.inc"` for $D010/$D011/$FFEF.
; ============================================================================

.ifndef _CHESS_COMMON_INC_LOADED_
_CHESS_COMMON_INC_LOADED_ = 1

; --- Board layout ----------------------------------------------------------
; 0x88 mailbox: index 0..127, valid squares are those where (idx & $88)==0.
; sq = rank*16 + file → A1=$00, H1=$07, A8=$70, H8=$77
; Off-board test:  TXA / AND #$88 / BNE off_board   (3 cycles)

OFFBOARD_MASK = $88

; File and rank base indices (used by parser to convert algebraic to 0x88).
FILE_A = $00
FILE_H = $07
RANK_1 = $00
RANK_8 = $70

; --- Piece encoding (1 byte per square) ------------------------------------
; bit 7    = colour (0=white, 1=black)
; bits 3-0 = piece type
PIECE_NONE   = $0
PIECE_PAWN   = $1
PIECE_KNIGHT = $2
PIECE_BISHOP = $3
PIECE_ROOK   = $4
PIECE_QUEEN  = $5
PIECE_KING   = $6
PIECE_MASK   = $07          ; isolate piece type
COLOR_BLACK  = $80
COLOR_MASK   = $80

; Convenient pre-built piece codes
WP = PIECE_PAWN
WN = PIECE_KNIGHT
WB = PIECE_BISHOP
WR = PIECE_ROOK
WQ = PIECE_QUEEN
WK = PIECE_KING
BP = PIECE_PAWN   | COLOR_BLACK
BN = PIECE_KNIGHT | COLOR_BLACK
BB = PIECE_BISHOP | COLOR_BLACK
BR_ = PIECE_ROOK   | COLOR_BLACK   ; BR conflicts with the BR mnemonic on some asms
BQ = PIECE_QUEEN  | COLOR_BLACK
BK = PIECE_KING   | COLOR_BLACK

; --- Move encoding (3 bytes per move in move_list) -------------------------
; +0  from-square (0..127, 0x88)
; +1  to-square   (0..127, 0x88)
; +2  flags:
;       bits 0-2  promotion piece (1-6 = PNBRQK, 0 = none)
;       bit  3    capture
;       bit  4    double pawn push
;       bit  5    en passant
;       bit  6    short castle (king side)
;       bit  7    long  castle (queen side)
MOVE_BYTES = 3

MV_FLAG_PROMO_MASK  = $07
MV_FLAG_CAPTURE     = $08
MV_FLAG_DBLPUSH     = $10
MV_FLAG_ENPASSANT   = $20
MV_FLAG_CASTLE_K    = $40
MV_FLAG_CASTLE_Q    = $80

; --- Castling rights bitmap (single byte, BSS castling_rights) -------------
CR_WHITE_K = $01
CR_WHITE_Q = $02
CR_BLACK_K = $04
CR_BLACK_Q = $08

; --- Material values (1 byte signed, scaled down so Q+R+R+... fits) --------
; Values divided by 10 vs classic (P=10, N=32, B=33, R=50, Q=90, K=127)
; so total material fits inside 8-bit accumulator for fast eval.
MAT_PAWN   = 10
MAT_KNIGHT = 32
MAT_BISHOP = 33
MAT_ROOK   = 50
MAT_QUEEN  = 90
MAT_KING   = 127            ; pseudo-infinity for king (never captured legally)

; --- Side codes ------------------------------------------------------------
SIDE_WHITE = $00
SIDE_BLACK = $80

; --- Error codes returned by apply_user_move ------------------------------
ERR_EMPTY        = 1
ERR_WRONG_COLOR  = 2
ERR_BAD_GEOMETRY = 3
ERR_KING_IN_CHECK= 4
ERR_NOT_IMPL     = 5

; --- BSS layout ------------------------------------------------------------
; A 256-byte BOARDST segment is reserved by the linker config (one of
; $1F00, $4000, or $5F00 depending on variant). chess_engine.asm declares
; the labels via .segment "BOARDST" + .res. Reference here for documentation:
;
;   board[0..127]          0x88 mailbox (only even-half occupied)
;   side_to_move           current side ($00 / $80)
;   castling_rights        bitmap of CR_* flags
;   ep_square              en-passant target square ($88 = none)
;   halfmove_clock         since last pawn move / capture
;   fullmove_number        increments after each Black move
;   king_sq_white          for fast in-check
;   king_sq_black
;   ply_index              current ply during search (0 = root)
;   move_count_per_ply[16] number of pseudo-legal moves at each ply
;   history[8 * 6]         undo entries (captured, ep, castling, halfmove,
;                          king_sq_white, king_sq_black) per ply
;
; The search-stack 4 KB region (SEARCHBUF) holds move_list[ply][i] arrays.

.endif  ; _CHESS_COMMON_INC_LOADED_
