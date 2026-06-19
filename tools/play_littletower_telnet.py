#!/usr/bin/env python3
"""
play_littletower_telnet.py — Win Little Tower via POM1 Terminal Card.

Pré-requis :
  - Dans POM1 : activer Hardware > P-LAB Terminal Card
  - Charger le jeu : File > Load Memory > software/Apple-1 games/LittleTower-1.0.txt
  - Le programme est ensuite exécutable via 0280R

Ce script se connecte à localhost:6502, lance le jeu, puis envoie la séquence
de commandes qui mène à la victoire.
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


def send_line(sock: socket.socket, line: str, wait: float = 0.55, read_t: float = 2.5) -> bytes:
    sock.sendall((line + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t)


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

        # Reset to known state (Woz monitor prompt usually).
        sock.sendall(bytes([CTRL_R]))
        time.sleep(0.9)
        out += recv_avail(sock, total=1.2)

        # Launch from Woz: 0280R + CR
        out += send_line(sock, "0280R", wait=0.7, read_t=4.0)

        # Intro choice: press '1' (single key, no CR).
        sock.sendall(b"1")
        time.sleep(0.7)
        out += recv_avail(sock, total=3.5)

        cmds = [
            "S",
            "EXAMINE SKELETON",
            "N",
            "USE KEY",
            "ENTER",
            "GET TORCH",
            "UP",
            "EXAMINE PICTURE",
            "S",
            "USE TORCH",
            "EXAMINE DESK",
            "N",
            "UP",
            "SAY ANAETOSH",
            "USE DAGGER",
        ]

        for c in cmds:
            out += send_line(sock, c, wait=0.6, read_t=3.5)

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

