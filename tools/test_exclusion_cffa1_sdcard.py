#!/usr/bin/env python3
"""
test_exclusion_cffa1_sdcard.py -- Verify mutual exclusion between CFFA1 and P-LAB microSD.

Tests:
  1. Start with P-LAB preset (microSD active) → verify SD CARD OS at $8000, no CFFA1 at $AFDC
  2. Start with CFFA1 preset → verify CFFA1 ID at $AFDC/$AFDD, no SD CARD OS at $8000

The test verifies that only one storage device is active at a time by reading
memory-mapped ROM/ID bytes via the Woz Monitor.
"""
from __future__ import annotations

import select
import socket
import subprocess
import sys
import time
import os
import signal

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18

passed = 0
failed = 0


def recv_avail(sock, total=4.0, idle=0.3):
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


def send_line(sock, cmd, wait=0.3, read_t=5.0):
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t, idle=0.4)


def send_ctrl(sock, byte):
    sock.sendall(bytes([byte & 0xFF]))


def check(name, output, expected):
    global passed, failed
    if expected.upper() in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    else:
        failed += 1
        print(f"  [FAIL] {name} -- expected '{expected}'")
        print(f"         got: {output.strip()!r}")
        return False


def check_not(name, output, unexpected):
    global passed, failed
    if unexpected.upper() not in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    else:
        failed += 1
        print(f"  [FAIL] {name} -- unexpected '{unexpected}' found")
        print(f"         got: {output.strip()!r}")
        return False


def read_byte_via_monitor(sock, addr_hex):
    """Read a single byte via Woz Monitor and return the output."""
    send_ctrl(sock, CTRL_R)
    time.sleep(0.9)
    recv_avail(sock, total=2.0)
    return send_line(sock, addr_hex, wait=0.3, read_t=3.0)


def start_pom1(preset):
    """Start POM1 with given preset, wait for it to be ready."""
    exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "pom1_imgui")
    proc = subprocess.Popen([exe, "--preset", preset],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    # Wait for Terminal Card to start listening
    for _ in range(40):
        time.sleep(0.25)
        try:
            s = socket.create_connection((HOST, PORT), timeout=1)
            s.close()
            return proc
        except (ConnectionRefusedError, OSError):
            continue
    return proc


def stop_pom1(proc):
    """Stop POM1."""
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    time.sleep(0.5)


def main():
    global passed, failed

    print("=" * 55)
    print("Mutual Exclusion Test: CFFA1 vs P-LAB microSD")
    print("=" * 55)

    # === TEST 1: P-LAB preset (microSD active, CFFA1 inactive) ===
    print("\nTest 1: P-LAB Apple 1 preset (microSD ON, CFFA1 OFF)")
    proc = start_pom1("p-lab")

    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
        sock.settimeout(10)
        recv_avail(sock, total=1.5)

        # Read SD CARD OS ROM signature at $8000 (should have data, not $00)
        out = read_byte_via_monitor(sock, "8000")
        print(f"    $8000 = {out.strip()!r}")
        check_not("1.1 SD CARD OS ROM present at $8000", out, "8000: 00")

        # Read CFFA1 ID at $AFDC (should be $00, not $CF — CFFA1 not loaded)
        out = read_byte_via_monitor(sock, "AFDC")
        print(f"    $AFDC = {out.strip()!r}")
        check_not("1.2 No CFFA1 at $AFDC (not CF)", out, "AFDC: CF")

        # Verify SD CARD OS entry works: jump to 8000R and look for SD CARD OS prompt
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)
        out = send_line(sock, "8000R", wait=0.5, read_t=4.0)
        print(f"    8000R = {out[-100:]!r}")
        check("1.3 SD CARD OS responds", out, "SD CARD OS")

        sock.close()
    except Exception as e:
        print(f"  ERROR: {e}")
    finally:
        stop_pom1(proc)

    # === TEST 2: CFFA1 preset (CFFA1 active, microSD inactive) ===
    print("\nTest 2: Replica 1 + CFFA1 preset (CFFA1 ON, microSD OFF)")
    proc = start_pom1("cffa1")

    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
        sock.settimeout(10)
        recv_avail(sock, total=1.5)

        # Read CFFA1 ID at $AFDC (should be $CF)
        out = read_byte_via_monitor(sock, "AFDC")
        print(f"    $AFDC = {out.strip()!r}")
        check("2.1 CFFA1 ID byte $AFDC = CF", out, "AFDC: CF")

        # Read CFFA1 ID at $AFDD (should be $FA)
        out = read_byte_via_monitor(sock, "AFDD")
        print(f"    $AFDD = {out.strip()!r}")
        check("2.2 CFFA1 ID byte $AFDD = FA", out, "AFDD: FA")

        # Read $8000 — should NOT have SD CARD OS (should be $00 or RAM content)
        out = read_byte_via_monitor(sock, "8000")
        print(f"    $8000 = {out.strip()!r}")
        # SD CARD OS starts with $4C (JMP). If $8000 is $00, no SD ROM.
        check_not("2.3 No SD CARD OS at $8000", out, "8000: 4C")

        # Verify CFFA1 menu works
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)
        out = send_line(sock, "9006R", wait=0.5, read_t=4.0)
        print(f"    9006R = {out[-100:]!r}")
        check("2.4 CFFA1 menu responds", out, "CFFA1>")

        # Verify 8000R does NOT enter SD CARD OS
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)
        out = send_line(sock, "8000R", wait=0.5, read_t=3.0)
        print(f"    8000R = {out[-100:]!r}")
        check_not("2.5 8000R does NOT show SD CARD OS", out, "SD CARD OS")

        sock.close()
    except Exception as e:
        print(f"  ERROR: {e}")
    finally:
        stop_pom1(proc)

    # === Results ===
    total = passed + failed
    print(f"\n{'='*55}")
    print(f"Results: {passed}/{total} PASSED")
    if failed:
        print(f"{failed} test(s) FAILED")
        return 1
    print("All tests passed! CFFA1 and microSD are mutually exclusive.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
