#!/usr/bin/env python3
"""
test_game_smoke.py -- headless boot-smoke for a text-mode Apple-1 program.

Launches POM1 with ``--headless --terminal``, loads the program at its origin
and runs it, connects to the Terminal Card TCP server (127.0.0.1:6502), reads
the boot output (telnet IAC stripped, Apple-1 high-bit masked) and asserts that
an expected marker string appears. This is the ``make test`` backend for
text-mode projects -- see dev/cc65/Makefile.common: a project opts in by
setting TEST_CMD to invoke this script.

Example (in a project Makefile, BEFORE ``include ../../cc65/Makefile.common``):

    TEST_CMD := python3 ../../../tools/test_game_smoke.py --pom1 ../../../build/POM1 \\
        --bin "$(OUT_DIR)/$(PROJECT).bin" --addr 0x0280 --expect "CONNECT 4"

Exit codes:
    0  marker seen -- OR -- POM1 binary not found (SKIP, so ``make test`` stays
       green on a box where POM1 was not built; CI builds build/POM1 first).
    1  POM1 launched but the marker never appeared (a real regression), or the
       program binary is missing / a setup error occurred.

POM1 location: ``--pom1``, else $POM1, else build/POM1 searched upward from CWD
(so it resolves when run from inside a dev/projects/<name>/ directory).

Protocol notes: the Terminal Card port is fixed at 6502 (see ``--terminal`` in
doc/CLI.md); tests run sequentially under ``make`` so they do not contend for
it. Apple-1 output has bit 7 set, hence the & 0x7F mask. IAC handling mirrors
tools/test_sdcard_os_telnet.py.
"""
from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import time

HOST = "127.0.0.1"
TERMINAL_PORT = 6502  # Terminal Card TCP server (fixed; doc/CLI.md --terminal)


def find_pom1(explicit):
    """Resolve the POM1 binary: --pom1, else $POM1, else build/POM1 searched
    upward from the current directory. Returns a path or None."""
    for c in (explicit, os.environ.get("POM1"), "build/POM1"):
        if c and os.path.exists(c):
            return c
    d = os.path.abspath(".")
    while True:
        p = os.path.join(d, "build", "POM1")
        if os.path.exists(p):
            return p
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def strip_iac(buf):
    """Remove telnet IAC sequences from `buf` (a bytearray). Returns
    (clean_bytes, leftover_bytearray); leftover holds a partial IAC sequence
    straddling a recv() boundary so the caller can prepend it next time."""
    out = bytearray()
    i, n = 0, len(buf)
    while i < n:
        b = buf[i]
        if b == 0xFF:  # IAC
            if i + 1 >= n:
                return bytes(out), buf[i:]          # need more bytes
            cmd = buf[i + 1]
            if cmd == 0xFF:                          # escaped literal 0xFF
                out.append(0xFF); i += 2; continue
            if cmd in (0xFB, 0xFC, 0xFD, 0xFE):      # WILL/WONT/DO/DONT + option
                if i + 2 >= n:
                    return bytes(out), buf[i:]
                i += 3; continue
            i += 2; continue                         # other 2-byte command
        out.append(b); i += 1
    return bytes(out), bytearray()


def read_for_marker(sock, marker, timeout):
    """Read from `sock` up to `timeout` s, accumulating decoded terminal text;
    return (found, text)."""
    sock.settimeout(0.4)
    deadline = time.monotonic() + timeout
    leftover = bytearray()
    chars = []
    needle = marker.upper()
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        if not chunk:
            break
        leftover += chunk
        clean, leftover = strip_iac(leftover)
        for b in clean:
            c = b & 0x7F                              # Apple-1 sets bit 7 on output
            if c >= 0x20 or c in (0x0A, 0x0D):
                chars.append(chr(c))
        if needle in "".join(chars).upper():
            return True, "".join(chars)
    return needle in "".join(chars).upper(), "".join(chars)


def main():
    ap = argparse.ArgumentParser(
        description="headless boot-smoke a text-mode Apple-1 program via the Terminal Card")
    ap.add_argument("--bin", required=True, help="raw binary to load (the project's .bin)")
    ap.add_argument("--addr", default="0x0280", help="load/run origin (default 0x0280)")
    ap.add_argument("--expect", required=True, help="marker that must appear in the boot output")
    ap.add_argument("--keys", default="", help=r"optional keystrokes to send after connect (\r=CR)")
    ap.add_argument("--preset", default=None, help="optional --preset N")
    ap.add_argument("--timeout", type=float, default=8.0, help="seconds to wait for the marker")
    ap.add_argument("--pom1", default=None, help="path to POM1 (else $POM1, else build/POM1)")
    args = ap.parse_args()

    pom1 = find_pom1(args.pom1)
    if not pom1:
        print(f"SKIP: POM1 not built (no build/POM1) -- not smoke-testing {os.path.basename(args.bin)}")
        return 0
    if not os.path.exists(args.bin):
        print(f"FAIL: program binary not found: {args.bin!r} (did `make all` run?)")
        return 1

    addr = int(args.addr, 0)
    cmd = [pom1, "--headless", "--terminal",
           "--load", f"{addr:04X}:{args.bin}",
           "--run", f"{addr:04X}"]
    if args.preset is not None:
        cmd += ["--preset", str(args.preset)]

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        sock = None
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                print(f"FAIL: POM1 exited early (code {proc.returncode}) before :{TERMINAL_PORT} opened")
                return 1
            try:
                sock = socket.create_connection((HOST, TERMINAL_PORT), 1.0)
                break
            except OSError:
                time.sleep(0.2)
        if sock is None:
            print(f"FAIL: Terminal Card port {TERMINAL_PORT} never opened")
            return 1
        try:
            if args.keys:
                time.sleep(0.5)
                keys = args.keys.replace("\\r", "\r").replace("\\n", "\n")
                sock.sendall(keys.encode("latin-1"))
            ok, text = read_for_marker(sock, args.expect, args.timeout)
        finally:
            sock.close()
        if ok:
            print(f"PASS: saw {args.expect!r} in the boot output of {os.path.basename(args.bin)}")
            return 0
        snippet = " ".join(text.split())[:200]
        print(f"FAIL: never saw {args.expect!r} within {args.timeout}s. Got: {snippet!r}")
        return 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
