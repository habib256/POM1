# lib/chess — Apple-1 Chess engine (shared across text/TMS9918/HGR variants)

Pure 6502 asm chess engine, designed for the POM1 Apple 1 emulator. The engine
itself is platform-agnostic — variants (text, TMS9918, HGR) supply their own
renderer + input glue and link against `chess_engine.o`.

Inspired by StewBC/cc65-Chess (board representation, move generation idiom,
material values), but rewritten in 6502 asm to match the existing POM1 game
conventions (Sokoban, Connect 4 trilogies).

## Files

- `chess_common.inc` — equates: piece codes, side codes, 0x88 board macros,
  move-flag bits, castling-rights bits, material values. Idempotent
  (`.ifndef _CHESS_COMMON_INC_LOADED_`).
- `chess_tables.inc` — direction tables (`knight_offsets`, `king_offsets`,
  `rook_offsets`, `bishop_offsets`), `material_table`, `starting_position`
  (rank-major 64-byte initial board), `piece_letters` (ASCII glyph per piece).
- `chess_engine.asm` — the engine. Separately assembled to `chess_engine.o`.
  Public symbols below.
- `chess_text_io.asm` — algebraic input parser (`E2E4`, optional `E7E8Q`),
  square-printer, error-string lookup. Used by all variants for the prompt
  (TMS9918/HGR variants overlay a graphical cursor on top, but algebraic
  remains the default fallback).

## Public API (chess_engine.o exports)

| Symbol | Description |
|---|---|
| `init_board` | Reset board to starting position, side=white, full castling rights. |
| `piece_at` | A = `board[X]`, where X is a 0x88 square. Cheap read. |
| `apply_user_move` | Validate & apply (mv_from, mv_to, mv_promo). CC=ok, CS+A=err. |
| `in_check` | A = 1 if side_to_move's king is attacked, A = 0 otherwise. |
| `game_status` | A = 0 ongoing, 1 white-mate, 2 black-mate, 3 stalemate. |
| `toggle_side` | side_to_move ^= COLOR_BLACK. |
| `ai_play_move` | Pick + apply best 1-ply greedy move. CC=ok, CS=no move. |
| `evaluate_material` | A = (our material - their material), signed 8-bit. |
| `perft1` | Count legal moves at depth 1, returns in `perft_count_lo:hi`. |
| `save_user_state` | Snapshot post-move state for undo (called by `apply_user_move`). |
| `undo_last_move` | Reverse the last user/AI move. CC=ok, CS=no undo available. |

BSS exports (variants `.import` then read directly):
`board, side_to_move, castling_rights, ep_square, halfmove_clock,
fullmove_number, king_sq_white, king_sq_black, mv_from, mv_to, mv_promo,
mv_flags`.

## Engine ZP usage

`tmp` and `tmp2` (from `lib/apple1/zp.inc`) — reserved by caller.
Engine-private slots: `ce_sq, ce_dir, ce_target, ce_piece, ce_color,
ce_dirs_left, ce_dir_ptr, ce_match, attacker_color, attacked_sq` — these
are declared inside the engine's `.segment "ZEROPAGE"` and consume 10
bytes contiguously after the caller's own ZP. Linker config must give
ZP at least 32 bytes (`size = $0040` recommended).

## Move encoding

Every move is 3 bytes: `(from, to, flags)`. Flags layout (`mv_flags`):

| Bit | Meaning |
|---|---|
| 0-2 | Promotion piece (0 = none, 1-6 = PNBRQK) |
| 3 | Capture |
| 4 | Double pawn push |
| 5 | En passant |
| 6 | Short castle (O-O) |
| 7 | Long castle (O-O-O) |

In v0.1 only bits 0-3 are produced; en-passant + castling are stubbed
(parsed but rejected with `ERR_NOT_IMPL`).

## Status — v0.4 (current)

| Feature | State |
|---|---|
| Move generation: pawn (single, double, capture) | ✅ |
| Move generation: knight, bishop, rook, queen, king | ✅ |
| Promotion (auto-queen + user-selectable Q/R/B/N) | ✅ |
| Capture handling | ✅ |
| En passant (tracking + capture + undo) | ✅ |
| Castling (KS/QS, both colours, with all checks) | ✅ |
| Check detection (own king after move) | ✅ |
| Checkmate / stalemate | ✅ via brute-force scan |
| Algebraic input parser (auto-complete on 4 chars) | ✅ |
| Auto-detect castling from `E1G1`/`E1C1` syntax | ✅ |
| AI 1-ply greedy (max our material - their material) | ✅ |
| Undo (single-level via compact engine state save) | ✅ |
| **Perft (depth 1)** — `is_attacked_runner`/`ce_piece` aliasing fixed; returns 20 for initial pos | ✅ |
| **Mode cycling (`M`)** — HvH / WAI / BAI / AvA in text variant | ✅ |
| AI (alpha-beta 2-3 ply + MVV-LVA) | ⏳ v1.2 |
| Perft (depth ≥2, recursive) | ⏳ v0.5 (needs ply-stacked saved_*) |

## Perft expected counts (for v0.2 self-test)

From the standard test positions (https://www.chessprogramming.org/Perft_Results):

| Position | depth 1 | depth 2 | depth 3 | depth 4 |
|---|---|---|---|---|
| Initial | 20 | 400 | 8 902 | 197 281 |
| Kiwipete | — | — | 97 862 | 4 085 603 |
| Endgame test 3 | — | — | 9 467 | 422 333 |

`perft(3)` from initial ≈ 6 s at 1 MHz; `perft(4)` ≈ 115 s.

## Use from a variant

```
.include "apple1.inc"
.include "chess_common.inc"
.import init_board, apply_user_move, in_check, game_status, toggle_side
.import board, side_to_move, mv_from, mv_to, mv_promo

main:
        JSR init_board
@loop:  JSR render_board                ; variant-supplied
        JSR read_player_move            ; from chess_text_io.asm
        BCS @special_or_error
        JSR apply_user_move
        BCS @illegal
        JSR game_status
        BNE @over
        JMP @loop
```

See `dev/projects/games_chess/Chess.asm` for the canonical text-mode caller.
