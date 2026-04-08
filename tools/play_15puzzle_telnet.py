#!/usr/bin/env python3
"""Connect to POM1 Terminal Card (localhost:6502) and solve 15-Puzzle via BFS.

Flux du jeu (important pour l’automatisation) :
  1. Lancer le programme depuis le moniteur Woz : ``0300R`` puis **Entrée** (CR).
  2. Le jeu affiche **INSTRUCTIONS (Y/N)?** — il faut taper **Y** ou **N**
     (**une seule touche**, sans Entrée) avant tout le reste.
  3. Ensuite seulement : **DIFFICULTY LEVEL (1-5)?** puis une touche **1**–**5**.
  4. Pendant la partie : une **lettre** A–O par coup (pas d’Entrée).

Entre deux parties / niveaux, un **CTRL-R** (octet 18, Terminal Card) ramène souvent
directement au titre avec l’invite INSTRUCTIONS si le jeu est encore en RAM à $0300.
"""
from __future__ import annotations

import re
import select
import socket
import time
from collections import deque
import heapq
from typing import Dict, List, Optional, Tuple

HOST = "127.0.0.1"
PORT = 6502

GOAL = "ABCDEFGHIJKLMNO "  # space last (bottom-right)

# Après 0300R : répondre à INSTRUCTIONS (Y/N)? — « N » saute le texte long.
INSTRUCTIONS_REPLY = "N"


def recv_avail(sock: socket.socket, total: float = 4.0, idle: float = 0.2) -> bytes:
    end = time.time() + total
    buf = b""
    while time.time() < end:
        r, _, _ = select.select([sock], [], [], idle)
        if r:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buf += chunk
        elif buf:
            break
    return buf


def parse_board(text: str) -> Optional[str]:
    """Extract 4 lines of 4 chars (A-O or space) from terminal output."""
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    grid_lines: List[str] = []
    for line in lines:
        # Do not strip spaces — rows like "ABC " are valid 4-wide lines.
        line = line.rstrip("\r\n").rstrip("\t")
        if len(line) != 4:
            continue
        if not re.match(r"^[A-O ]{4}$", line):
            continue
        grid_lines.append(line)
        if len(grid_lines) == 4:
            return "".join(grid_lines)
    return None


def apply_move(board: str, letter: str) -> Optional[str]:
    """Slide letter toward space; board is 16 chars row-major. Returns new board or None."""
    if len(board) != 16 or letter == " " or letter not in board:
        return None
    si = board.index(" ")
    li = board.index(letter)
    sr, sc = divmod(si, 4)
    lr, lc = divmod(li, 4)
    if sr != lr and sc != lc:
        return None
    b = list(board)
    if sr == lr:
        r = sr
        if sc < lc:
            for c in range(sc, lc):
                b[r * 4 + c] = b[r * 4 + c + 1]
            b[r * 4 + lc] = " "
        else:
            for c in range(sc, lc, -1):
                b[r * 4 + c] = b[r * 4 + c - 1]
            b[r * 4 + lc] = " "
    else:
        c = sc
        if sr < lr:
            for r in range(sr, lr):
                b[r * 4 + c] = b[(r + 1) * 4 + c]
            b[lr * 4 + c] = " "
        else:
            for r in range(sr, lr, -1):
                b[r * 4 + c] = b[(r - 1) * 4 + c]
            b[lr * 4 + c] = " "
    return "".join(b)


def iter_moves(state: str) -> List[Tuple[str, str]]:
    """Return (next_state, letter_typed) for each legal slide."""
    si = state.index(" ")
    sr, sc = divmod(si, 4)
    out: List[Tuple[str, str]] = []
    seen_ch: set[str] = set()
    for c in range(4):
        idx = sr * 4 + c
        ch = state[idx]
        if ch == " " or ch in seen_ch:
            continue
        seen_ch.add(ch)
        nxt = apply_move(state, ch)
        if nxt is not None:
            out.append((nxt, ch))
    for r in range(4):
        idx = r * 4 + sc
        ch = state[idx]
        if ch == " " or ch in seen_ch:
            continue
        seen_ch.add(ch)
        nxt = apply_move(state, ch)
        if nxt is not None:
            out.append((nxt, ch))
    return out


def bfs_moves(start: str) -> List[str]:
    if start == GOAL:
        return []
    q: deque[str] = deque([start])
    parent: dict[str, Tuple[str, str]] = {}  # state -> (prev_state, letter)
    seen = {start}
    while q:
        cur = q.popleft()
        for nxt, letter in iter_moves(cur):
            if nxt in seen:
                continue
            seen.add(nxt)
            parent[nxt] = (cur, letter)
            if nxt == GOAL:
                moves: List[str] = []
                back = nxt
                while back != start:
                    prev, mv = parent[back]
                    moves.append(mv)
                    back = prev
                moves.reverse()
                return moves
            q.append(nxt)
    raise RuntimeError("No solution (unsolvable or apply_move mismatch)")

def heuristic(state: str) -> int:
    """Heuristique simple pour guider la recherche (pas forcément admissible)."""
    # Somme des distances de Manhattan des tuiles à leur position cible.
    # Ici un coup peut déplacer le vide de plusieurs cases d’un coup, donc cette
    # heuristique surestime parfois peu/ beaucoup; on l’utilise en A* “pratique”.
    total = 0
    for i, ch in enumerate(state):
        if ch == " ":
            continue
        goal_i = ord(ch) - ord("A")
        r1, c1 = divmod(i, 4)
        r2, c2 = divmod(goal_i, 4)
        total += abs(r1 - r2) + abs(c1 - c2)
    return total


def astar_moves(start: str, max_expansions: int = 2_000_000) -> List[str]:
    """Recherche A* pour éviter l’explosion du BFS sur les niveaux durs."""
    if start == GOAL:
        return []
    h0 = heuristic(start)
    heap: List[Tuple[int, int, str]] = [(h0, 0, start)]  # (f, g, state)
    came_from: Dict[str, Tuple[str, str]] = {}
    best_g: Dict[str, int] = {start: 0}
    expansions = 0
    t0 = time.time()

    while heap:
        f, g, cur = heapq.heappop(heap)
        if cur == GOAL:
            moves: List[str] = []
            back = cur
            while back != start:
                prev, mv = came_from[back]
                moves.append(mv)
                back = prev
            moves.reverse()
            return moves

        if g != best_g.get(cur, 10**9):
            continue

        expansions += 1
        if expansions % 200_000 == 0:
            dt = time.time() - t0
            rate = expansions / dt if dt > 0 else 0.0
            print(f"A*: {expansions} expansions, open={len(heap)}, {rate:.0f}/s", flush=True)
        if expansions >= max_expansions:
            raise RuntimeError(f"A* gave up after {max_expansions} expansions (level too hard?)")

        for nxt, letter in iter_moves(cur):
            ng = g + 1
            if ng < best_g.get(nxt, 10**9):
                best_g[nxt] = ng
                came_from[nxt] = (cur, letter)
                nf = ng + heuristic(nxt)
                heapq.heappush(heap, (nf, ng, nxt))

    raise RuntimeError("A*: no solution found")


def solve_moves(start: str) -> List[str]:
    """Choisit un solveur qui tient au niveau 5."""
    # BFS est parfois instantané, mais peut aussi “geler” (explosion d’états)
    # sur certains mélanges. Pour une exécution robuste niveau 1→5, on utilise A*.
    return astar_moves(start)


def main() -> None:
    def solve_level_over_new_connection(level: int) -> int:
        """Open a fresh TCP connection per level (reset can drop the socket)."""
        sock = socket.create_connection((HOST, PORT), timeout=10)
        try:
            def send_key(k: str) -> None:
                sock.sendall(k.encode("ascii"))

            def send_byte(b: int) -> None:
                sock.sendall(bytes([b & 0xFF]))

            def send_wz(cmd: str) -> str:
                send_key(cmd + "\r")
                time.sleep(0.15)
                return recv_avail(sock, 6.0).decode("latin-1", errors="replace")

            def sync_to_instructions_prompt() -> str:
                out = recv_avail(sock, 1.0).decode("latin-1", errors="replace")  # banner
                for _ in range(8):
                    if "INSTRUCTIONS" in out:
                        return out

                    # Force reset-to-title (often lands on 15-puzzle title directly)
                    try:
                        send_byte(18)  # CTRL-R
                    except OSError:
                        return out
                    time.sleep(0.6)
                    out += recv_avail(sock, 3.0).decode("latin-1", errors="replace")
                    if "INSTRUCTIONS" in out:
                        return out

                    # If reset didn't yield it, try Woz launch unconditionally.
                    send_key("\r")
                    recv_avail(sock, 0.5)
                    out += send_wz("0300R")
                    time.sleep(0.2)
                    out += recv_avail(sock, 4.0).decode("latin-1", errors="replace")

                return out

            out = sync_to_instructions_prompt()
            if "INSTRUCTIONS" not in out:
                raise RuntimeError("No INSTRUCTIONS prompt. Output:\n" + out[-1200:])

            send_key(INSTRUCTIONS_REPLY)  # single key, no CR
            time.sleep(0.2)
            out = recv_avail(sock, 4.0).decode("latin-1", errors="replace")
            if "DIFFICULTY" not in out:
                out += recv_avail(sock, 4.0).decode("latin-1", errors="replace")
            if "DIFFICULTY" not in out:
                raise RuntimeError("No DIFFICULTY prompt. Output:\n" + out[-1200:])

            send_key(str(level))  # single digit, no CR
            time.sleep(0.25)
            out = recv_avail(sock, 5.0).decode("latin-1", errors="replace")

            board = parse_board(out)
            if board is None:
                out += recv_avail(sock, 3.0).decode("latin-1", errors="replace")
                board = parse_board(out)
            if board is None:
                raise RuntimeError("Could not parse board. Output:\n" + out[-1200:])

            moves = solve_moves(board)
            for mv in moves:
                send_key(mv)
                time.sleep(0.015)
                recv_avail(sock, 0.25)

            time.sleep(0.25)
            recv_avail(sock, 2.0)
            return len(moves)
        finally:
            try:
                sock.close()
            except Exception:
                pass

    results: List[Tuple[int, int]] = []
    for level in range(1, 6):
        print(f"Starting level {level}...", flush=True)
        moves = solve_level_over_new_connection(level)
        results.append((level, moves))
        print(f"Level {level}: solved in {moves} moves", flush=True)

    total = sum(m for _, m in results)
    print(f"Done: levels 1-5 solved, total moves {total}", flush=True)


if __name__ == "__main__":
    main()
