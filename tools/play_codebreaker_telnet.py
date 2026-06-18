#!/usr/bin/env python3
"""Beat Uncle Bernie's CODEBREAKER (Apple-1, MCODE/CODEBREAKERUB#060800)
through the POM1 Terminal Card (localhost:6502).

Le jeu (reverse-engineered depuis les chaînes du binaire 2 Ko) :

  *** UNCLE BERNIE'S CODEBREAKER GAME ***
  CHOOSE YOUR LEVEL:
    0 ROOKIE
    1 MASTER
    2 GENIUS
    H HELP
  ENTER YOUR CHOICE: _
  LEVEL: n, ROUNDS: r, LETTERS: A...X

Duel simultané :
  - L'ordinateur cache un code de 4 lettres  → au joueur de le casser.
  - Le joueur cache un code de 4 lettres     → à l'ordinateur de le casser
                                               via les retours du joueur.
  Retour d'essai (4 chars au total) :
      '*' une lettre à la bonne place
      '+' une lettre correcte mais mal placée
      '-' une lettre absente
  Touche ESC : annule / redémarre le groupe de saisie courant.

Fins possibles visibles dans le ROM :
  YOU WIN !   I WIN !   GAME IS A DRAW.
  NO POSSIBLE SOLUTION.  YOU CHEATED !
  SCORE (YOU:ME) IS nn

Stratégie du bot :
  - Contre le code de la machine → Knuth minimax (premier coup AABB, optimal
    pour 6x4), puis minimax restreint aux candidats encore compatibles.
  - Contre les essais de la machine → on garde en mémoire notre propre
    secret (fixe, cf. MY_SECRET) et on répond *toujours* la vérité —
    mentir déclenche NO POSSIBLE SOLUTION / YOU CHEATED.

Flux observé (capture réelle POM1 v1.8.6) :
      0 <-                              (ROOKIE)
      LEVEL: 0, ROUNDS: 6, LETTERS: A...F
      ...pub Uncle Bernie...
      SCORE (YOU:ME) IS 00:00
      #01 AABB+---  FBBF*---       ← on tape AABB, prog répond '+---'
      #02 ...                        puis '  FBBF' (sa tentative), on tape '*---'

Donc par round :
  1. prompt "#NN " — on tape 4 lettres sans CR
  2. prog imprime nos 4 symboles * + - (notre feedback)
  3. prog imprime "  " + ses 4 lettres (sa tentative sur notre code)
  4. on tape 4 symboles * + - (total = 4) pour son essai
  5. prog imprime CRLF + "#NN+1 "

Pré-requis :
  ./POM1 --preset 3 --terminal --cpu-max \\
         --load 0800:sdcard/PLAB/MCODE/CODEBREAKERUB#060800 --run 0800
Ou, plus simple :
      python3 tools/play_codebreaker_telnet.py --autolaunch
"""
from __future__ import annotations

import argparse
import os
import re
import select
import socket
import subprocess
import sys
import time
from collections import Counter
from itertools import product
from typing import List, Optional, Tuple

HOST = "127.0.0.1"
PORT = 6502

# Notre code secret (que la machine essaie de casser). On le garde fixe pour
# reproductibilité. Éviter "AAAA" trop facile et "ABCD" banal — ABCA a deux
# lettres répétées + position non-triviale, casse les heuristiques naïves.
MY_SECRET_BY_ALPHABET_SIZE = {
    6:  "FADF",   # ROOKIE (A..F) — bord d'alphabet + doublon non-adjacent
    8:  "GHEG",   # MASTER (A..G) mappé sur alphabet 7, sinon GENIUS (A..H)
    10: "JGIJ",   # sécurité si une variante étendait l'alphabet
}
MY_SECRET_DEFAULT = "FADF"

# Niveaux : 0=ROOKIE, 1=MASTER, 2=GENIUS
DEFAULT_LEVEL = "0"

# Timing
KEY_DELAY = 0.05      # entre deux touches (Apple-1 lit char par char)
RECV_IDLE = 0.25      # silence considéré comme "plus rien à lire pour l'instant"
RECV_TOTAL = 3.0      # plafond d'attente par bloc
POLL_ROUNDS = 12      # nb de tentatives pour voir un changement d'écran

LOG = print  # remplaçable


# ------------------------------------------------------------------ Knuth

def feedback(guess: str, code: str) -> Tuple[int, int]:
    black = sum(1 for g, c in zip(guess, code) if g == c)
    shared = sum((Counter(guess) & Counter(code)).values())
    return (black, shared - black)


def best_guess(candidates: List[str], universe: List[str], first: bool) -> str:
    if first:
        # AABB : premier coup optimal prouvé par Knuth pour 6 couleurs 4 positions,
        # reste raisonnable pour d'autres tailles.
        alpha = sorted({c for code in universe for c in code})
        if len(alpha) >= 2:
            return alpha[0] * 2 + alpha[1] * 2
        return alpha[0] * 4
    if len(candidates) == 1:
        return candidates[0]
    if len(candidates) == 2:
        return candidates[0]

    # Minimax restreint aux candidats encore cohérents (rapide, quasi-optimal).
    best = None
    best_score = 10**9
    best_in_S = False
    for g in candidates:
        buckets: dict = {}
        for c in candidates:
            fb = feedback(g, c)
            buckets[fb] = buckets.get(fb, 0) + 1
        mx = max(buckets.values())
        if mx < best_score or (mx == best_score and not best_in_S):
            best_score = mx
            best = g
            best_in_S = True
    assert best is not None
    return best


def filter_candidates(cands: List[str], guess: str, fb: Tuple[int, int]) -> List[str]:
    return [c for c in cands if feedback(guess, c) == fb]


# ------------------------------------------------------------------ I/O

def recv_until_quiet(sock: socket.socket, total: float = RECV_TOTAL,
                     idle: float = RECV_IDLE) -> bytes:
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


def decode(buf: bytes) -> str:
    return buf.decode("latin-1", errors="replace")


def send_str(sock: socket.socket, s: str, per_char_delay: float = KEY_DELAY) -> None:
    for ch in s:
        sock.sendall(ch.encode("ascii"))
        if per_char_delay > 0:
            time.sleep(per_char_delay)


def send_cr(sock: socket.socket) -> None:
    sock.sendall(b"\r")


def wait_for(sock: socket.socket, needles: List[str], total: float = 8.0,
             initial: str = "") -> str:
    """Accumulate output until one of the needles appears, or timeout."""
    buf = initial
    end = time.time() + total
    while time.time() < end:
        chunk = decode(recv_until_quiet(sock, total=1.0, idle=0.25))
        if chunk:
            buf += chunk
        if any(n in buf for n in needles):
            return buf
    return buf


# ------------------------------------------------------------------ Parsing

LETTERS_RE = re.compile(r"LETTERS:\s*A\s*\.\s*\.\s*\.\s*([A-Z])")
ROUNDS_RE = re.compile(r"ROUNDS:\s*(\d+)")


def parse_letters_and_rounds(text: str) -> Tuple[Optional[List[str]], Optional[int]]:
    m = LETTERS_RE.search(text)
    letters = None
    if m:
        last = m.group(1)
        letters = [chr(c) for c in range(ord("A"), ord(last) + 1)]
    r = ROUNDS_RE.search(text)
    rounds = int(r.group(1)) if r else None
    return letters, rounds


def find_4letter_guess(text: str, alphabet_set: set) -> Optional[str]:
    """Detect a 4-letter sequence on its own line — candidate for the computer's
    guess against our secret."""
    for line in reversed(text.replace("\r\n", "\n").replace("\r", "\n").split("\n")):
        s = line.strip()
        # Line may contain the guess followed by feedback symbols or spaces.
        # Take the first contiguous run of 4 alphabet letters.
        m = re.match(r"([A-Z]{4})(?:[\s*+-]|$)", s)
        if m and all(c in alphabet_set for c in m.group(1)):
            return m.group(1)
    return None


def find_feedback_after(text: str, guess: str) -> Optional[Tuple[int, int]]:
    """Look for a feedback symbol run appearing on the same line as `guess` or
    just after it."""
    # Normalize line breaks.
    t = text.replace("\r\n", "\n").replace("\r", "\n")
    idx = t.rfind(guess)
    if idx < 0:
        return None
    tail = t[idx + len(guess):]
    # Grab contiguous * + - ignoring spaces, up to 4 total symbols.
    symbols = []
    for ch in tail:
        if ch in "*+-":
            symbols.append(ch)
            if len(symbols) == 4:
                break
        elif ch in " \t":
            continue
        elif ch == "\n":
            if symbols:
                break
            continue
        else:
            if symbols:
                break
    if not symbols:
        return None
    black = symbols.count("*")
    white = symbols.count("+")
    return (black, white)


# ------------------------------------------------------------------ Game loop

GAME_END = ("YOU WIN", "I WIN", "GAME IS A DRAW", "CHEATED", "NO POSSIBLE SOLUTION")


def play(sock: socket.socket, level: str, my_secret: Optional[str],
         verbose: bool) -> str:
    # Drain terminal banner (printed on connect) and any leftover CodeBreaker output.
    initial = decode(recv_until_quiet(sock, total=1.5, idle=0.3))
    if verbose: LOG(f"[init] {initial!r}")

    # If the CodeBreaker menu isn't visible, poke a CR. If we're already at
    # "ENTER YOUR CHOICE:", the program will answer "?" + redraw prompt — good
    # enough to detect the menu state. If we're at Wozmon, try 800R.
    if "CHOOSE YOUR LEVEL" not in initial and "ENTER YOUR CHOICE" not in initial:
        sock.sendall(b"\r")
        time.sleep(0.4)
        initial += decode(recv_until_quiet(sock, total=1.0, idle=0.3))
        if "ENTER YOUR CHOICE" not in initial:
            # Probably Wozmon — run CodeBreaker at $0800.
            send_str(sock, "800R\r")
            time.sleep(0.5)
            initial += decode(recv_until_quiet(sock, total=2.0, idle=0.3))

    out = wait_for(sock, ["ENTER YOUR CHOICE"], total=6.0, initial=initial)
    if "ENTER YOUR CHOICE" not in out:
        raise RuntimeError(f"No CodeBreaker menu detected.\nLast output:\n{out[-400:]}")

    # Select level
    send_str(sock, level)
    out = wait_for(sock, ["LETTERS:"], total=6.0)
    if verbose: LOG(f"[level]\n{out}")
    alphabet, rounds_limit = parse_letters_and_rounds(out)
    if alphabet is None:
        alphabet = list("ABCDEF")
    LOG(f"[info] alphabet={''.join(alphabet)}  rounds_limit={rounds_limit}")

    alphabet_set = set(alphabet)
    universe = ["".join(p) for p in product(alphabet, repeat=4)]
    candidates = list(universe)

    if my_secret is None:
        my_secret = MY_SECRET_BY_ALPHABET_SIZE.get(len(alphabet), MY_SECRET_DEFAULT)
        # Garantir que my_secret n'utilise que des lettres valides.
        my_secret = "".join(
            c if c in alphabet_set else alphabet[0] for c in my_secret
        )[:4].ljust(4, alphabet[0])
    LOG(f"[info] my secret = {my_secret}")

    # Wait for first "#01 " prompt (round marker). The program emits "#NN "
    # before asking for our 4-letter guess.
    out += wait_for(sock, ["#01"], total=8.0)
    if verbose: LOG(f"[after level]\n{out}")

    first_guess = True
    round_no = 0
    screen = out
    max_rounds = rounds_limit or 10

    while round_no < max_rounds:
        round_no += 1

        # --- Our turn: guess the computer's code (no prompt beyond "#NN ")
        guess = best_guess(candidates, universe, first=first_guess)
        first_guess = False
        LOG(f"[R{round_no:02d}] our guess = {guess}   |S|={len(candidates)}")
        send_str(sock, guess)

        # Program echoes our 4 chars, then prints feedback (4 of * + -),
        # then "  " (2 spaces), then its 4-letter guess against our secret.
        piece = decode(recv_until_quiet(sock, total=3.0, idle=0.3))
        screen += piece
        if verbose: LOG(f"         << {piece!r}")

        # Parse feedback for our guess out of `piece` only (avoid older rounds).
        fb = find_feedback_after(piece, guess)
        tries = 0
        while fb is None and tries < 3:
            more = decode(recv_until_quiet(sock, total=1.5, idle=0.3))
            screen += more
            piece += more
            fb = find_feedback_after(piece, guess)
            tries += 1
        if fb is None:
            LOG("[warn] could not parse our feedback — aborting")
            break
        LOG(f"         feedback * x{fb[0]}  + x{fb[1]}  - x{4-fb[0]-fb[1]}")

        if fb == (4, 0):
            # We cracked it. The program still prints its last guess at our
            # secret + may want our feedback before declaring the winner.
            screen += decode(recv_until_quiet(sock, total=2.0, idle=0.3))
            # Continue so we feed the computer's last guess feedback too.

        candidates = filter_candidates(candidates, guess, fb)
        if not candidates and fb != (4, 0):
            LOG("[bug] candidate set empty — parser lost a symbol")
            break

        # --- Computer's turn: parse its 4-letter guess at our secret. Format
        # observed (real trace): "AABB ----  EEDD " — echo, space, 4 feedback
        # symbols, 2 spaces, computer's 4-letter guess.
        cg_re = re.compile(re.escape(guess) + r"\s*[*+\-]{4}\s+([A-Z]{4})")
        m = cg_re.search(piece)
        if not m:
            more = decode(recv_until_quiet(sock, total=2.5, idle=0.3))
            screen += more
            piece += more
            m = cg_re.search(piece)

        if m:
            comp_guess = m.group(1)
            if not all(c in alphabet_set for c in comp_guess):
                LOG(f"[warn] computer guess {comp_guess} contains letters outside alphabet — ignoring")
                comp_guess = None
        else:
            comp_guess = None

        if comp_guess is None:
            # Game might have ended (YOU WIN!/I WIN!/DRAW) before the computer
            # could guess again.
            if any(e in screen for e in GAME_END):
                break
            LOG("[warn] no computer guess detected — checking for end state")
            screen += decode(recv_until_quiet(sock, total=2.0, idle=0.3))
            if any(e in screen for e in GAME_END):
                break
            LOG("[warn] no end state either — aborting")
            break

        cfb = feedback(comp_guess, my_secret)
        reply = "*" * cfb[0] + "+" * cfb[1] + "-" * (4 - cfb[0] - cfb[1])
        LOG(f"         computer guess = {comp_guess}  reply = {reply}")
        send_str(sock, reply)

        # Wait for either end of game text or next "#NN " prompt.
        piece2 = decode(recv_until_quiet(sock, total=3.0, idle=0.3))
        screen += piece2
        if verbose: LOG(f"         << {piece2!r}")

        if any(e in screen for e in GAME_END):
            break

    # Drain end-of-game output.
    screen += decode(recv_until_quiet(sock, total=4.0, idle=0.4))
    LOG("=" * 60)
    LOG(screen[-900:])
    for e in GAME_END:
        if e in screen:
            return e
    return "UNKNOWN"


# ------------------------------------------------------------------ Launcher

def maybe_autolaunch(repo_root: str) -> Optional[subprocess.Popen]:
    """Spawn POM1 with CodeBreaker preloaded at $0800."""
    rom = os.path.join(repo_root, "sdcard", "PLAB", "MCODE",
                       "CODEBREAKERUB#060800")
    if not os.path.isfile(rom):
        raise FileNotFoundError(rom)
    exe = os.path.join(repo_root, "build", "POM1")
    if not os.path.isfile(exe):
        raise FileNotFoundError(f"{exe} — build POM1 first")
    cmd = [exe, "--preset", "3", "--terminal", "--cpu-max",
           "--load", f"0800:{rom}", "--run", "0800"]
    LOG(f"[launch] {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, cwd=repo_root,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    time.sleep(3.5)  # let it boot + plug peripherals + run
    return proc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--level", default=DEFAULT_LEVEL,
                    choices=["0", "1", "2"], help="0=ROOKIE 1=MASTER 2=GENIUS")
    ap.add_argument("--secret", default=None,
                    help="notre code secret à 4 lettres (par défaut selon l'alphabet)")
    ap.add_argument("--autolaunch", action="store_true",
                    help="lance POM1 avec CodeBreaker déjà chargé à $0800")
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    proc: Optional[subprocess.Popen] = None
    if args.autolaunch:
        repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        proc = maybe_autolaunch(repo_root)

    try:
        sock = socket.create_connection((args.host, args.port), timeout=10)
        try:
            verdict = play(sock, args.level, args.secret, args.verbose)
            LOG(f"\n=== {verdict} ===")
            return 0 if "YOU WIN" in verdict else 1
        finally:
            sock.close()
    finally:
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    sys.exit(main())
