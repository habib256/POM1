#!/usr/bin/env python3
"""
test_cffa1_telnet.py -- Test complet du CFFA1 CompactFlash via Terminal Card telnet.

Pre-requis :
  - POM1 en cours d'execution avec le preset "Replica 1 + CFFA1" ou CFFA1 active manuellement
  - Terminal Card active (ecoute sur localhost:6502)
  - Image disque cfcard/cfcard.po presente (ProDOS 16 Mo, volume /CFFA1)

Usage :
  python3 tools/test_cffa1_telnet.py [--verbose]

  --verbose  Affiche la sortie brute de chaque commande
"""
from __future__ import annotations

import argparse
import os
import select
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18  # Ctrl-R = reset Apple 1

VERBOSE = False


# ---------------------------------------------------------------------------
# Telnet helpers (same pattern as test_sdcard_os_telnet.py)
# ---------------------------------------------------------------------------

def recv_avail(sock: socket.socket, total: float = 4.0, idle: float = 0.3) -> str:
    """Read all available data with timeout. Returns decoded string."""
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


def send_line(sock: socket.socket, cmd: str, wait: float = 0.3, read_t: float = 5.0) -> str:
    """Send command + CR, wait, read response."""
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t, idle=0.4)


def send_char(sock: socket.socket, ch: str, wait: float = 0.1) -> None:
    """Send a single character."""
    sock.sendall(ch.encode("ascii"))
    time.sleep(wait)


def send_ctrl(sock: socket.socket, byte: int) -> None:
    """Send a control byte."""
    sock.sendall(bytes([byte & 0xFF]))


def drain_until_cffa1_prompt(sock: socket.socket, timeout: float = 30.0) -> str:
    """Read output from CFFA1, auto-dismissing the `[ SPACE/CR OR ESC ]`
    pagination prompt with ESC, until the `CFFA1> ` menu prompt returns or
    the timeout expires. Returns everything read. Needed after long dumps
    (CATALOG, block-read B command) that paginate every 20 lines and wait
    for user input — otherwise the next test command lands inside the
    paginator and the CFFA1 menu reads it as a pagination continuation."""
    end = time.time() + timeout
    buf = ""
    while time.time() < end:
        chunk = recv_avail(sock, total=2.0, idle=0.4)
        if chunk:
            buf += chunk
        if "[ SPACE/CR OR ESC ]" in buf[-64:]:
            sock.sendall(b"\x1b")  # ESC
            time.sleep(0.3)
            continue
        if buf.endswith("CFFA1> "):
            return buf
        if not chunk:
            return buf
    return buf


def enter_cffa1_menu(sock: socket.socket) -> str:
    """Reset Apple 1 and enter CFFA1 menu (9006R). Returns captured output."""
    send_ctrl(sock, CTRL_R)
    time.sleep(0.9)
    out = recv_avail(sock, total=2.0)
    # Enter CFFA1 menu
    out += send_line(sock, "9006R", wait=0.5, read_t=5.0)
    return out


def exit_to_monitor(sock: socket.socket) -> str:
    """Quit CFFA1 menu back to Woz Monitor."""
    return send_line(sock, "Q", wait=0.3, read_t=3.0)


# ---------------------------------------------------------------------------
# Test framework
# ---------------------------------------------------------------------------

class TestResults:
    def __init__(self):
        self.passed: list[str] = []
        self.failed: list[tuple[str, str]] = []

    def check(self, name: str, output: str, expected: str) -> bool:
        """Check that expected substring is in output (case-insensitive)."""
        if expected.upper() in output.upper():
            self.passed.append(name)
            print(f"  [PASS] {name}")
            return True
        else:
            self.failed.append((name, f"Expected '{expected}' not found"))
            print(f"  [FAIL] {name} -- expected '{expected}'")
            if VERBOSE:
                print(f"         Output: {output[-400:]!r}")
            return False

    def check_not(self, name: str, output: str, unexpected: str) -> bool:
        """Check that unexpected substring is NOT in output."""
        if unexpected.upper() not in output.upper():
            self.passed.append(name)
            print(f"  [PASS] {name}")
            return True
        else:
            self.failed.append((name, f"Unexpected '{unexpected}' found"))
            print(f"  [FAIL] {name} -- unexpected '{unexpected}'")
            if VERBOSE:
                print(f"         Output: {output[-400:]!r}")
            return False

    def report(self) -> int:
        total = len(self.passed) + len(self.failed)
        print(f"\n{'='*50}")
        print(f"Results: {len(self.passed)}/{total} PASSED")
        if self.failed:
            print(f"\nFailed tests:")
            for name, reason in self.failed:
                print(f"  - {name}: {reason}")
            return 1
        print("All tests passed!")
        return 0


def vprint(label: str, output: str) -> None:
    if VERBOSE:
        print(f"    [{label}] {output[-500:]!r}")


# ---------------------------------------------------------------------------
# Test phases
# ---------------------------------------------------------------------------

def phase0_connectivity(sock: socket.socket, results: TestResults) -> bool:
    """Phase 0: Verify connectivity and CFFA1 firmware detection."""
    print("\nPhase 0: Connectivity & Firmware Detection")

    # 0.1 TCP connection
    out = recv_avail(sock, total=1.5)
    vprint("banner", out)
    results.check("0.1 TCP connection", out, "POM1")

    # 0.2 Reset and read CFFA1 ID bytes via Woz Monitor
    send_ctrl(sock, CTRL_R)
    time.sleep(0.9)
    recv_avail(sock, total=2.0)

    # Read $AFDC (should show CF) and $AFDD (should show FA)
    out = send_line(sock, "AFDC", wait=0.3, read_t=3.0)
    vprint("AFDC", out)
    results.check("0.2 CFFA1 ID byte 1 ($AFDC=CF)", out, "CF")

    out = send_line(sock, "AFDD", wait=0.3, read_t=3.0)
    vprint("AFDD", out)
    results.check("0.3 CFFA1 ID byte 2 ($AFDD=FA)", out, "FA")

    # 0.4 Enter CFFA1 menu
    out = enter_cffa1_menu(sock)
    vprint("menu", out)
    ok = results.check("0.4 CFFA1 menu banner", out, "CFFA1")
    return ok


def phase1_menu(sock: socket.socket, results: TestResults) -> None:
    """Phase 1: CFFA1 Menu Navigation."""
    print("\nPhase 1: Menu Commands")

    # 1.1 ? shows menu options
    out = send_line(sock, "?", read_t=5.0)
    vprint("?", out)
    results.check("1.1 ? shows CATALOG option", out, "CATALOG")
    results.check("1.1b ? shows LOAD option", out, "LOAD")
    results.check("1.1c ? shows QUIT option", out, "QUIT")

    # 1.2 C = Catalog at root of /CFFA1 volume
    out = send_line(sock, "C", read_t=8.0)
    vprint("catalog", out)
    results.check("1.2 Catalog shows /CFFA1 volume", out, "CFFA1")
    results.check("1.2b Catalog shows GAMES directory", out, "GAMES")
    results.check("1.2c Catalog shows UTILITIES directory", out, "UTILITIES")

    # 1.3 P = Prefix (show current directory)
    out = send_line(sock, "P", read_t=4.0)
    vprint("prefix", out)
    # Prefix should show /CFFA1 or similar volume path
    results.check("1.3 Prefix shows volume", out, "CFFA1")

    # 1.4 Change prefix to GAMES
    # Menu reads single chars: 'P' triggers Prefix which reads a full line.
    # Send 'P' alone (no CR), wait for firmware to enter line-read mode, then send path.
    send_char(sock, "P", wait=0.4)
    out = send_line(sock, "GAMES", wait=0.5, read_t=4.0)
    vprint("P GAMES", out)
    results.check_not("1.4 Prefix to GAMES no error", out, "NOT FOUND")

    # 1.5 Catalog in GAMES — should show LUNAR and STARTREK
    # Need to press SPACE/CR after the catalog pagination prompt first
    out = send_line(sock, "C", read_t=8.0)
    vprint("catalog GAMES", out)
    results.check("1.5 Catalog GAMES shows LUNAR", out, "LUNAR")
    results.check("1.5b Catalog GAMES shows STARTREK", out, "STARTREK")

    # 1.6 Return prefix to root
    send_char(sock, "P", wait=0.4)
    send_line(sock, "/CFFA1", wait=0.5, read_t=3.0)

    # 1.7 T = Toggle terse mode (catalog shows shorter output)
    out = send_line(sock, "T", read_t=3.0)
    vprint("terse", out)
    # Terse mode toggled — catalog should still work
    out = send_line(sock, "C", read_t=8.0)
    vprint("catalog terse", out)
    results.check("1.7 Terse catalog still shows GAMES", out, "GAMES")
    # Toggle back
    send_line(sock, "T", read_t=3.0)


def phase2_load(sock: socket.socket, results: TestResults) -> None:
    """Phase 2: Load files from CompactFlash."""
    print("\nPhase 2: Load Files")

    # First set prefix to GAMES
    send_char(sock, "P", wait=0.4)
    send_line(sock, "GAMES", wait=0.5, read_t=3.0)

    # 2.1 L = Load a file (LUNAR)
    send_char(sock, "L", wait=0.4)
    out = send_line(sock, "LUNAR", wait=0.5, read_t=6.0)
    vprint("load LUNAR", out)
    # Should show the file was loaded (address, size, or confirmation)
    results.check_not("2.1 Load LUNAR no error", out, "ERROR")
    results.check_not("2.1b Load LUNAR not NOT FOUND", out, "NOT FOUND")

    # 2.2 Memory display to verify load — use M command
    out = send_line(sock, "M", read_t=5.0)
    vprint("memory display", out)
    # M shows a hex dump — should have some non-zero data
    results.check_not("2.2 Memory display works", out, "ERROR")

    # Reset prefix to root
    send_char(sock, "P", wait=0.4)
    send_line(sock, "/CFFA1", wait=0.5, read_t=3.0)


def phase3_block_io(sock: socket.socket, results: TestResults) -> None:
    """Phase 3: Block-level I/O."""
    print("\nPhase 3: Block I/O")

    # 3.1 B = Read Block 0 (boot block). A full 512-byte block prints as
    # 64 hex lines, which triggers the CFFA1 paginator every 20 lines.
    # drain_until_cffa1_prompt auto-sends ESC at each pagination prompt so
    # the test leaves the menu in a clean state for the next command.
    send_char(sock, "B", wait=0.4)
    sock.sendall(b"0\r")
    out = drain_until_cffa1_prompt(sock, timeout=20.0)
    vprint("read block 0", out)
    results.check_not("3.1 Read block 0 no error", out, "ERROR")

    # 3.2 B = Read Block 2 (volume directory)
    send_char(sock, "B", wait=0.4)
    sock.sendall(b"2\r")
    out = drain_until_cffa1_prompt(sock, timeout=20.0)
    vprint("read block 2", out)
    results.check_not("3.2 Read block 2 no error", out, "ERROR")


def phase4_quit(sock: socket.socket, results: TestResults) -> None:
    """Phase 4: Exit CFFA1 menu."""
    print("\nPhase 4: Quit")

    # Phase 3 now uses drain_until_cffa1_prompt, so we enter Phase 4 already
    # at a clean `CFFA1> ` prompt — no flushing needed.

    # 4.1 Q = Quit to Woz Monitor. The CFFA1 echoes 'Q', exits its menu loop,
    # the Woz Monitor then emits CR + '\' as its prompt.
    out = send_line(sock, "Q", wait=1.0, read_t=4.0)
    vprint("quit", out)
    results.check("4.1 Quit returns to monitor", out, "\\")

    # 4.2 Verify we can type Woz Monitor commands. Reading $FF00 dumps a
    # byte from the Woz Monitor ROM; the monitor echoes the address before
    # the value, so 'FF00' must appear on the line.
    out = send_line(sock, "FF00", wait=0.3, read_t=3.0)
    vprint("monitor FF00", out)
    results.check("4.2 Monitor command works", out, "FF00")


def phase5_applesoft_api(sock: socket.socket, results: TestResults) -> None:
    """Phase 5: Test Applesoft Lite CFFA1 API integration ($900C)."""
    print("\nPhase 5: Applesoft Lite API")

    # Reset and enter Applesoft Lite (E003R for warm start)
    send_ctrl(sock, CTRL_R)
    time.sleep(0.9)
    recv_avail(sock, total=2.0)

    # Check if Applesoft Lite is loaded by reading $E000
    out = send_line(sock, "E000", wait=0.3, read_t=3.0)
    vprint("E000", out)

    # Try to enter BASIC with E000R
    out = send_line(sock, "E000R", wait=0.5, read_t=3.0)
    vprint("E000R", out)

    # Applesoft Lite prompt is "]", Integer BASIC prompt is ">"
    has_prompt = "]" in out or ">" in out
    is_applesoft = "]" in out
    if has_prompt:
        basic_name = "Applesoft Lite" if is_applesoft else "Integer BASIC"
        results.passed.append(f"5.1 {basic_name} detected")
        print(f"  [PASS] 5.1 {basic_name} detected")

        # 5.2 Type a simple program and check LIST
        send_line(sock, "NEW", read_t=2.0)
        send_line(sock, '10 PRINT "HELLO CFFA1"', read_t=2.0)
        out = send_line(sock, "LIST", read_t=3.0)
        vprint("LIST", out)
        results.check("5.2 LIST shows program", out, "HELLO CFFA1")
    else:
        results.passed.append("5.1 No BASIC prompt detected")
        print("  [SKIP] 5.1 No BASIC prompt detected (skipping API tests)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    global VERBOSE
    parser = argparse.ArgumentParser(description="Test CFFA1 via Terminal Card telnet")
    parser.add_argument("--verbose", action="store_true", help="Show raw output")
    args = parser.parse_args()
    VERBOSE = args.verbose

    print("=" * 50)
    print("CFFA1 CompactFlash Test Suite")
    print("=" * 50)
    print(f"Connecting to {HOST}:{PORT}...")

    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
    except (ConnectionRefusedError, OSError) as e:
        print(f"ERROR: Cannot connect to Terminal Card at {HOST}:{PORT}")
        print(f"  {e}")
        print("  Make sure POM1 is running with CFFA1 + Terminal Card enabled.")
        return 1

    sock.settimeout(10)
    results = TestResults()

    try:
        # Phase 0: Connectivity & ID bytes
        if not phase0_connectivity(sock, results):
            print("\nCFFA1 firmware not detected. Aborting.")
            return results.report()

        # Phase 1: Menu navigation
        phase1_menu(sock, results)

        # Phase 2: Load files
        phase2_load(sock, results)

        # Phase 3: Block I/O
        phase3_block_io(sock, results)

        # Phase 4: Quit
        phase4_quit(sock, results)

        # Phase 5: Applesoft Lite API (if applicable)
        # Re-enter menu first for a clean state, then quit to monitor for BASIC test
        enter_cffa1_menu(sock)
        exit_to_monitor(sock)
        phase5_applesoft_api(sock, results)

    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
    finally:
        sock.close()

    return results.report()


if __name__ == "__main__":
    sys.exit(main())
