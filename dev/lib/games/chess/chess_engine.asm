; ============================================================================
; chess_engine.asm -- core chess engine (separately-assembled module)
; ============================================================================
; Public symbols (see .export at end):
;   init_board          -- reset to starting position, side=white
;   piece_at            -- A = board[X], where X is a 0x88 square
;   apply_user_move     -- attempt the move (mv_from, mv_to, mv_promo)
;                          carry clear = success, carry set + A=err code = fail
;   in_check            -- A = nonzero if side_to_move's king is in check
;   game_status         -- A = 0 ongoing, 1 white-mate, 2 black-mate, 3 stalemate
;   toggle_side         -- side_to_move ^= COLOR_BLACK
;
; Public BSS (importzp / import):
;   board (256 B BSS)   -- 0x88 mailbox, only even-half occupied
;   side_to_move (BSS)  -- $00 white / $80 black
;   castling_rights, ep_square, halfmove_clock, fullmove_number
;   king_sq_white, king_sq_black
;   mv_from, mv_to, mv_promo, mv_flags  (input/output of apply_user_move)
;
; Notes:
;   - Supports: all 6 piece types, captures, promotion to queen, castling
;     (king- and queen-side, with right / occupancy / pass-through-check
;     validation in apply_castle_move) and en-passant capture (ep_square
;     tracking + MV_FLAG_ENPASSANT, removed in make_move).
;   - Check detection IS active: a move that leaves your own king in check
;     is rejected (ERR_KING_IN_CHECK).
;   - Stalemate / checkmate detection runs after every successful move via
;     a brute-force "any pseudo-legal move that leaves king safe" scan.
; ============================================================================

.include "apple1.inc"
.include "chess_common.inc"
.include "chess_tables.inc"

; --- Public API symbols imported by variant code --------------------------
.export init_board
.export piece_at
.export apply_user_move
.export in_check
.export game_status
.export toggle_side
.export starting_position    ; re-export for variants that want to peek
.export piece_letters
; Internals exposed so variants can enumerate legal moves without duplicating
; the engine's machinery (used by Chess.asm's do_list_moves and do_hint).
.export is_pseudo_legal
.export make_move
.export unmake_move

; --- BSS ------------------------------------------------------------------
.segment "BOARDST"
.export board
board:                  .res 128
.export side_to_move
side_to_move:           .res 1
.export castling_rights
castling_rights:        .res 1
.export ep_square
ep_square:              .res 1
.export halfmove_clock
halfmove_clock:         .res 1
.export fullmove_number
fullmove_number:        .res 1
.export king_sq_white
king_sq_white:          .res 1
.export king_sq_black
king_sq_black:          .res 1

; Move I/O (input from parser, output from gen during legality scan).
.export mv_from
mv_from:                .res 1
.export mv_to
mv_to:                  .res 1
.export mv_promo
mv_promo:               .res 1
.export mv_flags
mv_flags:               .res 1

; Saved state for unmake_move (single-level undo only — sufficient for
; HvH and depth=1 search; deepens to a stack in v1.2 when AI lands).
saved_captured:         .res 1      ; piece on `to` before move
saved_ep:               .res 1
saved_castling:         .res 1
saved_halfmove:         .res 1
saved_king_sq:          .res 1      ; king_sq for moving side, if king moved
scan_sq:                .res 1      ; persistent scan-loop pos for is_attacked_runner
saved_castle_rook_from: .res 1      ; rook origin square (for castling undo)
saved_castle_rook_to:   .res 1      ; rook destination square (for castling undo)
castle_passthru_save:   .res 1      ; saves king piece byte during pass-through test
                                    ; (in_check clobbers ce_piece, can't use that)

; --- AI / perft scratch ---
ai_scan_x:              .res 1      ; from-square iterator
ai_scan_y:              .res 1      ; to-square iterator
ai_best_from:           .res 1
ai_best_to:             .res 1
ai_best_flags:          .res 1      ; mv_flags for the best move (0, or a castle bit)
ai_best_score:          .res 1      ; signed 8-bit material score (primary)
ai_best_pos:            .res 1      ; positional score of the best move (tie-break)
cand_pos:               .res 1      ; positional score of the candidate under test
ai_best_mvvlva:         .res 1      ; (unused since v0.6 — kept for layout stability)
score_lo:               .res 1      ; evaluate_material output
perft_count_lo:         .res 1
perft_count_hi:         .res 1

; --- SMART-mode state (LFSR + strategy + SEE scratch) ---
ai_rng:                 .res 1      ; 8-bit LFSR (period 255). Seeded in init_board.
ai_strategy:            .res 1      ; AI_STRATEGY_NAIVE / _SMART
see_min_val:            .res 1      ; lowest attacker value found (find_min_attacker)
see_min_sq:             .res 1      ; that attacker's square
see_value:              .res 1      ; signed 8-bit net gain from see_estimate
see_victim:             .res 1      ; raw victim value (mat_simple lookup) for adjustment
see_mover:              .res 1      ; our piece byte at mv_to (cached for SEE)

; --- 2-ply (negamax) search state. The outer move is preserved across the
;     inner opponent-reply search via a 2nd undo buffer + a move snapshot, so
;     make_move/unmake_move (single-level) can nest exactly one level deep. ---
saved2_captured:        .res 1
saved2_ep:              .res 1
saved2_castling:        .res 1
saved2_halfmove:        .res 1
saved2_king_sq:         .res 1
saved2_crf:             .res 1      ; mirror of saved_castle_rook_from
saved2_crt:             .res 1      ; mirror of saved_castle_rook_to
m_from:                 .res 1      ; outer move snapshot (inner search reuses mv_*)
m_to:                   .res 1
m_flags:                .res 1
m_promo:                .res 1
m_score:                .res 1      ; opponent's best reply eval (their perspective)
best_reply:             .res 1      ; running max inside best_reply_eval
reply_found:            .res 1      ; 1 once a legal opponent reply has been seen
rscan_x:                .res 1      ; inner reply from-square iterator
rscan_y:                .res 1      ; inner reply to-square iterator

; --- Compact undo (16 bytes; relies on unmake_move) ---
; After a successful user move, we copy the saved_* slots (which the engine
; uses for single-level undo) into user_saved_* so they survive subsequent
; game_status iterations. On 'U', restore saved_*, mv_*, side, fullmove
; from user_saved_* and call unmake_move.
user_mv_from:           .res 1
user_mv_to:             .res 1
user_mv_promo:          .res 1
user_mv_flags:          .res 1
user_saved_captured:    .res 1
user_saved_ep:          .res 1
user_saved_castling:    .res 1
user_saved_halfmove:    .res 1
user_saved_king_sq:     .res 1
user_saved_rook_from:   .res 1
user_saved_rook_to:     .res 1
user_saved_side:        .res 1
user_saved_fullmove:    .res 1
undo_avail:             .res 1      ; 1 if undo state is loaded

; --- Zero page scratch (caller-provided via standard zp.inc convention) ---
; chess_engine relies on tmp + tmp2 from lib/apple1/zp.inc.
.importzp tmp, tmp2

; Local engine-only ZP (allocated in caller's ZEROPAGE segment).
.segment "ZEROPAGE"
ce_sq:          .res 1      ; current scan square
ce_dir:         .res 1      ; current direction offset
ce_target:      .res 1      ; target square being tested
ce_piece:       .res 1      ; piece-type at MOVE-GEN's from-square (set by caller)
.exportzp ce_piece          ; do_list_moves loads it before each is_pseudo_legal
ce_color:       .res 1      ; colour byte of moving side
ce_dirs_left:   .res 1      ; direction-table loop counter
ce_dir_ptr:     .res 1      ; index into knight_offsets / king_offsets etc.
ce_match:       .res 1      ; 1 if a pseudo-legal match for (mv_from,mv_to)
attacker_color: .res 1      ; colour byte of attacker (for is_attacked)
attacked_sq:    .res 1      ; square being tested for attack
atk_piece:      .res 1      ; piece byte of CURRENT attacker (separate from ce_piece
                            ; so move-gen callers' ce_piece survives in_check)

.export mv_from, mv_to, mv_promo, mv_flags  ; redeclare for textual cite

; The engine code lives in its own segment so the linker can place it
; in the upper-bank ($E000-$EFFF) on stock 8 KB Apple-1 (Parmegiani's
; standard layout: 4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF). Variant
; renderer code stays in the regular CODE segment in the lower bank.
.segment "ENGINE"

; ============================================================================
; init_board -- reset to starting position
; ============================================================================
; Clears the 0x88 board, copies starting_position[64] into the valid half,
; sets side_to_move=white, full castling rights, no ep square, king
; positions cached.
init_board:
        ; Zero entire 128-byte board first.
        LDX #$00
        TXA
@zlp:   STA board,X
        INX
        CPX #$80
        BNE @zlp

        ; Copy 64 squares from starting_position[] (rank-major) into the
        ; 0x88 board. starting_position[i] -> board[(i & $38) << 1 | (i & 7)]
        ; i.e. rank = i / 8 (0..7), file = i & 7. board_idx = rank*16 + file.
        LDX #$00            ; X = source index 0..63
@cplp:
        TXA
        LSR A
        LSR A
        LSR A               ; A = rank (0..7)
        ASL A
        ASL A
        ASL A
        ASL A               ; A = rank * 16
        STA tmp             ; tmp = rank * 16
        TXA
        AND #$07            ; A = file
        CLC
        ADC tmp             ; A = rank*16 + file
        TAY                 ; Y = 0x88 destination index
        LDA starting_position,X
        STA board,Y
        INX
        CPX #64
        BNE @cplp

        ; Initialise game state.
        LDA #SIDE_WHITE
        STA side_to_move
        LDA #(CR_WHITE_K | CR_WHITE_Q | CR_BLACK_K | CR_BLACK_Q)
        STA castling_rights
        LDA #$88            ; sentinel "no ep square"
        STA ep_square
        LDA #$00
        STA halfmove_clock
        LDA #$01
        STA fullmove_number
        ; King squares: white E1 = $04, black E8 = $74.
        LDA #$04
        STA king_sq_white
        LDA #$74
        STA king_sq_black

        ; Seed the 8-bit LFSR used by SMART-mode tie-break. Seeding only on
        ; init_board (not on every ai_play_move) lets the RNG state diverge
        ; through a game so AvA runs are non-deterministic by move 2-3.
        ; $AC is a fixed odd seed; the period-255 LFSR cycles through every
        ; non-zero value before repeating.
        LDA #$AC
        STA ai_rng

        ; Default strategy: SMART (1-ply + MVV-LVA + SEE + random tie-break).
        ; The 'D' command at the prompt cycles between NAIVE and SMART.
        LDA #AI_STRATEGY_SMART
        STA ai_strategy
        RTS

; ============================================================================
; piece_at -- A = board[X]
; ============================================================================
; Cheap public read. X = 0x88 square. Caller checks (X & $88) first if needed.
piece_at:
        LDA board,X
        RTS

; ============================================================================
; toggle_side -- side_to_move ^= COLOR_BLACK
; ============================================================================
toggle_side:
        LDA side_to_move
        EOR #COLOR_BLACK
        STA side_to_move
        RTS

; ============================================================================
; apply_user_move -- attempt to make the move in (mv_from, mv_to, mv_promo)
; ============================================================================
; Input:
;   mv_from, mv_to: 0x88 squares
;   mv_promo: 0=no promotion, else PIECE_QUEEN/ROOK/BISHOP/KNIGHT
; Output:
;   carry clear = move was legal and applied; side_to_move toggled
;   carry set + A = error code:
;       1  empty source square
;       2  source piece is opponent's
;       3  pseudo-illegal move for this piece type
;       4  move would leave own king in check
;       5  reserved (ERR_NOT_IMPL — no longer returned; en-passant and
;          castling are implemented. Kept for the text-IO error switch.)
; (ERR_* constants now live in chess_common.inc so all TUs see them as
;  immediate compile-time values without 8-bit range warnings.)

apply_user_move:
        ; 0. Castling? Special validation path before generic checks.
        LDA mv_flags
        AND #(MV_FLAG_CASTLE_K | MV_FLAG_CASTLE_Q)
        BEQ @noc
        JMP apply_castle_move
@noc:
        ; 1. Validate from-square is on the board and has a piece of our colour.
        LDA mv_from
        AND #OFFBOARD_MASK
        BEQ @from_ok
        LDA #ERR_BAD_GEOMETRY
        SEC
        RTS
@from_ok:
        LDX mv_from
        LDA board,X
        BNE @has_piece
        LDA #ERR_EMPTY
        SEC
        RTS
@has_piece:
        STA ce_piece                ; full byte (colour + type)
        AND #COLOR_MASK
        CMP side_to_move
        BEQ @color_ok
        LDA #ERR_WRONG_COLOR
        SEC
        RTS
@color_ok:
        ; 2. Validate to-square is on board and not occupied by our own piece.
        LDA mv_to
        AND #OFFBOARD_MASK
        BEQ @to_ok
        LDA #ERR_BAD_GEOMETRY
        SEC
        RTS
@to_ok:
        LDX mv_to
        LDA board,X
        BEQ @dest_empty
        AND #COLOR_MASK
        CMP side_to_move
        BNE @dest_empty
        ; same colour on destination → illegal
        LDA #ERR_BAD_GEOMETRY
        SEC
        RTS
@dest_empty:

        ; 3. Pseudo-legal geometry check (does THIS piece type allow from→to?)
        JSR is_pseudo_legal
        BCC @geom_ok
        LDA #ERR_BAD_GEOMETRY
        SEC
        RTS
@geom_ok:
        ; 4. Make the move tentatively, check own-king-not-in-check, undo if so.
        JSR make_move
        JSR in_check
        BEQ @move_safe
        ; King left in check → undo and reject.
        JSR unmake_move
        LDA #ERR_KING_IN_CHECK
        SEC
        RTS
@move_safe:
        ; Move is legal. Snapshot for user-undo BEFORE toggling side
        ; (save_user_state captures side_to_move = mover, fullmove pre-bump).
        JSR save_user_state
        JSR toggle_side
        ; Bump fullmove if it's now white's turn (we just played black).
        LDA side_to_move
        BNE @done
        INC fullmove_number
@done:
        CLC
        RTS

; ============================================================================
; apply_castle_move -- validate and execute a castling move
; ============================================================================
; mv_from, mv_to set by parser to king's start and destination.
; mv_flags has CASTLE_K or CASTLE_Q.
;
; Validation:
;   1. Right granted in castling_rights for this side+side
;   2. Squares between king and rook are empty
;   3. King not currently in check
;   4. King does not pass through an attacked square (intermediate)
;   5. King's destination is not attacked (handled by post-move in_check)
; castle_try -- validate the castling move in mv_flags (CASTLE_K/Q) + mv_from/
; mv_to and, on success, MAKE it on the board WITHOUT the permanent bookkeeping
; (no save_user_state / toggle_side / fullmove bump). CC = legal and now made —
; the caller must either finalise it (apply_castle_move) or revert it with
; unmake_move (the move enumerators ai_play_move / game_status / perft1). CS =
; illegal, A = error code, board left unchanged. Factored out of the old
; apply_castle_move so the AI can actually consider castling as a candidate.
castle_try:
        ; --- 1. Right granted? ---
        LDA mv_flags
        AND #MV_FLAG_CASTLE_K
        BEQ @qs_check
        ; Kingside requested
        LDA side_to_move
        BNE @ks_b
        LDA castling_rights
        AND #CR_WHITE_K
        BEQ @no_right
        JMP @check_squares
@ks_b:  LDA castling_rights
        AND #CR_BLACK_K
        BEQ @no_right
        JMP @check_squares
@qs_check:
        LDA side_to_move
        BNE @qs_b
        LDA castling_rights
        AND #CR_WHITE_Q
        BEQ @no_right
        JMP @check_squares
@qs_b:  LDA castling_rights
        AND #CR_BLACK_Q
        BEQ @no_right
@no_right:
        LDA #ERR_BAD_GEOMETRY
        SEC
        RTS

@check_squares:
        ; --- 2. Squares between king and rook are empty ---
        LDA mv_flags
        AND #MV_FLAG_CASTLE_K
        BEQ @qs_squares
        ; KS: check mv_from+1 and mv_from+2 (F and G files)
        LDX mv_from
        INX
        LDA board,X
        BNE @blocked
        INX
        LDA board,X
        BNE @blocked
        JMP @check_in_check
@qs_squares:
        ; QS: check mv_from-1, -2, -3 (D, C, B files)
        LDX mv_from
        DEX
        LDA board,X
        BNE @blocked
        DEX
        LDA board,X
        BNE @blocked
        DEX
        LDA board,X
        BNE @blocked
        JMP @check_in_check
@blocked:
        LDA #ERR_BAD_GEOMETRY
        SEC
        RTS

@check_in_check:
        ; --- 3. King not currently in check ---
        JSR in_check
        BEQ @check_passthru
        LDA #ERR_KING_IN_CHECK
        SEC
        RTS

@check_passthru:
        ; --- 4. King does not pass through attacked square ---
        ; intermediate = (mv_from + mv_to) / 2  (since they're 2 apart on same rank)
        LDA mv_from
        CLC
        ADC mv_to
        LSR A
        STA ce_target           ; intermediate square

        ; Save the king piece byte in a BSS slot (NOT ce_piece — in_check
        ; clobbers ce_piece while iterating attackers).
        LDX mv_from
        LDA board,X
        STA castle_passthru_save
        LDA #$00
        STA board,X
        LDX ce_target
        LDA castle_passthru_save
        STA board,X

        LDA side_to_move
        BNE @pt_b
        LDA king_sq_white
        STA tmp
        LDA ce_target
        STA king_sq_white
        JSR in_check
        STA tmp2
        LDA tmp
        STA king_sq_white
        JMP @pt_restore
@pt_b:
        LDA king_sq_black
        STA tmp
        LDA ce_target
        STA king_sq_black
        JSR in_check
        STA tmp2
        LDA tmp
        STA king_sq_black
@pt_restore:
        ; Restore board: clear intermediate, put king back at mv_from.
        LDX ce_target
        LDA #$00
        STA board,X
        LDX mv_from
        LDA castle_passthru_save
        STA board,X
        ; Decision
        LDA tmp2
        BEQ @do_castle_for_real
        LDA #ERR_KING_IN_CHECK
        SEC
        RTS

@do_castle_for_real:
        ; --- 5. Execute the castle (make_move handles both pieces) ---
        JSR make_move
        ; Verify final king position not in check (paranoia — squares should
        ; have been cleared by checks above, but a sliding attack on the
        ; destination is still possible if the rook itself was in the way).
        JSR in_check
        BEQ @castle_safe
        JSR unmake_move
        LDA #ERR_KING_IN_CHECK
        SEC
        RTS
@castle_safe:
        CLC                     ; castle is legal and now made on the board
        RTS

; apply_castle_move -- the real (user/AI) castling move: validate+make via
; castle_try, then the permanent bookkeeping. Entered from apply_user_move.
apply_castle_move:
        JSR castle_try
        BCS @cfail              ; illegal — A already holds the error, CS set
        JSR save_user_state
        JSR toggle_side
        LDA side_to_move
        BNE @cdone
        INC fullmove_number
@cdone:
        CLC
@cfail:
        RTS

; try_one_castle -- A = MV_FLAG_CASTLE_K or _Q. Sets mv_from/mv_to/mv_flags for
; the side to move (king e1/e8, king-dest = from +/-2) and tail-calls castle_try.
; Returns castle_try's result: CC = legal + made (caller must unmake_move), CS =
; not available. Used by the enumerators to fold castling into move generation.
try_one_castle:
        STA mv_flags
        PHA                     ; remember which side (K vs Q)
        LDA #$00
        STA mv_promo
        LDA side_to_move
        BNE @black
        LDA #$04                ; white king e1
        JMP @setfrom
@black: LDA #$74                ; black king e8
@setfrom:
        STA mv_from
        PLA
        AND #MV_FLAG_CASTLE_K
        BEQ @qs
        LDA mv_from             ; kingside: king moves +2 (e->g)
        CLC
        ADC #$02
        STA mv_to
        JMP castle_try
@qs:    LDA mv_from             ; queenside: king moves -2 (e->c)
        SEC
        SBC #$02
        STA mv_to
        JMP castle_try

; ============================================================================
; is_pseudo_legal -- does the from→to move match this piece type's pattern?
; ============================================================================
; Input: mv_from, mv_to set; ce_piece = piece at from (full byte).
; Output: carry clear = pseudo-legal, carry set = no.
; Sets ce_match=1 if pseudo-legal.
is_pseudo_legal:
        LDA #$00
        STA ce_match
        LDA ce_piece
        AND #PIECE_MASK
        ; Dispatch by piece type.
        CMP #PIECE_PAWN
        BNE @not_p
        JMP psl_pawn
@not_p: CMP #PIECE_KNIGHT
        BNE @not_n
        JMP psl_knight
@not_n: CMP #PIECE_BISHOP
        BNE @not_b
        JMP psl_bishop
@not_b: CMP #PIECE_ROOK
        BNE @not_r
        JMP psl_rook
@not_r: CMP #PIECE_QUEEN
        BNE @not_q
        JMP psl_queen
@not_q: CMP #PIECE_KING
        BNE @bad
        JMP psl_king
@bad:   SEC
        RTS

; --- pawn ------------------------------------------------------------------
; White pawns move +$10, capture +$0F / +$11. Black pawns move -$10,
; capture -$11 / -$0F. Initial double push from rank 2 (white) or 7 (black).
psl_pawn:
        ; Determine forward step.
        LDA ce_piece
        AND #COLOR_MASK
        BEQ @white_pawn
        ; Black pawn: forward = -$10.
        LDA #$F0            ; -16 in two's complement
        STA tmp
        ; Capture diagonals: -$11 and -$0F
        LDA #$EF
        STA tmp2            ; left capture (file-1)
        ; right capture computed inline below
        JMP @check_pawn_dir
@white_pawn:
        LDA #$10
        STA tmp
        LDA #$0F
        STA tmp2            ; left capture (file-1)
@check_pawn_dir:
        ; Compute mv_from + tmp = single push square; compare to mv_to.
        LDA mv_from
        CLC
        ADC tmp
        CMP mv_to
        BNE @try_double
        ; Single push: destination must be empty.
        LDX mv_to
        LDA board,X
        BNE @bad_p          ; blocked
        ; Promotion check: if rank is 1 (black) or 8 (white), require mv_promo.
        ; (For v0.1 we auto-promote to queen if user didn't supply.)
        JSR maybe_promote
        CLC
        RTS
@try_double:
        ; Double push: only from starting rank, both intermediate and dest empty.
        LDA ce_piece
        AND #COLOR_MASK
        BEQ @wd
        ; Black double push: from rank 7 (mv_from in $60..$67) → -$20
        LDA mv_from
        AND #$70
        CMP #$60
        BNE @try_capture_pawn
        LDA mv_from
        SEC
        SBC #$20
        CMP mv_to
        BNE @try_capture_pawn
        ; Intermediate (mv_from + tmp = mv_from - $10) must be empty
        LDA mv_from
        CLC
        ADC tmp
        TAX
        LDA board,X
        BNE @bad_p
        ; Destination must be empty
        LDX mv_to
        LDA board,X
        BNE @bad_p
        CLC
        RTS
@wd:    ; White double push: from rank 2 (mv_from in $10..$17) → +$20
        LDA mv_from
        AND #$70
        CMP #$10
        BNE @try_capture_pawn
        LDA mv_from
        CLC
        ADC #$20
        CMP mv_to
        BNE @try_capture_pawn
        LDA mv_from
        CLC
        ADC tmp
        TAX
        LDA board,X
        BNE @bad_p
        LDX mv_to
        LDA board,X
        BNE @bad_p
        CLC
        RTS
@try_capture_pawn:
        ; Capture diagonals
        LDA mv_from
        CLC
        ADC tmp2            ; left capture
        CMP mv_to
        BEQ @cap
        ; Right capture: tmp2 + 2 (= -$0F for black, $11 for white)
        LDA mv_from
        CLC
        ADC tmp2
        CLC
        ADC #$02
        CMP mv_to
        BEQ @cap
@bad_p: SEC
        RTS
@cap:   ; Destination must contain an enemy piece, OR be the en-passant square.
        LDX mv_to
        LDA board,X
        BNE @cap_ok
        ; Empty destination — check en-passant.
        LDA mv_to
        CMP ep_square
        BNE @bad_p_ep
        ; En-passant capture: tag the move so make_move removes the
        ; adjacent pawn.
        LDA mv_flags
        ORA #MV_FLAG_ENPASSANT
        STA mv_flags
        JMP @cap_ok_ep
@bad_p_ep:
        SEC
        RTS
@cap_ok:
        ; Normal capture.
@cap_ok_ep:
        JSR maybe_promote
        CLC
        RTS

; If pawn lands on rank 1 or 8 and mv_promo == 0, force queen promotion.
maybe_promote:
        LDA mv_to
        AND #$70
        CMP #$00
        BEQ @do
        CMP #$70
        BEQ @do
        RTS
@do:    LDA mv_promo
        BNE @keep
        LDA #PIECE_QUEEN
        STA mv_promo
@keep:  RTS

; --- knight ----------------------------------------------------------------
psl_knight:
        ; Iterate 8 knight offsets, check if any equals (mv_to - mv_from).
        LDA mv_to
        SEC
        SBC mv_from
        STA tmp                 ; signed delta
        LDX #$08
@kl:    LDA knight_offsets-1,X
        CMP tmp
        BEQ @kok
        DEX
        BNE @kl
        SEC
        RTS
@kok:   CLC
        RTS

; --- bishop / rook / queen / king (sliding helpers) ----------------------
psl_bishop:
        LDA #<bishop_offsets
        STA ce_dir_ptr
        LDA #4
        STA ce_dirs_left
        JMP slide_check

psl_rook:
        LDA #<rook_offsets
        STA ce_dir_ptr
        LDA #4
        STA ce_dirs_left
        JMP slide_check

psl_queen:
        ; Queen = rook + bishop. Try rook first.
        JSR psl_rook
        BCC @ok
        JMP psl_bishop
@ok:    RTS

psl_king:
        ; King: any of 8 king_offsets, single step.
        LDA mv_to
        SEC
        SBC mv_from
        STA tmp
        LDX #$08
@kl:    LDA king_offsets-1,X
        CMP tmp
        BEQ @ok
        DEX
        BNE @kl
        SEC
        RTS
@ok:    CLC
        RTS

; slide_check: scan ce_dirs_left directions starting at offset table
; (low byte) ce_dir_ptr (must be in same page as bishop_offsets/rook_offsets).
; For each direction, walk from mv_from until off-board, an enemy/own piece,
; or mv_to is hit. Carry clear if mv_to reachable through empty squares
; (last square may be empty or any opponent piece — own-piece filter happened
; earlier in apply_user_move so we needn't recheck).
slide_check:
        LDX #$00
@dloop:
        ; Load offset for direction X.
        STX tmp2                ; preserve dir index across the @step walk (TAX below)
        ; Read tables[base + X] directly, forking by table low byte.
        LDA ce_dir_ptr
        CMP #<bishop_offsets
        BEQ @use_bishop
        LDA rook_offsets,X
        JMP @gotdir
@use_bishop:
        LDA bishop_offsets,X
@gotdir:
        STA ce_dir
        ; Walk from mv_from along ce_dir until we hit mv_to or a blocker.
        LDA mv_from
        STA ce_sq
@step:
        LDA ce_sq
        CLC
        ADC ce_dir
        STA ce_sq
        AND #OFFBOARD_MASK
        BNE @next_dir       ; off-board → try next direction
        LDA ce_sq
        CMP mv_to
        BEQ @found
        ; Square not yet target — must be empty to continue sliding.
        TAX
        LDA board,X
        BEQ @step           ; empty → keep stepping
        ; Blocked by some piece (not the target) → next direction.
@next_dir:
        LDX tmp2
        INX
        CPX ce_dirs_left
        BNE @dloop
        SEC
        RTS
@found: CLC
        RTS

; ============================================================================
; make_move -- apply the move (mv_from, mv_to, mv_promo, mv_flags) to the board
; ============================================================================
; Saves enough state for unmake_move (single-level undo).
; Does NOT toggle side_to_move (apply_user_move does that after legality OK).
;
; Handles:
;   - normal moves + captures + promotion
;   - en-passant capture (mv_flags bit MV_FLAG_ENPASSANT)
;   - castling (mv_flags bits MV_FLAG_CASTLE_K / Q)
;   - ep_square tracking (set after pawn double push, cleared otherwise)
;   - castling_rights updates (king move, rook move from corner, rook captured)
make_move:
        ; --- 1. Save state for undo ---
        LDX mv_to
        LDA board,X
        STA saved_captured
        LDA ep_square
        STA saved_ep
        LDA castling_rights
        STA saved_castling
        LDA halfmove_clock
        STA saved_halfmove
        LDA #$88
        STA saved_king_sq

        ; --- 2. Castling? Branch to specialised handler. ---
        LDA mv_flags
        AND #(MV_FLAG_CASTLE_K | MV_FLAG_CASTLE_Q)
        BEQ @normal
        JMP do_castle

@normal:
        ; --- 3. Pre-move bookkeeping for king/rook (castling rights) ---
        LDX mv_from
        LDA board,X
        STA tmp                 ; tmp = moving piece (full byte)
        AND #PIECE_MASK
        CMP #PIECE_KING
        BNE @not_king_move
        ; King is moving — save king_sq, update, clear both rights for this side
        LDA side_to_move
        BNE @bkm
        LDA king_sq_white
        STA saved_king_sq
        LDA mv_to
        STA king_sq_white
        ; Clear white castling rights
        LDA castling_rights
        AND #($FF ^ (CR_WHITE_K | CR_WHITE_Q))
        STA castling_rights
        JMP @not_king_move
@bkm:   LDA king_sq_black
        STA saved_king_sq
        LDA mv_to
        STA king_sq_black
        LDA castling_rights
        AND #($FF ^ (CR_BLACK_K | CR_BLACK_Q))
        STA castling_rights
@not_king_move:
        ; If a rook is moving from one of the corner squares, clear that
        ; side's castling right.
        LDA tmp
        AND #PIECE_MASK
        CMP #PIECE_ROOK
        BNE @not_rook_move
        LDA mv_from
        CMP #$00            ; A1
        BNE @nrm1
        LDA castling_rights
        AND #($FF ^ CR_WHITE_Q)
        STA castling_rights
        JMP @not_rook_move
@nrm1:  CMP #$07            ; H1
        BNE @nrm2
        LDA castling_rights
        AND #($FF ^ CR_WHITE_K)
        STA castling_rights
        JMP @not_rook_move
@nrm2:  CMP #$70            ; A8
        BNE @nrm3
        LDA castling_rights
        AND #($FF ^ CR_BLACK_Q)
        STA castling_rights
        JMP @not_rook_move
@nrm3:  CMP #$77            ; H8
        BNE @not_rook_move
        LDA castling_rights
        AND #($FF ^ CR_BLACK_K)
        STA castling_rights
@not_rook_move:
        ; If captured piece is a rook on a corner, clear that side's right.
        LDA saved_captured
        BEQ @nocap_corner
        AND #PIECE_MASK
        CMP #PIECE_ROOK
        BNE @nocap_corner
        LDA mv_to
        CMP #$00
        BNE @ncc1
        LDA castling_rights
        AND #($FF ^ CR_WHITE_Q)
        STA castling_rights
        JMP @nocap_corner
@ncc1:  CMP #$07
        BNE @ncc2
        LDA castling_rights
        AND #($FF ^ CR_WHITE_K)
        STA castling_rights
        JMP @nocap_corner
@ncc2:  CMP #$70
        BNE @ncc3
        LDA castling_rights
        AND #($FF ^ CR_BLACK_Q)
        STA castling_rights
        JMP @nocap_corner
@ncc3:  CMP #$77
        BNE @nocap_corner
        LDA castling_rights
        AND #($FF ^ CR_BLACK_K)
        STA castling_rights
@nocap_corner:

        ; --- 4. Move the piece, with optional promotion ---
        LDX mv_from
        LDA #$00
        STA board,X             ; clear from
        LDA mv_promo
        BEQ @no_promo
        ; Promotion: piece type becomes mv_promo
        LDA tmp
        AND #COLOR_MASK
        ORA mv_promo
        STA tmp
@no_promo:
        LDX mv_to
        LDA tmp
        STA board,X

        ; --- 5. En-passant capture: also clear the captured pawn ---
        LDA mv_flags
        AND #MV_FLAG_ENPASSANT
        BEQ @no_ep_cap
        ; Captured pawn sits on the same file as mv_to but the rank where
        ; the moving pawn started this turn (i.e., one rank back from mv_to
        ; in the moving side's perspective).
        LDA side_to_move
        BNE @epb
        ; White captures black: pawn is at mv_to - $10
        LDA mv_to
        SEC
        SBC #$10
        TAX
        LDA board,X
        STA saved_captured      ; record for unmake (overrides earlier $00)
        LDA #$00
        STA board,X
        JMP @no_ep_cap
@epb:
        ; Black captures white: pawn is at mv_to + $10
        LDA mv_to
        CLC
        ADC #$10
        TAX
        LDA board,X
        STA saved_captured
        LDA #$00
        STA board,X
@no_ep_cap:

        ; --- 6. Update ep_square ---
        ; Set if this was a pawn double push, else clear.
        LDA tmp
        AND #PIECE_MASK
        CMP #PIECE_PAWN
        BNE @ep_clear
        ; Compute |delta|. White double push: mv_to - mv_from = $20.
        ; Black double push: mv_from - mv_to = $20.
        LDA mv_to
        SEC
        SBC mv_from
        CMP #$20
        BEQ @ep_set
        LDA mv_from
        SEC
        SBC mv_to
        CMP #$20
        BNE @ep_clear
@ep_set:
        ; ep_square = (mv_from + mv_to) / 2
        CLC
        LDA mv_from
        ADC mv_to
        LSR A
        STA ep_square
        JMP @ep_done
@ep_clear:
        LDA #$88
        STA ep_square
@ep_done:

        ; --- 7. halfmove clock (50-move rule) ---
        LDA saved_captured
        BNE @reset_hm
        LDA tmp
        AND #PIECE_MASK
        CMP #PIECE_PAWN
        BEQ @reset_hm
        INC halfmove_clock
        RTS
@reset_hm:
        LDA #$00
        STA halfmove_clock
        RTS

; ============================================================================
; do_castle -- execute a validated castling move
; ============================================================================
; Called from make_move when mv_flags has CASTLE_K or CASTLE_Q set.
; Assumes apply_user_move has already validated:
;   - the right is granted
;   - the squares between king and rook are empty
;   - king not in check before the move
;   - king does not pass through an attacked square
;
; mv_from = king's current square (E1=$04 or E8=$74)
; mv_to   = king's destination (G1=$06/G8=$76 KS, C1=$02/C8=$72 QS)
;
; Saves enough for unmake_move (rook from/to noted in saved_castle_rook_*).
do_castle:
        ; Save king's position for undo.
        LDA mv_from
        STA saved_king_sq

        ; Move the king
        LDX mv_from
        LDA board,X
        STA tmp                 ; the king
        LDA #$00
        STA board,X
        LDX mv_to
        LDA tmp
        STA board,X

        ; Determine rook from/to and update king_sq.
        LDA mv_flags
        AND #MV_FLAG_CASTLE_K
        BEQ @qs
        ; Kingside: rook from H-file (col 7) to F-file (col 5).
        LDA side_to_move
        BNE @ksb
        ; White kingside: H1=$07 -> F1=$05, king to G1=$06
        LDA mv_to
        STA king_sq_white
        LDX #$07
        LDA board,X
        LDY #$05
        STA board,Y
        LDA #$00
        STA board,X
        LDA #$07                ; rook from
        STA saved_castle_rook_from
        LDA #$05                ; rook to
        STA saved_castle_rook_to
        JMP @done
@ksb:
        ; Black kingside: H8=$77 -> F8=$75, king to G8=$76
        LDA mv_to
        STA king_sq_black
        LDX #$77
        LDA board,X
        LDY #$75
        STA board,Y
        LDA #$00
        STA board,X
        LDA #$77
        STA saved_castle_rook_from
        LDA #$75
        STA saved_castle_rook_to
        JMP @done
@qs:
        ; Queenside: rook from A-file (col 0) to D-file (col 3).
        LDA side_to_move
        BNE @qsb
        ; White queenside: A1=$00 -> D1=$03, king to C1=$02
        LDA mv_to
        STA king_sq_white
        LDX #$00
        LDA board,X
        LDY #$03
        STA board,Y
        LDA #$00
        STA board,X
        LDA #$00
        STA saved_castle_rook_from
        LDA #$03
        STA saved_castle_rook_to
        JMP @done
@qsb:
        ; Black queenside: A8=$70 -> D8=$73, king to C8=$72
        LDA mv_to
        STA king_sq_black
        LDX #$70
        LDA board,X
        LDY #$73
        STA board,Y
        LDA #$00
        STA board,X
        LDA #$70
        STA saved_castle_rook_from
        LDA #$73
        STA saved_castle_rook_to
@done:
        ; Clear both castling rights for the moving side.
        LDA side_to_move
        BNE @cb
        LDA castling_rights
        AND #($FF ^ (CR_WHITE_K | CR_WHITE_Q))
        STA castling_rights
        JMP @hm
@cb:    LDA castling_rights
        AND #($FF ^ (CR_BLACK_K | CR_BLACK_Q))
        STA castling_rights
@hm:    ; Castling resets ep_square but does NOT reset halfmove (it counts
        ; as a non-pawn, non-capture move).
        LDA #$88
        STA ep_square
        INC halfmove_clock
        RTS

; ============================================================================
; unmake_move -- restore state saved by the most recent make_move
; ============================================================================
unmake_move:
        ; Castling? Special path.
        LDA mv_flags
        AND #(MV_FLAG_CASTLE_K | MV_FLAG_CASTLE_Q)
        BEQ @normal
        JMP undo_castle

@normal:
        ; Restore moving piece to from-square. If promotion happened,
        ; the original piece type was a pawn — restore the pawn.
        LDX mv_to
        LDA board,X
        STA tmp                 ; current piece on to-square
        LDA mv_promo
        BEQ @no_unp
        LDA tmp
        AND #COLOR_MASK
        ORA #PIECE_PAWN
        STA tmp
@no_unp:
        LDX mv_from
        LDA tmp
        STA board,X
        ; Clear to-square first (default).
        LDX mv_to
        LDA #$00
        STA board,X

        ; En-passant capture: restore captured pawn to its original square.
        LDA mv_flags
        AND #MV_FLAG_ENPASSANT
        BEQ @no_ep_undo
        LDA side_to_move
        BNE @epb
        ; White ep: pawn was at mv_to - $10
        LDA mv_to
        SEC
        SBC #$10
        TAX
        LDA saved_captured
        STA board,X
        JMP @restore_state
@epb:
        ; Black ep: pawn was at mv_to + $10
        LDA mv_to
        CLC
        ADC #$10
        TAX
        LDA saved_captured
        STA board,X
        JMP @restore_state
@no_ep_undo:
        ; Normal capture (or empty): restore captured piece on to-square.
        LDX mv_to
        LDA saved_captured
        STA board,X
@restore_state:
        ; Restore game state.
        LDA saved_ep
        STA ep_square
        LDA saved_castling
        STA castling_rights
        LDA saved_halfmove
        STA halfmove_clock
        ; Restore king square if king moved.
        LDA saved_king_sq
        CMP #$88
        BEQ @nok
        LDA side_to_move
        BNE @bk
        LDA saved_king_sq
        STA king_sq_white
        RTS
@bk:    LDA saved_king_sq
        STA king_sq_black
@nok:   RTS

; ============================================================================
; undo_castle -- reverse the castling move
; ============================================================================
undo_castle:
        ; Move the king back from mv_to to mv_from.
        LDX mv_to
        LDA board,X
        STA tmp
        LDA #$00
        STA board,X
        LDX mv_from
        LDA tmp
        STA board,X
        ; Restore king_sq.
        LDA side_to_move
        BNE @bk
        LDA saved_king_sq
        STA king_sq_white
        JMP @rook
@bk:    LDA saved_king_sq
        STA king_sq_black
@rook:  ; Move the rook back from saved_castle_rook_to to saved_castle_rook_from.
        LDX saved_castle_rook_to
        LDA board,X
        STA tmp
        LDA #$00
        STA board,X
        LDX saved_castle_rook_from
        LDA tmp
        STA board,X
        ; Restore castling_rights, ep, halfmove.
        LDA saved_castling
        STA castling_rights
        LDA saved_ep
        STA ep_square
        LDA saved_halfmove
        STA halfmove_clock
        RTS

; ============================================================================
; in_check -- is the side_to_move king attacked by the other side?
; ============================================================================
; Returns A = nonzero if our king is attacked, A = 0 if safe.
; Z flag matches A.
in_check:
        LDA side_to_move
        BNE @bk
        LDA king_sq_white
        STA attacked_sq
        LDA #COLOR_BLACK
        STA attacker_color
        JMP is_attacked_runner
@bk:    LDA king_sq_black
        STA attacked_sq
        LDA #SIDE_WHITE
        STA attacker_color
        ; fall through

; is_attacked_runner: scan board[] for any attacker_color piece that can
; reach attacked_sq. Returns A = 0 (not attacked) or A = 1 (attacked).
;
; Implementation note: the attack-test helpers (atk_rook, atk_bishop,
; atk_knight, atk_king) all use X as their direction-loop counter, which
; would clobber our scan square. We persist the scan square in `scan_sq`
; (BSS) and reload X from it after every JSR.
is_attacked_runner:
        LDA #$00
        STA scan_sq
@bloop:
        LDX scan_sq
        TXA
        AND #OFFBOARD_MASK
        BNE @next
        LDA board,X
        BEQ @next
        ; Must be the attacker colour.
        TAY
        AND #COLOR_MASK
        CMP attacker_color
        BNE @next
        ; Test if this piece attacks attacked_sq. Use atk_piece (NOT ce_piece)
        ; so move-gen callers' ce_piece survives across in_check.
        STX ce_sq               ; attacker's square
        TYA
        STA atk_piece
        JSR attacks_target
        BCC @hit
@next:
        INC scan_sq
        BNE @bloop
        LDA #$00                ; not attacked
        RTS
@hit:   LDA #$01                ; attacked
        RTS

; attacks_target: does the piece at ce_sq (full byte ce_piece) attack
; the square attacked_sq? Returns CC=yes, CS=no.
attacks_target:
        LDA atk_piece
        AND #PIECE_MASK
        CMP #PIECE_PAWN
        BNE @nop
        JMP atk_pawn
@nop:   CMP #PIECE_KNIGHT
        BNE @non
        JMP atk_knight
@non:   CMP #PIECE_KING
        BNE @nok
        JMP atk_king
@nok:   CMP #PIECE_BISHOP
        BNE @nob
        JMP atk_bishop
@nob:   CMP #PIECE_ROOK
        BNE @noq
        JMP atk_rook
@noq:   ; Queen
        JSR atk_rook
        BCC @qok
        JMP atk_bishop
@qok:   RTS

atk_pawn:
        ; Pawn attacks two diagonal forward squares.
        LDA atk_piece
        AND #COLOR_MASK
        BEQ @wp
        ; Black pawn attacks ce_sq + (-$11) and (-$0F)
        LDA ce_sq
        CLC
        ADC #$EF
        CMP attacked_sq
        BEQ @ok
        LDA ce_sq
        CLC
        ADC #$F1
        CMP attacked_sq
        BEQ @ok
        SEC
        RTS
@wp:    ; White pawn attacks ce_sq + ($0F) and ($11)
        LDA ce_sq
        CLC
        ADC #$0F
        CMP attacked_sq
        BEQ @ok
        LDA ce_sq
        CLC
        ADC #$11
        CMP attacked_sq
        BEQ @ok
        SEC
        RTS
@ok:    CLC
        RTS

atk_knight:
        LDA attacked_sq
        SEC
        SBC ce_sq
        STA tmp
        LDX #$08
@l:     LDA knight_offsets-1,X
        CMP tmp
        BEQ @ok
        DEX
        BNE @l
        SEC
        RTS
@ok:    CLC
        RTS

atk_king:
        LDA attacked_sq
        SEC
        SBC ce_sq
        STA tmp
        LDX #$08
@l:     LDA king_offsets-1,X
        CMP tmp
        BEQ @ok
        DEX
        BNE @l
        SEC
        RTS
@ok:    CLC
        RTS

atk_bishop:
        LDX #$00
@dloop:
        LDA bishop_offsets,X
        STA ce_dir
        LDA ce_sq
        STA tmp                 ; current scan square
@step:
        LDA tmp
        CLC
        ADC ce_dir
        STA tmp
        AND #OFFBOARD_MASK
        BNE @next
        LDA tmp
        CMP attacked_sq
        BEQ @hit
        TAY
        LDA board,Y
        BEQ @step
        ; Blocked
@next:  INX
        CPX #$04
        BNE @dloop
        SEC
        RTS
@hit:   CLC
        RTS

atk_rook:
        LDX #$00
@dloop:
        LDA rook_offsets,X
        STA ce_dir
        LDA ce_sq
        STA tmp
@step:
        LDA tmp
        CLC
        ADC ce_dir
        STA tmp
        AND #OFFBOARD_MASK
        BNE @next
        LDA tmp
        CMP attacked_sq
        BEQ @hit
        TAY
        LDA board,Y
        BEQ @step
@next:  INX
        CPX #$04
        BNE @dloop
        SEC
        RTS
@hit:   CLC
        RTS

; ============================================================================
; game_status -- 0 ongoing, 1 white-mate, 2 black-mate, 3 stalemate
; ============================================================================
; v0.1: brute-force "any legal move" check. Iterates all (from, to) pairs
; for the side to move and tests pseudo-legality + safety. Slow (O(64*64))
; but correct, runs once per move (~few hundred ms at 1 MHz). Polished in
; v1.2 with proper move-list iteration.
;
; Returns A = status code, Z reflects A.
game_status:
        ; Try every from/to pair until we find a legal move.
        LDX #$00
@floop:
        TXA
        AND #OFFBOARD_MASK
        BNE @nextf
        STX mv_from
        LDA board,X
        BEQ @nextf
        AND #COLOR_MASK
        CMP side_to_move
        BNE @nextf
        STA tmp                 ; (unused, just preserved)
        ; Re-load piece into ce_piece for is_pseudo_legal.
        LDA board,X
        STA ce_piece
        LDY #$00
@tloop:
        TYA
        AND #OFFBOARD_MASK
        BNE @nextt
        STY mv_to
        ; Skip same square.
        CPY mv_from
        BEQ @nextt
        ; Skip own-colour destination.
        LDA board,Y
        BEQ @testit
        AND #COLOR_MASK
        CMP side_to_move
        BEQ @nextt
@testit:
        LDA #$00
        STA mv_promo            ; ignore promotion choice during scan
        STA mv_flags            ; clear castling/ep flags from prior user move
        ; Save then test: is_pseudo_legal + make + in_check + unmake.
        TXA
        PHA                     ; save X across calls
        TYA
        PHA
        JSR is_pseudo_legal
        BCS @undo_y
        JSR make_move
        JSR in_check
        BNE @bad
        ; Found a legal move → game ongoing.
        JSR unmake_move
        PLA
        TAY
        PLA
        TAX
        LDA #$00
        RTS
@bad:   JSR unmake_move
@undo_y:
        PLA
        TAY
        PLA
        TAX
@nextt: INY
        BNE @tloop
@nextf: INX
        BNE @floop
        ; No NORMAL legal move was found. Castling isn't produced by the
        ; (from,to) scan, so a castle could still be the only legal move — test
        ; both before declaring mate/stalemate, or a castle-only position would
        ; be a false terminal.
        LDA #MV_FLAG_CASTLE_K
        JSR try_one_castle
        BCC @gs_castle_ok
        LDA #MV_FLAG_CASTLE_Q
        JSR try_one_castle
        BCS @gs_no_move
@gs_castle_ok:
        JSR unmake_move     ; revert the castle that try_one_castle made
        LDA #$00            ; a legal move exists → game ongoing
        RTS
@gs_no_move:
        ; No legal move at all → mate or stalemate.
        JSR in_check
        BEQ @stale
        ; Checkmate: side_to_move loses. White-mate=1 means white is mated.
        LDA side_to_move
        BNE @bm
        LDA #$01            ; white in checkmate
        RTS
@bm:    LDA #$02            ; black in checkmate
        RTS
@stale: LDA #$03
        RTS

; ============================================================================
; evaluate_material -- A = (our material - their material), signed 8-bit
; ============================================================================
; Simple material count, scaled to fit in signed 8-bit:
;   pawn=1, knight=3, bishop=3, rook=5, queen=9, king=0 (always equal).
; Returned in score_lo (BSS). Range +/-39 for normal positions.
;
; "Our" = side_to_move at evaluation time.
.export evaluate_material
.export score_lo
evaluate_material:
        LDA #$00
        STA score_lo
        LDX #$00
@elp:
        TXA
        AND #OFFBOARD_MASK
        BNE @ent
        LDA board,X
        BEQ @ent
        STA tmp                 ; tmp = piece byte
        AND #PIECE_MASK
        TAY                     ; Y = piece type
        LDA mat_simple,Y        ; A = piece value (table below)
        STA tmp2
        LDA tmp
        AND #COLOR_MASK
        CMP side_to_move
        BNE @sub
        ; Same colour as side_to_move → add to score
        CLC
        LDA score_lo
        ADC tmp2
        STA score_lo
        JMP @ent
@sub:
        ; Opposite colour → subtract
        SEC
        LDA score_lo
        SBC tmp2
        STA score_lo
@ent:
        INX
        BNE @elp
        LDA score_lo
        RTS

; Compact material table (signed 8-bit safe sums):
; index 0..7 (NONE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, ?)
mat_simple:
        .byte 0, 1, 3, 3, 5, 9, 0, 0

; cdist -- "distance from the edge" per file/rank index: a pyramid peaking at
; the centre files/ranks. Centralisation of a square = cdist[file] + cdist[rank].
cdist:
        .byte 0, 1, 2, 3, 3, 2, 1, 0

; ============================================================================
; evaluate_positional -- A = (our positional - their positional), signed 8-bit.
; ============================================================================
; A small, material-independent score: every piece earns its centralisation
; (cdist[file]+cdist[rank]); pawns additionally earn 2x their advance toward
; promotion (white = rank, black = 7-rank). "Our" = side_to_move. It is used
; ONLY as a tie-break between material-equal moves (see consider_move), so it
; can never outweigh material or a tactic — it just gives the search a sense of
; progress (develop, centralise, push passed pawns), which is what stops a won
; endgame stalling at the move cap.
evaluate_positional:
        LDA #$00
        STA score_lo            ; signed accumulator (our - their)
        LDX #$00
@ep_loop:
        TXA
        AND #OFFBOARD_MASK
        BNE @ep_next
        LDA board,X
        BEQ @ep_next
        STA tmp                 ; tmp = piece byte
        ; centralisation = cdist[rank] + cdist[file]; keep rank for pawn reuse
        TXA
        LSR A
        LSR A
        LSR A
        LSR A
        AND #$07
        STA ce_target           ; ce_target = rank
        TAY
        LDA cdist,Y
        STA tmp2                ; cdist[rank]
        TXA
        AND #$07
        TAY
        LDA cdist,Y
        CLC
        ADC tmp2
        STA tmp2                ; centralisation (0..6)
        ; pawns: + 2 * advance toward promotion (white=rank, black=7-rank)
        LDA tmp
        AND #PIECE_MASK
        CMP #PIECE_PAWN
        BNE @ep_have
        LDA ce_target           ; rank
        BIT tmp                 ; N = colour bit (black -> set)
        BPL @ep_padd            ; white: advance = rank
        EOR #$07                ; black: advance = 7 - rank
@ep_padd:
        ASL A                   ; weight advancement x2
        CLC
        ADC tmp2
        STA tmp2
@ep_have:
        ; Accumulate signed: + for our pieces, - for theirs.
        LDA tmp
        AND #COLOR_MASK
        CMP side_to_move
        BNE @ep_their
        CLC
        LDA score_lo
        ADC tmp2
        STA score_lo
        JMP @ep_next
@ep_their:
        SEC
        LDA score_lo
        SBC tmp2
        STA score_lo
@ep_next:
        INX
        BNE @ep_loop
        LDA score_lo
        RTS

; ============================================================================
; ai_rng_step -- advance the 8-bit LFSR. Period 255 (Galois, taps $1D).
; ============================================================================
; Returns A = new LFSR state (and stored in ai_rng). Z reflects A.
; Bit 0 of the result is the random bit consumed by the reservoir sampler.
; Self-heals if ai_rng ever drops to zero (which kills the LFSR).
ai_rng_step:
        LDA ai_rng
        BNE @nz
        LDA #$AC
@nz:    ASL A
        BCC @done
        EOR #$1D
@done:  STA ai_rng
        RTS

; ============================================================================
; 2-ply (negamax) move search — replaces the old 1-ply + Static-Exchange
; heuristic. ai_strategy selects depth: NAIVE = 1-ply material (fast), SMART
; (default) = 2-ply minimax. Depth 2 sees the opponent's immediate reply, so it
; stops hanging pieces, grabs free material, finds mate-in-1 and refuses to give
; it — a large strength jump over the destination-only SEE it replaces.
;
; Nesting our move (outer) over the opponent's reply (inner) needs make_move to
; nest one level. make_move/unmake_move keep a single saved_* slot, so the outer
; move's undo info is copied to saved2_* across the inner reply loop (push_saved
; / pop_saved) and the outer move bytes are snapshotted in m_* (the inner loop
; reuses mv_from/mv_to/mv_flags).
; ============================================================================

; Positional bonus (pawn-equivalents) added to a castling move's score so the
; otherwise material-neutral castle is preferred over a quiet move but still
; yields to a real capture/promotion of equal-or-greater value.
CASTLE_BONUS = 1

; push_saved / pop_saved — copy the make_move undo slots to / from the 2nd
; buffer so one nested make_move can't destroy the outer move's undo info.
push_saved:
        LDA saved_captured
        STA saved2_captured
        LDA saved_ep
        STA saved2_ep
        LDA saved_castling
        STA saved2_castling
        LDA saved_halfmove
        STA saved2_halfmove
        LDA saved_king_sq
        STA saved2_king_sq
        LDA saved_castle_rook_from
        STA saved2_crf
        LDA saved_castle_rook_to
        STA saved2_crt
        RTS
pop_saved:
        LDA saved2_captured
        STA saved_captured
        LDA saved2_ep
        STA saved_ep
        LDA saved2_castling
        STA saved_castling
        LDA saved2_halfmove
        STA saved_halfmove
        LDA saved2_king_sq
        STA saved_king_sq
        LDA saved2_crf
        STA saved_castle_rook_from
        LDA saved2_crt
        STA saved_castle_rook_to
        RTS

; best_reply_eval — side_to_move is the OPPONENT and the board reflects our
; move. Scan the opponent's legal replies; return A = the opponent's best
; evaluate_material (their perspective = their material - ours): the most they
; can do to us in one reply. No legal reply: A = -127 if the opponent is in
; check (we just mated them), else 0 (stalemate). Single-level make/unmake here.
best_reply_eval:
        LDA #$80
        STA best_reply          ; -128: any real reply beats it
        LDA #$00
        STA reply_found
        STA rscan_x
@rfloop:
        LDX rscan_x
        TXA
        AND #OFFBOARD_MASK
        BNE @rnextf
        LDA board,X
        BEQ @rnextf
        AND #COLOR_MASK
        CMP side_to_move
        BNE @rnextf             ; only the side-to-move's (opponent's) pieces
        STX mv_from
        LDA board,X
        STA ce_piece
        LDA #$00
        STA rscan_y
@rtloop:
        LDY rscan_y
        TYA
        AND #OFFBOARD_MASK
        BNE @rnextt
        STY mv_to
        CPY mv_from
        BEQ @rnextt
        LDA board,Y
        BEQ @rtest
        AND #COLOR_MASK
        CMP side_to_move
        BEQ @rnextt             ; can't capture own piece
@rtest:
        LDA #$00
        STA mv_promo
        STA mv_flags
        JSR is_pseudo_legal
        BCS @rnextt
        JSR make_move
        JSR in_check            ; opponent's own king left in check?
        BNE @rillegal
        LDA #$01
        STA reply_found
        JSR evaluate_material   ; A = opp - us (opponent perspective)
        TAX                     ; preserve candidate
        CLC
        ADC #$80
        STA tmp                 ; biased candidate
        LDA best_reply
        CLC
        ADC #$80
        CMP tmp                 ; biased best vs biased candidate
        BCS @rnotbetter         ; best >= candidate -> keep
        STX best_reply          ; candidate > best -> adopt
@rnotbetter:
        JSR unmake_move
        JMP @rnextt
@rillegal:
        JSR unmake_move
@rnextt:
        INC rscan_y
        BNE @rtloop
@rnextf:
        INC rscan_x
        BNE @rfloop
        LDA reply_found
        BNE @rhave
        ; No normal reply — a castle could be the opponent's only legal move.
        LDA #MV_FLAG_CASTLE_K
        JSR try_one_castle
        BCC @rcastle
        LDA #MV_FLAG_CASTLE_Q
        JSR try_one_castle
        BCS @rnomove
@rcastle:
        JSR evaluate_material   ; castle is material-neutral; opp perspective
        STA best_reply
        JSR unmake_move
        LDA best_reply
        RTS
@rnomove:
        JSR in_check            ; opponent has NO legal move
        BEQ @rstale
        LDA #$81                ; -127: opponent is checkmated (we win)
        RTS
@rstale:
        LDA #$00                ; stalemate ~ draw
        RTS
@rhave:
        LDA best_reply
        RTS

; score_move — the move in mv_* is currently MADE on the board (our king already
; verified safe). Return A = its score from our perspective. NAIVE = material
; after the move; SMART = -(opponent's best reply) (2-ply). The board stays MADE
; (caller unmakes); SMART restores saved_*/mv_* before returning.
score_move:
        LDA ai_strategy
        BNE @sm_smart
        JMP evaluate_material   ; tail: A = our - their
@sm_smart:
        JSR push_saved
        LDA mv_from
        STA m_from
        LDA mv_to
        STA m_to
        LDA mv_flags
        STA m_flags
        LDA mv_promo
        STA m_promo
        JSR toggle_side         ; opponent to move
        JSR best_reply_eval
        STA m_score
        JSR toggle_side         ; back to us
        LDA m_from
        STA mv_from
        LDA m_to
        STA mv_to
        LDA m_flags
        STA mv_flags
        LDA m_promo
        STA mv_promo
        JSR pop_saved
        SEC
        LDA #$00
        SBC m_score             ; A = -(opponent best) = our negamax score
        RTS

; consider_move — A = candidate score for the move in mv_*. Replace ai_best_* if
; it strictly beats ai_best_score; on an exact tie step the LFSR and replace iff
; bit 0 = 1 (keeps AvA self-play diverging). Stores mv_flags too (castle rides).
consider_move:
        PHA
        CLC
        ADC #$80
        STA tmp                 ; biased candidate material
        LDA ai_best_score
        CLC
        ADC #$80
        STA tmp2                ; biased best material
        LDA tmp
        CMP tmp2
        BCC @cm_keep            ; material < best
        BEQ @cm_tie             ; material == best -> positional tie-break
        JMP @cm_take            ; material > best
@cm_tie:
        ; Material tied: prefer the better positional score (cand_pos). This is
        ; the ONLY place positional matters, so it can never override material.
        LDA cand_pos
        CLC
        ADC #$80
        STA tmp
        LDA ai_best_pos
        CLC
        ADC #$80
        STA tmp2
        LDA tmp
        CMP tmp2
        BCC @cm_keep            ; positional < best -> keep
        BNE @cm_take            ; positional > best -> take
        JSR ai_rng_step         ; positional also tied -> LFSR coin-flip
        AND #$01
        BEQ @cm_keep
@cm_take:
        PLA
        STA ai_best_score
        LDA cand_pos
        STA ai_best_pos
        LDA mv_from
        STA ai_best_from
        LDA mv_to
        STA ai_best_to
        LDA mv_flags
        STA ai_best_flags
        RTS
@cm_keep:
        PLA
        RTS

; ============================================================================
; ai_play_move — pick + apply the best move for side_to_move (depth per
; ai_strategy). CC if a move was made, CS if none (caller treats as mate/stale).
; ============================================================================
.export ai_play_move
.export ai_best_from, ai_best_to
.export ai_best_score
.export ai_strategy, ai_rng
ai_play_move:
        LDA #$88
        STA ai_best_from
        STA ai_best_to
        LDA #$00
        STA ai_best_flags
        LDA #$80                ; -128: worst
        STA ai_best_score
        STA ai_best_pos         ; -128: worst positional too
        LDA #$00
        STA ai_scan_x
@floop:
        LDX ai_scan_x
        TXA
        AND #OFFBOARD_MASK
        BNE @nextf
        LDA board,X
        BEQ @nextf
        AND #COLOR_MASK
        CMP side_to_move
        BNE @nextf
        STX mv_from
        LDA #$00
        STA ai_scan_y
@tloop:
        LDY ai_scan_y
        TYA
        AND #OFFBOARD_MASK
        BNE @nextt
        STY mv_to
        CPY mv_from
        BEQ @nextt
        LDA board,Y
        BEQ @testit
        AND #COLOR_MASK
        CMP side_to_move
        BEQ @nextt              ; own piece on destination
@testit:
        ; Re-establish ce_piece every destination: the SMART reply search
        ; (best_reply_eval) overwrites it while iterating the opponent's pieces.
        LDX mv_from
        LDA board,X
        STA ce_piece
        LDA #$00
        STA mv_promo
        STA mv_flags
        JSR is_pseudo_legal
        BCS @nextt
        JSR make_move
        JSR in_check            ; does the move leave OUR king in check?
        BNE @bad                ; illegal — discard
        JSR evaluate_positional ; board = after our move; tie-break score
        STA cand_pos
        JSR score_move          ; A = material score (1- or 2-ply)
        JSR consider_move       ; maybe adopt as best
        JSR unmake_move
        JMP @nextt
@bad:
        JSR unmake_move
@nextt:
        INC ai_scan_y
        BNE @tloop
@nextf:
        INC ai_scan_x
        BNE @floop

        ; --- Castling candidates (not produced by the from/to scan). ---
        LDA #MV_FLAG_CASTLE_K
        JSR try_one_castle
        BCS @try_qs
        JSR @castle_consider
@try_qs:
        LDA #MV_FLAG_CASTLE_Q
        JSR try_one_castle
        BCS @apply
        JSR @castle_consider
@apply:
        LDA ai_best_from
        CMP #$88
        BEQ @no_move
        STA mv_from
        LDA ai_best_to
        STA mv_to
        LDA #$00
        STA mv_promo
        LDA ai_best_flags
        STA mv_flags
        JSR apply_user_move     ; routes a castle bit to apply_castle_move
        RTS
@no_move:
        SEC
        RTS

; @castle_consider — the castle in mv_* is currently MADE (try_one_castle CC).
; Score it at the same depth + CASTLE_BONUS, adopt if best, then unmake.
@castle_consider:
        JSR evaluate_positional ; board = after the castle
        STA cand_pos
        JSR score_move
        CLC
        ADC #CASTLE_BONUS
        JSR consider_move
        JMP unmake_move         ; tail: revert the castle, RTS to caller


; ============================================================================
; perft1 -- count pseudo-legal moves at the current position that pass the
;           own-king-not-in-check filter (i.e. legal moves at depth 1)
; ============================================================================
; Returns count in perft_count_lo:hi (16-bit). Stops at 65535 (overflow
; safe for chess: max ~218 legal moves in any position).
.export perft1
.export perft_count_lo, perft_count_hi
perft1:
        LDA #$00
        STA perft_count_lo
        STA perft_count_hi
        STA ai_scan_x
@floop:
        LDX ai_scan_x
        TXA
        AND #OFFBOARD_MASK
        BNE @nextf
        LDA board,X
        BEQ @nextf
        AND #COLOR_MASK
        CMP side_to_move
        BNE @nextf
        STX mv_from
        LDA board,X
        STA ce_piece
        LDA #$00
        STA ai_scan_y
@tloop:
        LDY ai_scan_y
        TYA
        AND #OFFBOARD_MASK
        BNE @nextt
        STY mv_to
        CPY mv_from
        BEQ @nextt
        LDA board,Y
        BEQ @ptest
        AND #COLOR_MASK
        CMP side_to_move
        BEQ @nextt
@ptest:
        LDA #$00
        STA mv_promo
        STA mv_flags
        JSR is_pseudo_legal
        BCS @nextt
        JSR make_move
        JSR in_check
        BNE @illegal
        ; Legal move: increment counter
        INC perft_count_lo
        BNE @inc_done
        INC perft_count_hi
@inc_done:
        JSR unmake_move
        JMP @nextt
@illegal:
        JSR unmake_move
@nextt:
        INC ai_scan_y
        BNE @tloop
@nextf:
        INC ai_scan_x
        BNE @floop
        ; Castling isn't emitted by the (from,to) scan — add each legal castle
        ; to the count so perft(1) matches a reference generator.
        LDA #MV_FLAG_CASTLE_K
        JSR try_one_castle
        BCS @pft_qs
        JSR unmake_move
        INC perft_count_lo
        BNE @pft_qs
        INC perft_count_hi
@pft_qs:
        LDA #MV_FLAG_CASTLE_Q
        JSR try_one_castle
        BCS @pft_end
        JSR unmake_move
        INC perft_count_lo
        BNE @pft_end
        INC perft_count_hi
@pft_end:
        RTS

; ============================================================================
; save_user_state / undo_last_move -- single-level undo (compact)
; ============================================================================
; save_user_state: called by apply_user_move right after a move is committed
; (just before the side toggle). Snapshots the per-move undo info that the
; engine's saved_* slots will lose at the next game_status iteration.
;
; undo_last_move: restore the snapshotted state and call unmake_move. Toggles
; side back, decrements fullmove if applicable. Clears undo_avail (no
; double-undo).
.export save_user_state, undo_last_move
.export undo_avail

save_user_state:
        LDA mv_from
        STA user_mv_from
        LDA mv_to
        STA user_mv_to
        LDA mv_promo
        STA user_mv_promo
        LDA mv_flags
        STA user_mv_flags
        LDA saved_captured
        STA user_saved_captured
        LDA saved_ep
        STA user_saved_ep
        LDA saved_castling
        STA user_saved_castling
        LDA saved_halfmove
        STA user_saved_halfmove
        LDA saved_king_sq
        STA user_saved_king_sq
        LDA saved_castle_rook_from
        STA user_saved_rook_from
        LDA saved_castle_rook_to
        STA user_saved_rook_to
        LDA side_to_move
        STA user_saved_side
        LDA fullmove_number
        STA user_saved_fullmove
        LDA #$01
        STA undo_avail
        RTS

undo_last_move:
        LDA undo_avail
        BNE @ok
        SEC                     ; no undo
        RTS
@ok:
        ; Restore engine saved_* from user_* so unmake_move sees the right state
        LDA user_mv_from
        STA mv_from
        LDA user_mv_to
        STA mv_to
        LDA user_mv_promo
        STA mv_promo
        LDA user_mv_flags
        STA mv_flags
        LDA user_saved_captured
        STA saved_captured
        LDA user_saved_ep
        STA saved_ep
        LDA user_saved_castling
        STA saved_castling
        LDA user_saved_halfmove
        STA saved_halfmove
        LDA user_saved_king_sq
        STA saved_king_sq
        LDA user_saved_rook_from
        STA saved_castle_rook_from
        LDA user_saved_rook_to
        STA saved_castle_rook_to
        ; side_to_move was already toggled by apply_user_move after the
        ; original move. unmake_move expects side_to_move to still be the
        ; mover's side (it uses side_to_move to identify which king to
        ; restore). So toggle BACK first.
        LDA user_saved_side
        STA side_to_move
        LDA user_saved_fullmove
        STA fullmove_number
        ; Now reverse the move on the board.
        JSR unmake_move
        LDA #$00
        STA undo_avail
        CLC
        RTS
