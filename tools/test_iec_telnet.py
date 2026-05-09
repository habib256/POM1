#!/usr/bin/env python3
"""
test_iec_telnet.py -- End-to-end smoke for the P-LAB IEC daughterboard.

Boots POM1 with --preset 5 (microSD + Applesoft Lite) and --enable iec
and exercises the @-prefixed IEC commands of SD CARD OS 1.3 against the
bundled disks/iec/dev8.d64 fixture (label "POM1 IEC", id "01", with a
single PRG file "HELLO" of 256 bytes).

Tests:
  1. @DEV          -> "DEVICE:" + "8"      (default device)
  2. @DEV 9 ; @DEV -> "DEVICE: 9"          (set/get round-trip)
  3. @DEV 8        -> back to default
  4. @$            -> contains "POM1" + "HELLO" + "BLOCKS FREE"
  5. @ERR          -> "00, OK,00,00" or "73, CBM DOS V2.6 1541" (power-on)

This is the live verification that the IECCard byte-frame FSM can talk
to the firmware without timing out (?DEVICE NOT PRESENT). If 4. fails
but 1./2./3. pass, the FSM is plugged in but the directory transmission
path is broken. If 1. fails (no @DEV echo), the firmware can't even
reach the drive — most likely VIA pin polarity is wrong.

Pre-requisites:
  - build/POM1 built (desktop, not WASM).
  - disks/iec/dev8.d64 present (run `make make_iec_fixture` then
    `./tools/make_iec_fixture disks/iec/dev8.d64` from build/).
  - Nothing else listening on 127.0.0.1:6502.

Run from repo root: python3 tools/test_iec_telnet.py [-v]
"""
from __future__ import annotations

import select
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18
VERBOSE = "--verbose" in sys.argv or "-v" in sys.argv

REPO_ROOT = Path(__file__).resolve().parent.parent
DEV8_D64 = REPO_ROOT / "disks" / "iec" / "dev8.d64"


def vprint(label: str, data: str) -> None:
    if VERBOSE:
        print(f"    [{label}] {data[-500:]!r}")


def recv_avail(sock: socket.socket, total: float = 4.0, idle: float = 0.4) -> str:
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


def send_line(sock: socket.socket, cmd: str, wait: float = 0.3, read_t: float = 4.0) -> str:
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t, idle=0.4)


def send_ctrl(sock: socket.socket, byte: int) -> None:
    sock.sendall(bytes([byte & 0xFF]))


passed = 0
failed = 0


def check(name: str, output: str, expected: str) -> bool:
    global passed, failed
    vprint(name, output)
    if expected.upper() in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    failed += 1
    print(f"  [FAIL] {name} -- expected '{expected}'")
    print(f"         got: {output[-300:]!r}")
    return False


def check_not(name: str, output: str, unexpected: str) -> bool:
    global passed, failed
    vprint(name, output)
    if unexpected.upper() not in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    failed += 1
    print(f"  [FAIL] {name} -- unexpected '{unexpected}'")
    print(f"         got: {output[-300:]!r}")
    return False


def ensure_pom1_binary() -> Path:
    p = REPO_ROOT / "build" / "POM1"
    if not p.is_file():
        sys.exit(f"ERROR: {p} not found - build POM1 first (cd build && make)")
    return p


def ensure_fixture() -> None:
    if not DEV8_D64.is_file():
        sys.exit(f"ERROR: {DEV8_D64} not found. From build/, run "
                 f"`make make_iec_fixture && ./tools/make_iec_fixture {DEV8_D64}`.")


def launch_pom1(log_path: str):
    exe = ensure_pom1_binary()
    log = open(log_path, "w")
    proc = subprocess.Popen(
        [str(exe), "--preset", "5", "--enable", "iec", "--terminal", "--cpu-max"],
        stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
        cwd=str(REPO_ROOT),
    )
    time.sleep(3.0)
    if proc.poll() is not None:
        sys.exit(f"ERROR: POM1 exited early (code {proc.returncode}); see {log_path}")
    return proc, log


def teardown_pom1(proc, log) -> None:
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
    log.close()


def main() -> int:
    print("=" * 60)
    print("P-LAB IEC daughterboard -- @-commands smoke")
    print("=" * 60)

    ensure_fixture()

    log_path = "/tmp/pom1_iec_test.log"
    proc, log = launch_pom1(log_path)

    try:
        try:
            sock = socket.create_connection((HOST, PORT), timeout=5)
        except (ConnectionRefusedError, OSError) as e:
            sys.exit(f"ERROR: cannot connect to {HOST}:{PORT}: {e}")
        sock.settimeout(10)

        recv_avail(sock, total=2.0)  # drain banner
        send_ctrl(sock, CTRL_R)       # soft reset -> Wozmon
        time.sleep(0.9)
        recv_avail(sock, total=1.5)

        print("\nStep 1: launch SD CARD OS (8000R)")
        sock.sendall(b"8000R\r")
        deadline = time.time() + 5.0
        out = ""
        while time.time() < deadline and "/>" not in out:
            out += recv_avail(sock, total=0.5, idle=0.2)
        vprint("1.1 banner", out)
        check("1.1 SD CARD OS prompt visible", out, "/>")

        print("\nStep 2: @DEV (read default device)")
        out = send_line(sock, "@DEV", wait=0.6, read_t=4.0)
        check("2.1 @DEV emits 'DEVICE'", out, "DEVICE")
        check("2.2 @DEV default = 8",       out, "8")

        print("\nStep 3: @DEV 9 ; @DEV (set + read back)")
        out = send_line(sock, "@DEV 9", wait=0.5, read_t=3.0)
        out = send_line(sock, "@DEV",   wait=0.5, read_t=3.0)
        check("3.1 @DEV after set -> 9", out, "DEVICE: 9")

        print("\nStep 4: @DEV 8 (back to default)")
        out = send_line(sock, "@DEV 8", wait=0.5, read_t=3.0)
        out = send_line(sock, "@DEV",   wait=0.5, read_t=3.0)
        check("4.1 @DEV after reset -> 8", out, "DEVICE: 8")

        print("\nStep 5: @$ directory listing (live IEC bus traffic)")
        out = send_line(sock, "@$", wait=2.0, read_t=8.0)
        check_not("5.1 @$ does not time out (?DEVICE NOT PRESENT)", out, "DEVICE NOT PRESENT")
        check    ("5.2 @$ shows disk label POM1",       out, "POM1")
        check    ("5.3 @$ shows HELLO file entry",      out, "HELLO")
        check    ("5.4 @$ shows BLOCKS FREE trailer",   out, "BLOCKS FREE")

        print("\nStep 6: @ERR (read error channel)")
        out = send_line(sock, "@ERR", wait=1.5, read_t=5.0)
        check_not("6.1 @ERR does not time out", out, "DEVICE NOT PRESENT")
        # 73 = power-on banner ("CBM DOS V2.6 1541"); 00 = OK after a successful op.
        check    ("6.2 @ERR shows a status code", out, ",")

        sock.close()
    finally:
        teardown_pom1(proc, log)

    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)
    if failed > 0:
        print(f"\nDiagnostics: see {log_path}")
        print("Possible causes if all @ commands fail:")
        print("  - VIA pin polarity / map mismatch in IECCard.h "
              "(kAtnOutBit/kClkOutBit/kDataOutBit/kClkInBit/kDataInBit)")
        print("  - Frame timing too coarse — kTxBitSettleCycles / kTxByteAckCycles "
              "in IECCard.cpp may need tuning")
        print("  - VIA T2 not ticking — check MicroSD.cpp::advanceCycles t2Running")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
