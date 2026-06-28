#!/usr/bin/env python3
# Engine-QUALITY harness for sketchs/apple1/game_chess: drive the 6502 chess
# engine into AI-vs-AI self-play (headless, over the telemetry side channel) and
# validate every move against an INDEPENDENT oracle (python-chess). This is the
# correctness counterpart to tools/test_chess_selfplay.py (which only checks the
# stream is well-formed): here we prove the engine's move generation, legality
# and terminal detection actually agree with a reference implementation.
#
# Three correctness gates (any failure => non-zero exit):
#   1. LEGALITY     — every move the engine plays is legal in python-chess.
#   2. MOVE-GEN      — the engine's perft(1) (the telemetry `legal` field) equals
#                      python-chess's legal-move count for the SAME position, at
#                      every ply. Catches missing/spurious move generation.
#   3. TERMINAL      — when the engine reports mate/stalemate, python-chess agrees
#                      (is_checkmate / is_stalemate); a move-limit draw is allowed.
#
# Plus informational QUALITY metrics (never fail the run): game lengths, decisive
# vs drawn, special-move coverage (castle / en-passant / promotion), and a rough
# one-ply "blunder" rate (moved piece left hanging to a cheaper attacker) — a
# sanity check that the SMART AI's Static Exchange Evaluation is doing something.
#
# The harness perturbs the engine's LFSR seed through the telemetry inbound FIFO
# (Chess.asm folds an inbound byte into ai_rng at new_game), so each self-play
# game is DIFFERENT — broad position coverage instead of one fixed game.
#
#   Build : make -C sketchs/apple1/game_chess
#   Oracle: pip install python-chess   (the test SKIPs (77) without it)
#   Run   : python3 tools/test_chess_quality.py [N_GAMES] [path/to/POM1]

import os
import sys
import time
import socket
import subprocess
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pom1_telemetry import TelemetryClient, ProtocolError  # noqa: E402

try:
    import chess  # python-chess
except ImportError:
    print("SKIP: python-chess not installed (pip install python-chess)")
    sys.exit(77)

PROG = "software/Apple-1 games/Chess.txt"
PORT = 6640
DEFAULT_GAMES = 25
PIECE_VAL = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3,
             chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 100}

# status field codes (mirror Chess.asm)
ONGOING, WHITE_MATE, BLACK_MATE, STALEMATE, MOVE_LIMIT = 0, 1, 2, 3, 4


def to_py(sq):
    """0x88 engine square -> python-chess square (white on ranks 0-1, same as
    the engine; file = sq&7, rank = (sq>>4)&7)."""
    return ((sq >> 4) & 7) * 8 + (sq & 7)


def engine_perft(board):
    """Reproduce the engine's perft(1) COUNTING convention so the cross-check
    compares move generation, not counting style: the engine scans (from,to)
    pairs with mv_promo=0, so the four promotion choices on one square collapse
    to a SINGLE move; castling now IS enumerated (a distinct king (from,to)).
    Returns (engine_equivalent_count, castling_moves_available)."""
    pairs = set()
    castles = 0
    for m in board.legal_moves:
        if board.is_castling(m):
            castles += 1
        pairs.add((m.from_square, m.to_square))    # collapse promotions
    return len(pairs), castles


def find_legal(board, frm, to):
    """Return the legal python-chess move matching engine from/to (queen for a
    promotion, since the engine auto-queens), or None if no legal move matches."""
    cand = [m for m in board.legal_moves
            if m.from_square == frm and m.to_square == to]
    if not cand:
        return None
    for m in cand:
        if m.promotion in (None, chess.QUEEN):
            return m
    return cand[0]


def is_blunder(board, move):
    """Rough 1-ply blunder check BEFORE pushing `move`: does it leave the moved
    piece attacked by a cheaper enemy piece and undefended (a hung piece)?"""
    mover = board.piece_at(move.from_square)
    if mover is None or board.is_capture(move):
        return False                      # ignore captures (SEE territory)
    nb = board.copy(stack=False)
    nb.push(move)
    them = not nb.turn                     # side that just moved
    dest = move.to_square
    attackers = nb.attackers(not them, dest)   # enemy pieces hitting the dest
    if not attackers:
        return False
    defenders = nb.attackers(them, dest)
    mv = PIECE_VAL[mover.piece_type]
    cheapest = min(PIECE_VAL[nb.piece_at(a).piece_type] for a in attackers)
    return cheapest < mv or not defenders


class Stats:
    def __init__(self):
        self.games = self.plies = self.illegal = self.perft_bad = 0
        self.terminal_bad = self.castles = self.eps = self.promos = 0
        self.blunders = self.quiet = 0
        self.mates = self.stalemates = self.cap_draws = 0
        self.castle_omitted = self.promo_positions = 0
        self.lengths = []


def launch(pom1, keys_path):
    args = [pom1, "--headless", "--telemetry-port", str(PORT),
            "--load", f"0280:{PROG}", "--run", "0280", "--cpu-max",
            "--paste", keys_path]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def play(tc, n_games, st):
    """Read the continuous self-play stream; validate each game vs python-chess."""
    board = chess.Board()
    first = True
    while st.games < n_games:
        s = tc.read_named(10.0)
        if s is None:
            raise RuntimeError(f"stream stalled after {st.plies} plies "
                               f"({st.games} games done)")
        frm, to = to_py(s["from"]), to_py(s["to"])
        status, legal = s["status"], s["legal"]

        if status == MOVE_LIMIT:
            # Re-emit of the last move with status=4 (no NEW move). Cross-check
            # perft of the current position, then close the game.
            eq, castles = engine_perft(board)
            st.castle_omitted += castles
            if legal != eq:
                st.perft_bad += 1
                print(f"  PERFT MISMATCH (cap pos): engine={legal} "
                      f"oracle_equiv={eq} fen={board.fen()}")
            st.cap_draws += 1
            st.lengths.append(board.ply())
            st.games += 1
            board = chess.Board(); first = True
            continue

        mover_was = board.turn
        move = find_legal(board, frm, to)
        if move is None:
            st.illegal += 1
            print(f"  ILLEGAL: {chess.SQUARE_NAMES[frm]}{chess.SQUARE_NAMES[to]} "
                  f"not legal for {'White' if mover_was else 'Black'} "
                  f"fen={board.fen()}")
            # Can't continue this game coherently; resync on next terminal.
            if status != ONGOING:
                st.games += 1; board = chess.Board(); first = True
            else:
                board = chess.Board(); first = True
            continue

        # Quality: blunder / special-move accounting (before push).
        if board.is_castling(move):    st.castles += 1
        if board.is_en_passant(move):  st.eps += 1
        if move.promotion:             st.promos += 1
        if is_blunder(board, move):    st.blunders += 1
        else:                          st.quiet += 1

        board.push(move)
        st.plies += 1
        first = False

        # Gate 2: move-gen cross-check (perft(1) of the resulting position),
        # using the engine's own counting convention so we test generation only.
        eq, castles = engine_perft(board)
        st.castle_omitted += castles
        if any(m.promotion for m in board.legal_moves):
            st.promo_positions += 1
        if legal != eq:
            st.perft_bad += 1
            print(f"  PERFT MISMATCH after {move.uci()}: engine={legal} "
                  f"oracle_equiv={eq} (raw {board.legal_moves.count()}, "
                  f"castling {castles}) fen={board.fen()}")

        if status != ONGOING:
            # Gate 3: terminal agreement.
            if status in (WHITE_MATE, BLACK_MATE):
                if not board.is_checkmate():
                    st.terminal_bad += 1
                    print(f"  TERMINAL: engine says mate but oracle disagrees "
                          f"fen={board.fen()}")
                else:
                    st.mates += 1
            elif status == STALEMATE:
                if not board.is_stalemate():
                    st.terminal_bad += 1
                    print(f"  TERMINAL: engine says stalemate but oracle "
                          f"disagrees fen={board.fen()}")
                else:
                    st.stalemates += 1
            st.lengths.append(board.ply())
            st.games += 1
            board = chess.Board(); first = True


def main():
    args = [a for a in sys.argv[1:]]
    n_games = int(args[0]) if args and args[0].isdigit() else DEFAULT_GAMES
    rest = [a for a in args if not a.isdigit()]
    pom1 = rest[0] if rest else os.environ.get("POM1", "build/POM1")

    if not os.path.exists(pom1):
        print(f"SKIP: {pom1} not found (build first)"); return 77
    if not os.path.exists(PROG):
        print(f"SKIP: {PROG} not found (make -C sketchs/apple1/game_chess)"); return 77

    keys = tempfile.NamedTemporaryFile("w", suffix=".keys", delete=False)
    keys.write("X4"); keys.close()        # dismiss splash, pick MODE 4 = AVA

    st = Stats()
    proc = launch(pom1, keys.name)
    tc = TelemetryClient(port=PORT)
    try:
        deadline = time.monotonic() + 10
        while True:
            if proc.poll() is not None:
                print("FAIL: POM1 exited early"); return 1
            try:
                tc.sock = socket.create_connection(("127.0.0.1", PORT), 1.0); break
            except OSError:
                if time.monotonic() >= deadline:
                    print("FAIL: telemetry port never opened"); return 1
                time.sleep(0.2)

        # Perturb the LFSR per game: distinct non-zero seed bytes, one popped at
        # each new_game, so the N games diverge. (Extra bytes harmlessly queue.)
        tc.send(bytes((i * 7 + 1) & 0xFF or 0x5A for i in range(n_games + 4)))

        play(tc, n_games, st)
    except (OSError, ProtocolError, RuntimeError, KeyError, ValueError) as e:
        print(f"FAIL: {e}")
        return 1
    finally:
        tc.close(); proc.terminate()
        try: proc.wait(3)
        except subprocess.TimeoutExpired: proc.kill()
        os.unlink(keys.name)

    # --- Report ---------------------------------------------------------------
    avg = sum(st.lengths) / len(st.lengths) if st.lengths else 0
    print(f"\nGames {st.games}  plies {st.plies}  "
          f"avg {avg:.1f}  (min {min(st.lengths, default=0)} "
          f"max {max(st.lengths, default=0)})")
    print(f"Outcomes: {st.mates} checkmate, {st.stalemates} stalemate, "
          f"{st.cap_draws} move-limit draw")
    print(f"Coverage: {st.castles} castling, {st.eps} en-passant, "
          f"{st.promos} promotion moves seen "
          f"({st.castle_omitted} castles were legal across all positions)")
    print(f"Counting note (not a failure): perft1 collapses the 4 promotion "
          f"choices to 1 move (reconciled), seen in {st.promo_positions} positions.")
    total_q = st.blunders + st.quiet
    if total_q:
        print(f"Quality:  {st.blunders}/{total_q} quiet moves hang a piece "
              f"({100*st.blunders/total_q:.1f}% rough blunder rate)")
    print(f"Correctness: illegal={st.illegal}  perft_mismatch={st.perft_bad}  "
          f"terminal_mismatch={st.terminal_bad}")

    ok = (st.illegal == 0 and st.perft_bad == 0 and st.terminal_bad == 0
          and st.plies > 0)
    print("\n" + ("PASS: engine move generation, legality and terminal "
                  "detection match python-chess across every validated ply"
                  if ok else "FAIL: see discrepancies above"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
