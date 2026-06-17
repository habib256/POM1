#!/usr/bin/env python3
"""
test_logo_telnet.py -- Smoke test for the P-LAB LOGO interpreter.

Auto-launches POM1 with preset 6 (P-LAB Apple-1 with TMS9918 + CodeTank),
the Terminal Card on localhost:6502, and the LOGO binary pre-loaded at
$0280 with reset vector pointing at $0280.

Verifies:
  - LOGO banner "P-LAB LOGO V1" appears.
  - "? " prompt after each command.
  - TR / FD / REPEAT do not emit "?" (parser error).
  - BYE returns to Woz Monitor (next prompt is Woz "\\").

POM1 must be built first (cd build && make).

Run from repo root:
  python3 tools/test_logo_telnet.py [--verbose]
"""
from __future__ import annotations

import argparse
import os
import select
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
LOGO_BIN = REPO_ROOT / "build" / "TMS_Logo.bin"

HOST = "127.0.0.1"
PORT = 6502
VERBOSE = False


def ensure_binaries() -> Path:
    pom = REPO_ROOT / "build" / "POM1"
    if not pom.is_file():
        sys.exit(f"ERROR: {pom} not found - build POM1 first (cd build && make)")
    if not LOGO_BIN.is_file():
        sys.exit(
            f"ERROR: {LOGO_BIN} not found - run python3 software/tms9918/emit_TMS_Logo_txt.py first"
        )
    return pom


def launch_pom1(log_path: str):
    exe = ensure_binaries()
    log = open(log_path, "w")
    proc = subprocess.Popen(
        [
            str(exe),
            "--preset", "6",
            "--terminal",
            "--cpu-max",
            "--load", f"0280:{LOGO_BIN}",
        ],
        stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
    )
    time.sleep(3.5)
    if proc.poll() is not None:
        sys.exit(f"ERROR: POM1 exited early (code {proc.returncode}); see {log_path}")
    return proc, log


def teardown_pom1(proc, log) -> None:
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        proc.wait(timeout=5)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass
    log.close()


def recv_avail(sock: socket.socket, total: float = 4.0, idle: float = 0.3) -> str:
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


def drain_to_prompt(sock: socket.socket, prompt: str, timeout: float = 6.0) -> str:
    """Read until the last non-whitespace char is `prompt`, or timeout."""
    end = time.time() + timeout
    buf = ""
    while time.time() < end:
        chunk = recv_avail(sock, total=1.0, idle=0.25)
        if chunk:
            buf += chunk
        if buf.rstrip().endswith(prompt):
            return buf
    return buf


def send(sock: socket.socket, payload: str) -> None:
    if VERBOSE:
        print(f"  >>> {payload!r}")
    sock.sendall(payload.encode("ascii"))


def vprint(label: str, output: str) -> None:
    if VERBOSE:
        print(f"  [{label}] {output[-400:]!r}")


class Results:
    def __init__(self) -> None:
        self.passed = 0
        self.failed: list[tuple[str, str]] = []

    def expect(self, name: str, output: str, needle: str) -> bool:
        if needle.upper() in output.upper():
            self.passed += 1
            print(f"  [PASS] {name}")
            return True
        self.failed.append((name, f"missing '{needle}'"))
        print(f"  [FAIL] {name} -- expected '{needle}'")
        if not VERBOSE:
            print(f"         tail: {output[-300:]!r}")
        return False

    def reject(self, name: str, output: str, needle: str) -> bool:
        if needle not in output:
            self.passed += 1
            print(f"  [PASS] {name}")
            return True
        self.failed.append((name, f"unexpected '{needle}'"))
        print(f"  [FAIL] {name} -- got '{needle}'")
        if not VERBOSE:
            print(f"         tail: {output[-300:]!r}")
        return False

    def report(self) -> int:
        total = self.passed + len(self.failed)
        print()
        print("=" * 50)
        print(f"Results: {self.passed}/{total} PASSED")
        if self.failed:
            for name, why in self.failed:
                print(f"  - {name}: {why}")
            return 1
        print("All tests passed!")
        return 0


def main() -> int:
    global VERBOSE
    parser = argparse.ArgumentParser()
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    VERBOSE = args.verbose

    print("=" * 50)
    print("P-LAB LOGO -- Turtle interpreter telnet smoke test")
    print("=" * 50)

    log_path = "/tmp/pom1_logo_test.log"
    proc, log = launch_pom1(log_path)

    results = Results()
    try:
        try:
            sock = socket.create_connection((HOST, PORT), timeout=5)
        except (ConnectionRefusedError, OSError) as e:
            print(f"ERROR: cannot connect to {HOST}:{PORT}: {e}")
            return 1
        sock.settimeout(10)

        # --- 1: banner appears ---
        time.sleep(1.0)
        out = recv_avail(sock, total=3.0, idle=0.4)
        vprint("boot", out)
        results.expect("1.1 banner LOGO V1", out, "P-LAB LOGO V1")
        results.expect("1.2 first prompt", out, "? ")

        # --- 2: TR 90 (no draw, just rotation) ---
        send(sock, "TR 90\r")
        out = drain_to_prompt(sock, "? ", timeout=4.0)
        vprint("TR 90", out)
        results.reject("2.1 TR 90 no error", out, "?\r")
        results.expect("2.2 prompt back after TR", out, "? ")

        # --- 3: FD 30 (forward) ---
        send(sock, "FD 30\r")
        out = drain_to_prompt(sock, "? ", timeout=4.0)
        vprint("FD 30", out)
        results.reject("3.1 FD 30 no error", out, "?\r")

        # --- 4: REPEAT 4 [FD 50 TR 90] (square) ---
        send(sock, "REPEAT 4 [FD 50 TR 90]\r")
        out = drain_to_prompt(sock, "? ", timeout=8.0)
        vprint("square", out)
        results.reject("4.1 REPEAT square no error", out, "?\r")
        results.expect("4.2 prompt back after square", out, "? ")

        # --- 5: PU + FD 10 + PD (pen flag round-trip) ---
        send(sock, "PU\r")
        out = drain_to_prompt(sock, "? ", timeout=3.0)
        vprint("PU", out)
        results.reject("5.1 PU no error", out, "?\r")
        send(sock, "PD\r")
        out = drain_to_prompt(sock, "? ", timeout=3.0)
        vprint("PD", out)
        results.reject("5.2 PD no error", out, "?\r")

        # --- 6: bogus command -> '?' error ---
        send(sock, "ZZZZ 1\r")
        out = drain_to_prompt(sock, "? ", timeout=3.0)
        vprint("ZZZZ", out)
        results.expect("6.1 unknown -> '?'", out, "?\r")

        # --- 7: BYE -> Woz Monitor '\' prompt ---
        send(sock, "BYE\r")
        out = drain_to_prompt(sock, "\\", timeout=4.0)
        vprint("BYE", out)
        results.expect("7.1 BYE returns to Woz '\\'", out, "\\")

    except Exception as e:
        print(f"ERROR: {e}")
        import traceback
        traceback.print_exc()
    finally:
        try:
            sock.close()
        except Exception:
            pass
        teardown_pom1(proc, log)

    return results.report()


if __name__ == "__main__":
    sys.exit(main())
