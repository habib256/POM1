# lib/games/chess — Apple-1 Chess engine (shared across text/TMS9918/HGR variants)

*[← lib/games index](../README.md)*

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
  `rook_offsets`, `bishop_offsets`), `starting_position`
  (rank-major 64-byte initial board), `piece_letters` (ASCII glyph per piece).
  (Material values now live as `mat_simple` inside `chess_engine.asm`.)
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
ce_dirs_left, ce_dir_ptr, ce_match, attacker_color, attacked_sq, atk_piece` —
these are declared inside the engine's `.segment "ZEROPAGE"` and consume 11
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

All eight flag values are produced: en-passant and castling are fully
implemented (the engine validates and executes both). `ERR_NOT_IMPL`
is retained as a reserved error code but is no longer returned.

## Status — v0.5 (current)

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
| **AI 2-ply minimax (negamax)** — default, v0.6        | ✅ sees the opponent's reply: stops hanging pieces, wins free material, finds mate-in-1. Blunder rate ~8 % → ~1 % vs the old 1-ply. |
| **AI 1-ply greedy** — `D`-toggled "FAST" mode          | ✅ max `our material − their material`; instant, weaker. |
| **AI castling** (v0.5.2)                               | ✅ the search enumerates + scores the two castles (`CASTLE_BONUS`). |
| **AI random tie-break via 8-bit LFSR**                 | ✅ equal-scored moves diverge AvA games. |
| **AI strategy toggle** (FAST 1-ply / STRONG 2-ply)     | ✅ via the `D` command at the prompt. |
| Undo (single-level via compact engine state save)   | ✅ — note: `H` (hint) consumes the slot |
| **Perft (depth 1)** — returns 20 for initial pos    | ✅ cross-checked vs python-chess at every self-play ply (`tools/test_chess_quality.py`) |
| **Mode cycling (`M`)** — HvH / WAI / BAI / AvA in text variant | ✅ |
| **AI move legality / self-play (AvA)**              | ✅ **fixed v0.5.1** — `find_min_attacker` looped forever (see below); the AI (default) now moves. Verified end-to-end by `tools/test_chess_selfplay.py`. |
| AI (alpha-beta, depth ≥3, killer/TT)                | ⏳ — 2-ply has no pruning yet; ~35× the 1-ply node count (a few seconds/move at 1 MHz). |
| Perft (depth ≥2, recursive)                         | ⏳ (needs ply-stacked saved_*; the 2-ply search uses a single extra `saved2_*` buffer) |
| 50-move / threefold-repetition / insufficient-material draws | ⏳ |

### v0.6 internals — 2-ply search

`ai_play_move` enumerates our legal moves and scores each through `score_move`,
which branches on the `ai_strategy` byte:

- `AI_STRATEGY_NAIVE = 0` ("FAST") — score = `evaluate_material` after our move
  (1-ply greedy). Instant but blunders.
- `AI_STRATEGY_SMART = 1` ("STRONG", default, set in `init_board`) — **2-ply
  negamax**: make our move, `toggle_side`, then `best_reply_eval` scans the
  opponent's legal replies and returns the best material they can reach; our
  score is its negation. Leaf detection returns −127 when the opponent has no
  legal reply and is in check (we deliver mate) and 0 for stalemate, so the AI
  finds mate-in-1 and grabs hanging material instead of getting fooled by the
  shallow per-square exchange estimate it replaces.

The single-level `make_move`/`unmake_move` is made to nest exactly one ply by
copying the outer move's undo slots to a second buffer (`push_saved` →
`saved2_*`) and snapshotting the outer move bytes (`m_*`) across the inner reply
loop, which reuses `mv_from/mv_to`. Equal-scored moves still break ties with the
8-bit LFSR (Galois, taps `$1D`, seeded `$AC` in `init_board`) so AvA games
diverge. This **replaced** v0.5's Static-Exchange-Evaluation + MVV-LVA heuristic
(and its `find_min_attacker`/`see_estimate` helpers) — 2-ply subsumes them and
is both stronger and smaller.

> **v0.5.1 bug fix — `find_min_attacker` infinite loop (the old SEE; SMART AI never moved).**
> The routine scans the 0x88 board with `X` as the index and calls
> `attacks_target` per square — but the sliding-piece probes (`atk_rook`/
> `atk_bishop`) walk rays with their *own* `LDX #$00 .. INX .. CPX #$04`, so they
> return with `X` clobbered. On a hit, `X` came back as the attacker's direction
> index (0–3), and the loop's `INX` resumed scanning from there, re-finding the
> same attacker forever. Because `find_min_attacker` is reached **only** through
> SMART's `see_estimate`, every AI move hung the moment SEE found a contested
> destination — so AvA self-play (and any AI side) wedged on its first move while
> NAIVE was unaffected. Fix: save/restore `X` around the `attacks_target` call
> (push/pull preserves its carry result) and take the attacker piece from
> `atk_piece` rather than the also-clobbered `Y`. Pinned by
> `tools/test_chess_selfplay.py`.

(The `find_min_attacker`/`see_estimate` helpers this bug lived in were removed in
v0.6 when 2-ply replaced SEE.) `is_pseudo_legal`, `make_move`, `unmake_move` are
exported so variant code (Chess.asm's `do_list_moves`, `do_hint`) can enumerate
legal moves without re-implementing move-gen.

## Engine-quality validation (`tools/test_chess_quality.py`)

Beyond "does self-play run", this harness proves the engine plays *correct*
chess by replaying every self-play move into **python-chess** (an independent
reference) over the telemetry channel. The game now emits one extra telemetry
field per move — `legal` = `perft(1)` of the resulting position — and folds a
harness-supplied inbound byte into the LFSR seed at `new_game`, so each game
diverges and the suite covers many positions. Three correctness gates:

1. **Legality** — every move the engine plays is legal in python-chess.
2. **Move generation** — the engine's `perft(1)` equals python-chess's
   legal-move count at every ply (promotions reconciled, see the note).
3. **Terminal** — engine mate/stalemate ⇔ `board.is_checkmate()/is_stalemate()`.

Result: **legality, move generation and terminal detection are all exact.** Over
a 12-game / 2003-ply sample (`illegal=perft_mismatch=terminal_mismatch=0`) the
engine never plays or claims an illegal move, its `perft(1)` matches the
reference at every ply (castling and en-passant included), and its
mate/stalemate calls are correct.

> **v0.5.2 — the AI now castles.** Previously `ai_play_move`, `perft1` and
> `game_status` shared a `(from,to)` scan that only emits 1-square king steps, so
> castling was never *enumerated*: the AI couldn't castle, and `game_status`
> could mis-score a castle-only position as mate/stalemate (latent false
> terminal). Fix: `apply_castle_move` is split into a reusable `castle_try`
> (validate + make, no permanent bookkeeping) + the finaliser; a `try_one_castle`
> helper feeds the two castle candidates into all three routines. `ai_play_move`
> scores a legal castle as `material + CASTLE_BONUS` (=1) so it prefers castling
> to a quiet move but still yields to a real capture; `perft1` counts each legal
> castle; `game_status` checks for a legal castle before declaring a terminal.
> Pinned by `tools/test_chess_quality.py` (the perft cross-check now *includes*
> castling on both sides).

**Counting note (not a failure):** `perft1` counts a pawn promotion as one move
(it sets `mv_promo=0`) where standard perft counts four (Q/R/B/N); the harness
reconciles this by collapsing promotions on a `(from,to)` square when comparing.

Quality (informational, never fails the run): the harness also reports a rough
"hangs a piece on a quiet move" rate. The default **2-ply** AI sits near **~1 %**
(it sees the recapture); the `D`-toggled 1-ply FAST mode is the old **~8 %**.
Promotions, en-passant and castling validate clean. 2-ply self-play games run
markedly longer and reach checkmate far more often than the 1-ply shuffle-to-a-
draw games (e.g. the committed `test_chess_selfplay.py` game now mates in ~92
plies vs ~30 before).

## Perft reference counts

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

See `sketchs/apple1/game_chess/Chess.asm` for the canonical text-mode caller.
