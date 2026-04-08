#!/usr/bin/env python3
"""
play_hamurabi_telnet.py — Play Hamurabi (Apple-1 BASIC) via POM1 Terminal Card.

Pré-requis :
  - Dans POM1 : activer Hardware > P-LAB Terminal Card
  - Charger le programme : File > Load Memory > software/basic/hamurabi.apl.txt

Ce script se connecte à localhost:6502, lance RUN, puis répond automatiquement
aux 4 questions annuelles :
  1) ACRES TO BUY
  2) ACRES TO SELL
  3) BUSHELS TO FEED
  4) ACRES TO PLANT WITH SEED

Objectif : éviter famine/émeute et finir la partie correctement (stratégie prudente).
"""

from __future__ import annotations

import re
import select
import socket
import sys
import time
from dataclasses import dataclass
from typing import Optional, Tuple

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18  # Terminal Card reset


def recv_avail(sock: socket.socket, total: float = 2.0, idle: float = 0.2) -> str:
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
    return buf.decode("latin-1", errors="replace")


def send_line(sock: socket.socket, line: str, wait: float = 0.55, read_t: float = 3.5) -> str:
    sock.sendall((line + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t)


@dataclass(frozen=True)
class State:
    year: int
    pop: int
    acres: int
    grain: int
    price: int


RE_YEAR = re.compile(r"IN YEAR\\s+(\\d+)", re.I)
RE_POP = re.compile(r"POPULATION IS NOW\\s+(\\d+)", re.I)
RE_ACRES = re.compile(r"OWNS\\s+(\\d+)\\s+ACRES", re.I)
RE_GRAIN = re.compile(r"YOU NOW HAVE\\s+(\\d+)\\s+BUSHELS", re.I)
RE_PRICE = re.compile(r"LAND IS TRADING AT\\s+(\\d+)\\s+BUSHELS", re.I)


def parse_state(text: str) -> Optional[State]:
    # Use the most recent full report present in the transcript.
    year_m = list(RE_YEAR.finditer(text))
    pop_m = list(RE_POP.finditer(text))
    acres_m = list(RE_ACRES.finditer(text))
    grain_m = list(RE_GRAIN.finditer(text))
    price_m = list(RE_PRICE.finditer(text))
    if not (year_m and pop_m and acres_m and grain_m and price_m):
        return None
    year = int(year_m[-1].group(1))
    pop = int(pop_m[-1].group(1))
    acres = int(acres_m[-1].group(1))
    grain = int(grain_m[-1].group(1))
    price = int(price_m[-1].group(1))
    return State(year=year, pop=pop, acres=acres, grain=grain, price=price)


def clamp(n: int, lo: int, hi: int) -> int:
    return lo if n < lo else hi if n > hi else n


def decide(st: State) -> Tuple[int, int, int, int]:
    """
    Retourne (buy, sell, feed, plant).

    Hypothèses (vérifiées sur cette version) :
      - Nourrir : 20 bushels/personne pour 0 famine.
      - Planter : 1 personne peut s'occuper de 10 acres.
      - Semences : 0.5 bushel par acre (donc acres_plant <= grain_after_feed * 2).
    """
    pop = st.pop
    acres = st.acres
    grain = st.grain
    price = st.price

    # Reserve de sécurité (rats + variations récolte)
    reserve = 300

    # 1) Cible foncière : ~10 acres/personne, mais ne pas acheter si on est short en grain.
    target_acres = pop * 10

    # Grain requis pour 0 famine.
    feed = pop * 20

    # On prévoit de planter au moins 70% de la capacité (si possible) pour stabiliser le grain.
    plant_cap = min(acres, pop * 10)

    # En pratique, on veut garder assez de grain pour nourrir + semer (0.5/acre) + réserve.
    # Si on plante X acres, coût semences = ceil(X/2). (On arrondit au-dessus par prudence.)
    def seed_cost(x: int) -> int:
        return (x + 1) // 2

    # Plan de base : vendre si besoin pour atteindre feed+reserve.
    buy = 0
    sell = 0

    # Si grain insuffisant pour nourrir, vendre des acres d'abord.
    if grain < feed + reserve:
        deficit = (feed + reserve) - grain
        sell = (deficit + price - 1) // price
        sell = clamp(sell, 0, acres)
        acres2 = acres - sell
        grain2 = grain + sell * price
    else:
        acres2 = acres
        grain2 = grain

    # Acheter seulement si on a du surplus après feed + une plantation raisonnable + réserve.
    # On essaie de monter vers target_acres sans mettre en danger le grain.
    desired = max(0, target_acres - acres2)

    # Choisir une plantation prévue (sera recalculée après transactions).
    planned_plant = int(0.8 * min(acres2, pop * 10))

    surplus = grain2 - feed - seed_cost(planned_plant) - reserve
    if surplus > price * 5 and desired > 0:
        max_buy = surplus // price
        buy = clamp(min(max_buy, desired), 0, 99999)
        grain2 -= buy * price
        acres2 += buy

    # Si on est encore largement au-dessus de la cible foncière, vendre l'excédent.
    if buy == 0:
        excess = acres2 - target_acres
        if excess > pop * 2:
            sell2 = excess
            sell2 = clamp(sell2, 0, acres2)
            sell += sell2
            acres2 -= sell2
            grain2 += sell2 * price

    # 3) Nourrir : viser 0 famine, sinon au mieux.
    feed = min(feed, grain2)
    grain_after_feed = grain2 - feed

    # 4) Planter : max acres, max pop*10, max grain*2 (seed 0.5/bushel),
    # en gardant une petite réserve pour l'année suivante.
    plant_max = min(acres2, pop * 10, (max(0, grain_after_feed - 100) * 2))
    plant = clamp(plant_max, 0, acres2)

    return buy, sell, feed, plant


def main() -> int:
    try:
        sock = socket.create_connection((HOST, PORT), timeout=6)
    except OSError as e:
        print(f"Erreur: impossible de se connecter à {HOST}:{PORT} ({e})", file=sys.stderr)
        return 2

    transcript = ""
    try:
        recv_avail(sock, total=0.6)
        sock.sendall(bytes([CTRL_R]))
        time.sleep(0.9)
        transcript += recv_avail(sock, total=1.6)
        print("[hamurabi] reset done", flush=True)

        def wait_for_any(needles: Tuple[str, ...], timeout_s: float = 10.0) -> bool:
            nonlocal transcript
            end = time.time() + timeout_s
            while time.time() < end:
                chunk = recv_avail(sock, total=1.5)
                if chunk:
                    transcript += chunk
                    up = transcript[-2000:].upper()
                    if any(n in up for n in needles):
                        return True
                else:
                    time.sleep(0.1)
            return False

        # Ensure we're in BASIC (prompt '>'). If reset landed in Woz, run E000R.
        send_line(sock, "", wait=0.2, read_t=1.2)  # nudge prompt
        if not wait_for_any((">",), timeout_s=2.5):
            print("[hamurabi] not in BASIC, trying E000R", flush=True)
            transcript += send_line(sock, "E000R", wait=0.9, read_t=5.0)
            wait_for_any((">",), timeout_s=3.5)
        print("[hamurabi] BASIC prompt ready", flush=True)

        # Start the program (BASIC must already have it loaded).
        transcript += send_line(sock, "RUN", wait=0.9, read_t=8.0)
        print("[hamurabi] RUN sent, waiting BUY prompt", flush=True)
        # Wait for the first BUY question so we don't race the output.
        wait_for_any(("HOW MANY ACRES DO YOU WISH TO BUY",), timeout_s=12.0)
        print("[hamurabi] first BUY prompt seen", flush=True)

        cur_state: Optional[State] = None
        cur_plan: Optional[Tuple[int, int, int, int]] = None

        # Main interaction loop: answer prompts until game ends or we timeout.
        stage = 0  # 0=buy,1=sell,2=feed,3=plant

        idle_s = 0.0
        for _ in range(1200):
            t0 = time.time()
            chunk = recv_avail(sock, total=2.5)
            dt = time.time() - t0
            if not chunk:
                idle_s += dt
            else:
                idle_s = 0.0
                transcript += chunk
                # periodic tiny heartbeat when receiving output
                if "HOW MANY" in transcript[-700:].upper():
                    print("[hamurabi] prompt detected", flush=True)

            # End conditions (varies by version; keep broad)
            if "CONGRAT" in transcript.upper() or "A FANTASTIC" in transcript.upper():
                print(transcript[-2500:])
                return 0
            if "SO LONG FOR NOW" in transcript.upper() or "GAME OVER" in transcript.upper():
                print(transcript[-2500:])
                return 0
            if "YOU STARVED" in transcript.upper() or "RIOT" in transcript.upper():
                print(transcript[-2500:])
                return 1

            # Only (re)plan when we are at the BUY prompt of a new year; that's the first
            # question asked after the yearly report.

            tail = transcript[-700:].upper()
            # Detect which question is being asked.
            asked = None
            # Some outputs place the cursor right after the '?' without newline; match broadly.
            tail2 = transcript[-2500:].upper()
            if "HOW MANY ACRES DO YOU WISH TO BUY" in tail2:
                asked = 0
            elif "HOW MANY ACRES DO YOU WISH TO SELL" in tail2:
                asked = 1
            elif "HOW MANY BUSHELS DO YOU WISH TO FEED" in tail2:
                asked = 2
            elif "HOW MANY ACRES DO YOU WISH TO PLANT" in tail2:
                asked = 3
            elif "***" in tail and "ERR" in tail:
                # If BASIC error, bail with context.
                print(transcript[-2500:])
                return 3

            if asked is None:
                if idle_s > 25.0:
                    print(transcript[-2500:])
                    print(
                        "\n[ERREUR] Bloqué: je ne vois pas de question Hamurabi.",
                        file=sys.stderr,
                    )
                    return 4
                continue

            if asked == 0:
                # New year planning point: parse the report immediately above the BUY question.
                st2 = parse_state(transcript)
                if st2 is None:
                    # Not enough output yet; wait for more report lines.
                    time.sleep(0.2)
                    continue
                if cur_state is None or st2.year != cur_state.year:
                    cur_state = st2
                    cur_plan = decide(st2)
                    stage = 0
                    print(
                        f"[hamurabi] year {cur_state.year} parsed: pop={cur_state.pop} acres={cur_state.acres} grain={cur_state.grain} price={cur_state.price}",
                        flush=True,
                    )

            if cur_plan is None:
                # Still can't plan: wait.
                time.sleep(0.2)
                continue

            buy, sell, feed, plant = cur_plan
            ans = [buy, sell, feed, plant][asked]
            qname = ["BUY", "SELL", "FEED", "PLANT"][asked]
            print(f"[hamurabi] {qname} -> {ans}", flush=True)
            transcript += send_line(sock, str(ans), wait=0.6, read_t=7.0)

        print(transcript[-2500:])
        return 1
    finally:
        try:
            sock.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())

