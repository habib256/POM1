#!/usr/bin/env python3
"""
play_littletower_telnet.py — Win Little Tower via POM1 Terminal Card.

Deux façons de préparer l'émulateur :

  GUI (manuel) :
    - Dans POM1 : activer Hardware > P-LAB Terminal Card
    - Charger le jeu : File > Load Memory > software/Apple-1 games/LittleTower-1.0.txt
      (on reste dans Wozmon — le script tape 0280R lui-même)

  Headless (scripté) :
    ./build/POM1 --headless --terminal \
        --load "0280:software/Apple-1 games/LittleTower-1.0.txt"
    (--load réécrit le vecteur reset et lance le jeu : CTRL-R ramène
     directement à l'écran titre du jeu, PAS à Wozmon)

Le script se connecte à localhost:6502, reset (CTRL-R), détecte dans quel
état il retombe (écran titre "1] PLAY  2] HELP" du jeu, ou prompt Wozmon —
auquel cas il tape 0280R), puis envoie la séquence gagnante en se
synchronisant sur le prompt '>' du jeu après chaque commande.

Code retour : 0 = victoire ("YOU WIN"), 1 = séquence désynchronisée,
2 = connexion impossible.
"""

from __future__ import annotations

import select
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 6502

# Terminal Card control byte: CTRL-R triggers Apple-1 reset.
CTRL_R = 18


def recv_avail(sock: socket.socket, total: float = 2.0, idle: float = 0.2) -> bytes:
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


def read_until(sock: socket.socket, token: bytes, timeout: float = 8.0) -> bytes:
    """Accumulate output until `token` shows up (plus a short grace drain),
    or until `timeout`. Returns everything read either way."""
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        r, _, _ = select.select([sock], [], [], 0.15)
        if r:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buf += chunk
            if token in buf:
                buf += recv_avail(sock, total=0.3)
                return buf
    return buf


def send_cmd(sock: socket.socket, line: str, timeout: float = 8.0) -> bytes:
    """Type a game command and wait for the game to re-print its '>' prompt.
    Much more robust than fixed sleeps: a slow host can't desync the run."""
    sock.sendall((line + "\r").encode("ascii"))
    return read_until(sock, b">", timeout=timeout)


def main() -> int:
    try:
        sock = socket.create_connection((HOST, PORT), timeout=6)
    except OSError as e:
        print(f"Erreur: impossible de se connecter à {HOST}:{PORT} ({e})", file=sys.stderr)
        return 2

    out = b""
    try:
        # Drain banner/noise.
        recv_avail(sock, total=0.6)

        # Reset to a known state. Where we land depends on how the game was
        # loaded: --load rewrites the reset vector to $0280, so CTRL-R
        # relaunches the game (title screen + "1] PLAY  2] HELP"); after a
        # GUI File > Load Memory the vector still points at Wozmon.
        sock.sendall(bytes([CTRL_R]))
        boot = read_until(sock, b"1] PLAY", timeout=5.0)
        out += boot

        if b"1] PLAY" not in boot:
            # Wozmon prompt — launch the game ourselves.
            sock.sendall(b"0280R\r")
            out += read_until(sock, b"1] PLAY", timeout=6.0)

        # Intro choice: press '1' = PLAY (single key, no CR — the intro
        # polls the keyboard directly). The game then prints "OK, NOW
        # LET'S BEGIN ...", the room-1 description and its '>' prompt.
        sock.sendall(b"1")
        out += read_until(sock, b">", timeout=8.0)

        cmds = [
            "S",                    # room 2 (lake bank)
            "EXAMINE SKELETON",     # -> key in inventory
            "N",                    # back to room 1 (tower door)
            "USE KEY",              # unlock the door
            "ENTER",                # room 3 (ground floor)
            "GET TORCH",            # -> torch in inventory
            "UP",                   # room 4 (bedroom)
            "EXAMINE PICTURE",      # flavour (the ANAETOSH hint)
            "S",                    # room 5 (dark study)
            "USE TORCH",            # light the room
            "EXAMINE DESK",         # -> silver dagger
            "N",                    # back to room 4
            "UP",                   # room 6 (the vampire; paralysis on)
            "SAY ANAETOSH",         # lift the paralysis
            "USE DAGGER",           # win
        ]

        for c in cmds:
            out += send_cmd(sock, c)

        text = out.decode("latin-1", errors="replace")
        tail = text[-2200:]
        print(tail)

        if "YOU WIN" in text or "CONGRATULATIONS" in text:
            return 0
        return 1
    finally:
        try:
            sock.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
