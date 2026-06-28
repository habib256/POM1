#!/usr/bin/env python3
# Headless self-play regression for sketchs/apple1/game_chess: drive the text
# Chess into AI-vs-AI mode and watch a whole game play itself out over the
# telemetry side channel — no display, no human (CI-friendly via --headless).
#
# How it works:
#   * --paste "X4" dismisses the splash then picks MODE 4 = AVA (AI vs AI).
#   * Chess.asm emits one telemetry DATA frame per move (schema:
#     move,to_move,from,to,status — see tele_emit_move). We decode it BY NAME.
#   * The engine has no draw detection yet, so Chess.asm caps an AvA game at
#     MOVE_CAP full moves and emits a terminal status=4 ("move limit - draw").
#
# Asserts the game makes legal-looking, strictly alternating moves and reaches a
# terminal state (mate / stalemate / move-limit draw) within a bounded number of
# plies. Exits 77 (skip) if POM1 or the built Chess.txt is missing.
#
#   Build : make -C sketchs/apple1/game_chess
#   Run   : python3 tools/test_chess_selfplay.py [path/to/POM1]

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pom1_telemetry import launch_headless, ProtocolError  # noqa: E402

PROG = "software/Apple-1 games/Chess.txt"   # dual-bank Wozmon-hex ($0280 + $E000)
PORT = 6605
MAX_PLIES = 260          # MOVE_CAP=100 full moves -> ~200 plies + slack
FIRST_TIMEOUT = 8.0      # boot + menu + first AI search
MOVE_TIMEOUT = 8.0       # per AI move (1-ply SEE scan, emulated)

# game_status / status field codes (mirror Chess.asm).
ONGOING, WHITE_MATE, BLACK_MATE, STALEMATE, MOVE_LIMIT = 0, 1, 2, 3, 4
TERMINAL = {WHITE_MATE: "white mated", BLACK_MATE: "black mated",
            STALEMATE: "stalemate", MOVE_LIMIT: "move-limit draw"}


def sq(s):
    """0x88 square byte -> algebraic, matching print_square_ax (file=s&7,
    rank=(s>>4)&7)."""
    return f"{chr(ord('A') + (s & 7))}{chr(ord('1') + ((s >> 4) & 7))}"


def main():
    pom1 = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("POM1", "build/POM1")
    if not os.path.exists(pom1):
        print(f"SKIP: {pom1} not found (build first)"); return 77
    if not os.path.exists(PROG):
        print(f"SKIP: {PROG} not found (run: make -C sketchs/apple1/game_chess)"); return 77

    # Splash wants any key; choose_mode wants '4' = AVA. No RETURN needed
    # (wait_key returns on the first keystroke).
    keys = tempfile.NamedTemporaryFile("w", suffix=".keys", delete=False)
    keys.write("X4")
    keys.close()

    try:
        # --cpu-max: run the CPU flat-out. Each move renders the whole board
        # through the Apple-1 ECHO busy-wait (~1000 chars), which crawls at the
        # authentic 1 MHz; self-play has no human to pace it, so uncap it.
        with launch_headless(PROG, load_addr=0x0280, port=PORT, pom1=pom1,
                             extra=["--cpu-max", "--paste", keys.name]) as tc:
            st = tc.read_named(FIRST_TIMEOUT)
            if st is None:
                print("FAIL: no telemetry frame — did AvA mode start? "
                      "(splash/mode --paste may not have landed)")
                return 1
            if not tc.schema:
                print("FAIL: game emitted a frame but no schema — telemetry "
                      "instrumentation missing?")
                return 1

            moves = []
            prev_to_move = None
            for ply in range(MAX_PLIES):
                frm, to = st["from"], st["to"],
                status, to_move = st["status"], st["to_move"]

                # Legality smoke: from/to must be distinct on-board 0x88 squares.
                for name, v in (("from", frm), ("to", to)):
                    if v & 0x88:
                        print(f"FAIL: ply {ply} {name} ${v:02X} is an off-board "
                              f"0x88 square"); return 1
                if frm == to:
                    print(f"FAIL: ply {ply} null move {sq(frm)}{sq(to)}"); return 1

                # to_move is the side to move AFTER this move ($00=white,
                # $80=black), so it must flip every ply (white moves -> black to
                # move -> white to move ...).
                if prev_to_move is not None and to_move == prev_to_move:
                    print(f"FAIL: ply {ply} side did not alternate "
                          f"(to_move stayed {to_move})"); return 1
                prev_to_move = to_move

                moves.append(f"{sq(frm)}{sq(to)}")

                if status != ONGOING:
                    pgn = " ".join(f"{i//2+1}.{m}" if i % 2 == 0 else m
                                   for i, m in enumerate(moves))
                    print(f"PASS: chess played itself to {TERMINAL.get(status, status)} "
                          f"in {len(moves)} plies — headless, no human")
                    print(f"      {pgn}")
                    return 0

                st = tc.read_named(MOVE_TIMEOUT)
                if st is None:
                    print(f"FAIL: stream stalled after {len(moves)} plies "
                          f"(last {moves[-1]}) — no terminal state reached")
                    return 1

            print(f"FAIL: no terminal state within {MAX_PLIES} plies "
                  f"(cap should force a draw far sooner)")
            return 1
    except (OSError, ProtocolError, AssertionError, RuntimeError, KeyError) as e:
        print(f"FAIL: {e}")
        return 1
    finally:
        os.unlink(keys.name)


if __name__ == "__main__":
    sys.exit(main())
