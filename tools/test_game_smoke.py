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


def send_keys(sock, text, per_char=0.08):
    """Drip-feed `text` to the Apple-1 keyboard one byte at a time.

    The Apple-1 keyboard is a single-byte latch ($D010): a byte injected
    before the program/monitor has read the previous one is overwritten and
    lost. Sending the whole string at once over loopback overruns the latch
    and mangles the typed address (e.g. `0280R` -> `28R`), so the program
    runs at the wrong place or not at all. An ~80 ms inter-byte gap lets the
    6502 poll KBDCR between keys."""
    for ch in text.encode("latin-1"):
        sock.sendall(bytes([ch]))
        time.sleep(per_char)


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
    # POM1 locates its data dirs (roms/ holding the WOZ Monitor, fonts/, the
    # seed for the macOS app-support symlinks) relative to its launch CWD. Tests
    # run from dev/projects/<name>/, where those dirs are unreachable -- the
    # Terminal Card still answers (card init is CPU-independent) but the Monitor
    # ROM never loads, so typed commands do nothing. Launch from the repo root
    # (two levels above build/POM1) when it actually holds roms/, and pass the
    # binary as an absolute path so --load survives POM1's own chdir.
    pom1 = os.path.abspath(pom1)
    launch_cwd = os.path.dirname(os.path.dirname(pom1))
    if not os.path.isdir(os.path.join(launch_cwd, "roms")):
        launch_cwd = None  # non-standard layout: launch in-place, hope roms/ is found
    bin_abs = os.path.abspath(args.bin)

    # Load the program at startup but do NOT --run it yet: the program prints
    # its title once, and the Terminal Card does not replay scrollback to a
    # client that connects later. Running at startup would emit the title
    # seconds before this script's socket attaches, so the marker would be
    # missed. Instead we boot into WOZ Monitor and run the program *after*
    # connecting, by typing `<addr>R` over the socket (the Terminal Card feeds
    # received bytes to the Apple-1 keyboard) -- see the run command below.
    cmd = [pom1, "--headless", "--terminal",
           "--load", f"{addr:04X}:{bin_abs}"]
    if args.preset is not None:
        cmd += ["--preset", str(args.preset)]

    # A POM1 from an adjacent test may still be tearing down and holding :6502.
    # If we launch now, the new instance can't bind the port (Terminal Card
    # silently disabled) and our socket attaches to the dying instance, which
    # has no program loaded -> only the welcome banner, spurious FAIL. Wait for
    # the port to be free (connection refused) before launching.
    free_deadline = time.monotonic() + 5.0
    while time.monotonic() < free_deadline:
        try:
            probe = socket.create_connection((HOST, TERMINAL_PORT), 0.3)
            probe.close()
            time.sleep(0.2)  # still held by a previous instance
        except OSError:
            break            # refused -> port is free

    proc = subprocess.Popen(cmd, cwd=launch_cwd,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
            # Warm-reset the CPU into WOZ Monitor (Ctrl-R) before running. On a
            # rapid relaunch the boot-time monitor state can race with the macOS
            # data-dir provisioning (refreshed each launch), leaving the CPU not
            # polling the keyboard -- the Terminal Card still answers (welcome)
            # but typed commands do nothing. A warm reset forces a known-good
            # monitor state and does NOT clear RAM, so the loaded program at
            # <addr> survives. Then run it by typing `<addr>R` (send_keys
            # drip-feeds so the single-byte keyboard latch is not overrun).
            time.sleep(0.6)
            sock.sendall(b"\x12")  # Ctrl-R: warm reset into WOZ Monitor
            time.sleep(0.6)
            # Re-issue the run command until the marker shows or we run out of
            # time. On a slow relaunch the monitor may not be polling the
            # keyboard yet when we first type, so the keystrokes are dropped and
            # the program never starts -- only the Terminal Card welcome comes
            # back. Re-running `<addr>R` from the monitor just reprints the
            # title, so retrying is safe; we only retry while the marker is
            # still absent (i.e. the program has not started).
            ok, text = False, ""
            run_cmd = f"{addr:04X}R\r"
            sent_extra = False
            overall_deadline = time.monotonic() + args.timeout
            while not ok and time.monotonic() < overall_deadline:
                send_keys(sock, run_cmd)
                if args.keys and not sent_extra:
                    time.sleep(0.3)
                    send_keys(sock, args.keys.replace("\\r", "\r").replace("\\n", "\n"))
                    sent_extra = True
                window = max(1.0, min(2.5, overall_deadline - time.monotonic()))
                found, chunk = read_for_marker(sock, args.expect, window)
                text += chunk
                ok = found
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
