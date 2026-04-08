#!/usr/bin/env python3
"""
play_lunar_lander_ascii_basic_telnet.py — Pilot Lunar Lander (ASCII graphics, BASIC) via Terminal Card.

Pré-requis (comme pour les autres scripts Telnet) :
  - Dans POM1 : activer Hardware > P-LAB Terminal Card
  - Charger le programme : File > Load Memory > software/basic/lunar-lander-ascii-graphics.apl.txt
    (ce fichier est un dump mémoire de programme BASIC)

Ce script :
  - se connecte à localhost:6502
  - reset l’Apple-1 (CTRL-R via Terminal Card)
  - démarre BASIC (E000R)
  - lance RUN
  - répond automatiquement au prompt "BURN" en régulant la vitesse de descente.

Remarque :
  - Le modèle exact de la physique est dans le programme BASIC ; on utilise ici un contrôleur
    heuristique basé sur les valeurs imprimées (HEIGHT, VELOCITY, FUEL).
"""

from __future__ import annotations

import re
import select
import socket
import sys
import time
from dataclasses import dataclass
import heapq
from typing import Dict, List, Optional, Tuple, Union

HOST = "127.0.0.1"
PORT = 6502

CTRL_R = 18  # Terminal Card reset

MAX_BURN = 30
MIN_BURN = 0
SAFE_LANDING_SPEED = 0  # this variant seems to require a full stop (0 ft/s)


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


def send_raw(sock: socket.socket, s: str) -> None:
    sock.sendall(s.encode("ascii"))


def send_line(sock: socket.socket, line: str, wait: float = 0.45, read_t: float = 2.5) -> str:
    send_raw(sock, line + "\r")
    time.sleep(wait)
    return recv_avail(sock, total=read_t)


@dataclass(frozen=True)
class Telemetry:
    t: int
    height: int
    velocity: int
    fuel: int


_ROW_RE = re.compile(
    r"(?m)^\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*$"
)


def parse_latest_row(text: str) -> Optional[Telemetry]:
    """
    Extrait la dernière ligne de télémétrie imprimée comme :
        TIME HEIGHT VELOCITY FUEL
    """
    matches = list(_ROW_RE.finditer(text.replace("\r\n", "\n").replace("\r", "\n")))
    if not matches:
        return None
    m = matches[-1]
    t, h, v, f = (int(m.group(i)) for i in range(1, 5))
    return Telemetry(t=t, height=h, velocity=v, fuel=f)


def clamp(v: int, lo: int, hi: int) -> int:
    return lo if v < lo else hi if v > hi else v


def step_model(height: int, speed_down: int, fuel: int, burn: int) -> Tuple[int, int, int]:
    """
    Modèle exact (déduit des transitions observées dans cette version) :
      a = 5 - burn
      speed' = speed + a
      height' = height - speed - (a // 2)
      fuel' = fuel - burn

    Ici, speed_down est "downward positive" (comme affiché par le jeu).
    """
    burn = clamp(burn, 0, min(MAX_BURN, fuel))
    a = 5 - burn
    speed2 = speed_down + a
    height2 = height - speed_down - (a // 2)
    fuel2 = fuel - burn
    return height2, speed2, fuel2


def touchdown_velocity(height: int, speed_down: int, burn: int) -> Optional[float]:
    """
    If touchdown occurs within the next 1 second under constant acceleration a=(5-burn),
    return the impact velocity (downward positive). Otherwise return None.

    Equation (downward positive):
        height(t) = height - speed*t - 0.5*a*t^2
    Solve height(t)=0 for t in (0, 1].
    """
    if height <= 0:
        return float(speed_down)
    a = 5 - burn
    # Solve: 0.5*a*t^2 + speed*t - height = 0
    A = 0.5 * float(a)
    B = float(speed_down)
    C = -float(height)

    if abs(A) < 1e-12:
        # Linear: speed*t = height
        if B <= 0:
            return None
        t = height / B
        if 0.0 < t <= 1.0:
            return B  # v stays constant
        return None

    disc = B * B - 4.0 * A * C
    if disc < 0:
        return None
    sqrt_disc = disc ** 0.5
    # We want the positive root.
    t1 = (-B + sqrt_disc) / (2.0 * A)
    t2 = (-B - sqrt_disc) / (2.0 * A)
    t_candidates = [t for t in (t1, t2) if t > 1e-9]
    if not t_candidates:
        return None
    t = min(t_candidates)
    if t <= 1.0 + 1e-9:
        return B + float(a) * t
    return None


def simulate_action(
    height: int, speed_down: int, fuel: int, burn: int
) -> Union[Tuple[str, float, int], Tuple[str, Tuple[int, int, int]]]:
    """
    Returns either:
      ("terminal", impact_velocity, fuel_after)
      ("state", (h2, v2, f2))
    """
    burn = clamp(burn, 0, min(MAX_BURN, fuel))
    v_impact = touchdown_velocity(height, speed_down, burn)
    if v_impact is not None:
        # This variant seems to subtract the full burn even if touchdown happens mid-step.
        return ("terminal", float(v_impact), fuel - burn)
    return ("state", step_model(height, speed_down, fuel, burn))


def plan_burns(height: int, speed_down: int, fuel: int) -> List[int]:
    """
    Cherche une séquence de burns menant à un atterrissage sûr (speed <= SAFE_LANDING_SPEED)
    en minimisant principalement la consommation de fuel, puis le nombre d'étapes.
    """
    start = (height, speed_down, fuel)

    def is_safe_impact(v_impact: float) -> bool:
        return v_impact <= float(SAFE_LANDING_SPEED) + 1e-9

    # Priority: (fuel_used, steps, h, v, f)
    # We'll also use a light heuristic: if v is huge and fuel is low, deprioritize.
    pq: List[Tuple[int, int, int, int, int]] = []
    heapq.heappush(pq, (0, 0, height, speed_down, fuel))

    parent: Dict[Tuple[int, int, int], Tuple[Tuple[int, int, int], int]] = {}
    best: Dict[Tuple[int, int, int], Tuple[int, int]] = {start: (0, 0)}

    expansions = 0
    while pq:
        fuel_used, steps, h, v, f = heapq.heappop(pq)
        state = (h, v, f)
        b0 = best.get(state)
        if b0 is None or (fuel_used, steps) != b0:
            continue

        expansions += 1
        if expansions > 1_200_000:
            break

        if f <= 0:
            continue

        # Action set:
        # Near the ground, we need fine control (some landings require burn ~7 etc).
        if h <= 30:
            candidates = set(range(0, min(30, f) + 1))
        else:
            # Otherwise, keep branching low.
            candidates = {0, 5, min(30, f)}

        # Rough needed decel to reach SAFE_LANDING_SPEED at ground using max a=25
        # h_need ~= (v^2 - v_f^2)/50. If h is small, consider stronger burns.
        v_f = SAFE_LANDING_SPEED
        h_need = (v * v - v_f * v_f) / 50.0 if v > v_f else 0.0
        if h <= h_need + 120:
            candidates.update({min(30, f), min(25, f), min(20, f), min(15, f), min(10, f)})
        elif h <= h_need + 250:
            candidates.update({min(20, f), min(15, f), min(10, f)})
        else:
            candidates.update({0, 1, 2, 3, 4, 5})

        for burn in sorted(candidates):
            sim = simulate_action(h, v, f, burn)
            if sim[0] == "terminal":
                _, v_impact, f_after = sim
                if not is_safe_impact(v_impact):
                    continue
                # Found a 1-step-to-win action: reconstruct and append burn.
                burns: List[int] = []
                cur = state
                while cur != start:
                    prev, bprev = parent[cur]
                    burns.append(bprev)
                    cur = prev
                burns.reverse()
                burns.append(burn)
                return burns

            _, (h2, v2, f2) = sim

            # Prune nonsense
            if v2 < -200 or v2 > 400:
                continue
            if h2 < -5000:
                continue

            s2 = (h2, v2, f2)
            fu2 = fuel_used + burn
            st2 = steps + 1
            prev_best = best.get(s2)
            if prev_best is None or (fu2, st2) < prev_best:
                best[s2] = (fu2, st2)
                parent[s2] = (state, burn)
                heapq.heappush(pq, (fu2, st2, h2, v2, f2))

    return []


def last_prompt_is_question_mark(text: str) -> bool:
    tail = text[-1200:].replace("\r\n", "\n").replace("\r", "\n")
    # Many Lunar Lander BASIC variants prompt with a bare "?" on its own line.
    return re.search(r"(?m)^\?\s*$", tail) is not None


def main() -> int:
    try:
        sock = socket.create_connection((HOST, PORT), timeout=6)
    except OSError as e:
        print(f"Erreur: impossible de se connecter à {HOST}:{PORT} ({e})", file=sys.stderr)
        return 2

    transcript = ""
    try:
        recv_avail(sock, total=0.6)

        # Reset to Woz
        sock.sendall(bytes([CTRL_R]))
        time.sleep(0.9)
        transcript += recv_avail(sock, total=1.6)

        # Start BASIC (ROM).
        # Depending on configuration, reset may already land in BASIC. We try to detect that,
        # otherwise we run E000R from Woz.
        transcript += send_line(sock, "", wait=0.2, read_t=1.2)  # nudge prompt
        for _ in range(3):
            # If already in BASIC, the prompt is typically ">".
            if "\n>" in transcript or transcript.strip().endswith(">"):
                break
            # If we're in Woz, prompt is "\" (often followed by @ cursor).
            transcript += send_line(sock, "E000R", wait=0.9, read_t=5.0)
            if "\n>" in transcript or transcript.strip().endswith(">"):
                break

        # Launch the preloaded BASIC program
        transcript += send_line(sock, "RUN", wait=0.8, read_t=6.0)

        # Quick sanity check: if the program isn't loaded, BASIC will usually complain and/or
        # we won't see any of the game's header strings.
        if not any(
            s in transcript
            for s in (
                "LUNAR LANDER",
                "LUNAR LANDING SIMULATION",
                "CONTROL TO LUNAR MODULE",
                "INSTRUCTIONS",
                "BURN",
            )
        ):
            # Give it one more chance in case output is delayed.
            time.sleep(0.8)
            transcript += recv_avail(sock, total=4.5)
        if not any(
            s in transcript
            for s in (
                "LUNAR LANDER",
                "LUNAR LANDING SIMULATION",
                "CONTROL TO LUNAR MODULE",
                "INSTRUCTIONS",
                "BURN",
            )
        ):
            print(transcript[-2200:])
            print(
                "\n[ERREUR] Je ne vois pas l'écran de Lunar Lander. "
                "Assure-toi d'avoir chargé `software/basic/lunar-lander-ascii-graphics.apl.txt` "
                "dans POM1 (File > Load Memory) avant de lancer ce script.",
                file=sys.stderr,
            )
            return 3

        # Many BASIC versions prompt "INSTRUCTIONS (Y OR N)?", expecting a single key.
        if "INSTRUCTIONS" in transcript:
            send_raw(sock, "N")  # single key
            # Some variants continue after CR; harmless if ignored.
            send_raw(sock, "\r")
            time.sleep(0.6)
            transcript += recv_avail(sock, total=10.0)

        # Main loop: wait for input prompt and feed burn numbers.
        # We keep appending outputs to parse latest telemetry row.
        last_tm: Optional[Telemetry] = None
        down_positive: Optional[bool] = None  # sign convention
        cached_plan: List[int] = []
        cached_for: Optional[Tuple[int, int, int]] = None
        idle_s = 0.0
        for _step in range(900):  # hard stop
            t0 = time.time()
            chunk = recv_avail(sock, total=2.5)
            dt = time.time() - t0
            if not chunk:
                idle_s += dt
            else:
                idle_s = 0.0
            if chunk:
                transcript += chunk

            # Check terminal messages
            if "PERFECT LANDING" in transcript or "CONGRATULATIONS" in transcript:
                print(transcript[-2200:])
                return 0
            if "YOU JUST CREAMED" in transcript or "CRASH" in transcript or "CREAMED" in transcript:
                print(transcript[-2200:])
                return 1

            # If program asks Try again, stop (user can rerun script).
            if "TRY AGAIN" in transcript:
                print(transcript[-2200:])
                return 1
            if "ANOTHER MISSION" in transcript:
                print(transcript[-3000:])
                return 1

            # Input prompt can be either "BURN ?" text or a bare "?" line depending on variant.
            prompt_ready = ("BURN" in transcript[-1200:]) or last_prompt_is_question_mark(transcript)
            if not prompt_ready:
                if idle_s > 25.0:
                    print(transcript[-4000:])
                    print(
                        "\n[ERREUR] Aucun prompt d'entrée reçu (connexion idle). "
                        "Vérifie que le jeu tourne bien (RUN) et que la Terminal Card est activée.",
                        file=sys.stderr,
                    )
                    return 4
                continue

            tm = parse_latest_row(transcript)
            if tm is None:
                # Sometimes prompt arrives before first row; poke a 0 burn.
                transcript += send_line(sock, "0", wait=0.35, read_t=2.0)
                continue

            if down_positive is None:
                # In this Apple-1 BASIC Lunar Lander variant, SPEED is typically printed as
                # a positive number for downward motion (despite some instruction texts).
                # We lock to that convention as soon as we see the first telemetry row.
                down_positive = tm.velocity >= 0
                if not down_positive:
                    # This solver expects downward-positive speed; bail with a clear error.
                    print(transcript[-3000:])
                    print(
                        "\n[ERREUR] Cette variante semble afficher SPEED négatif pour la descente. "
                        "Le solveur actuel suppose SPEED positif vers le bas.",
                        file=sys.stderr,
                    )
                    return 5

            # Avoid spamming the same decision if output repeats.
            if last_tm == tm:
                time.sleep(0.15)
                continue
            last_tm = tm

            state = (tm.height, tm.velocity, tm.fuel)
            if cached_for != state or not cached_plan:
                cached_plan = plan_burns(tm.height, tm.velocity, tm.fuel)
                cached_for = state

            burn = cached_plan.pop(0) if cached_plan else 0
            transcript += send_line(sock, str(burn), wait=0.35, read_t=3.0)

            # Handle "BURN OUT OF RANGE" by retrying with clamp.
            if "BURN OUT OF RANGE" in transcript[-500:]:
                burn = clamp(burn, 0, 30)
                transcript += send_line(sock, str(burn), wait=0.35, read_t=2.0)

        print(transcript[-2200:])
        return 1
    finally:
        try:
            sock.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())

