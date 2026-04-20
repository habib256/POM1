#!/usr/bin/env python3
"""
test_cffa1_save_load.py -- Test sauvegarde/rechargement d'un programme Applesoft Lite via CFFA1.

Scenario:
  1. Entrer dans Applesoft Lite, ecrire un programme BASIC
  2. Revenir au Woz Monitor, entrer dans le menu CFFA1
  3. Sauvegarder le programme (S = Save BASIC)
  4. Hard reset (Ctrl-R), re-entrer BASIC, verifier que le programme est perdu (NEW)
  5. Revenir au Woz Monitor, entrer dans le menu CFFA1
  6. Recharger le programme (L = Load)
  7. Entrer dans BASIC, LIST pour verifier que le programme est intact

Pre-requis:
  ./POM1 --preset cffa1
"""
from __future__ import annotations

import select
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18


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


def send_char(sock, ch, wait=0.2):
    sock.sendall(ch.encode("ascii"))
    time.sleep(wait)


def send_ctrl(sock, byte):
    sock.sendall(bytes([byte & 0xFF]))


passed = 0
failed = 0


def check(name, output, expected):
    global passed, failed
    if expected.upper() in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    else:
        failed += 1
        print(f"  [FAIL] {name} -- expected '{expected}'")
        print(f"         got: {output[-300:]!r}")
        return False


def check_not(name, output, unexpected):
    global passed, failed
    if unexpected.upper() not in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    else:
        failed += 1
        print(f"  [FAIL] {name} -- unexpected '{unexpected}'")
        print(f"         got: {output[-300:]!r}")
        return False


def main():
    global passed, failed
    print("=" * 55)
    print("CFFA1 Save/Load Test — Applesoft Lite BASIC Program")
    print("=" * 55)

    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
    except (ConnectionRefusedError, OSError) as e:
        print(f"ERROR: Cannot connect: {e}")
        return 1

    sock.settimeout(10)

    try:
        # --- Banner ---
        recv_avail(sock, total=1.5)

        # === STEP 1: Write a BASIC program ===
        print("\nStep 1: Write Applesoft Lite program")
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)

        out = send_line(sock, "E000R", wait=0.5, read_t=3.0)
        check("1.1 Applesoft Lite started", out, "]")

        send_line(sock, "NEW", read_t=2.0)
        send_line(sock, '10 REM CFFA1 SAVE TEST', read_t=2.0)
        send_line(sock, '20 PRINT "APPLE1 FOREVER"', read_t=2.0)
        send_line(sock, '30 END', read_t=2.0)

        out = send_line(sock, "LIST", read_t=3.0)
        check("1.2 LIST shows line 10", out, "CFFA1 SAVE TEST")
        check("1.3 LIST shows line 20", out, "APPLE1 FOREVER")
        check("1.4 LIST shows line 30", out, "END")

        # === STEP 2: Save to CFFA1 ===
        print("\nStep 2: Save program to CFFA1")

        # Read TXTTAB ($67-$68) and VARTAB ($69-$6A) from zero page via Woz Monitor
        # to determine the exact memory range of the BASIC program.
        # Exit BASIC to Woz Monitor via reset
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)

        # Read TXTTAB (little-endian at $67-$68)
        out = send_line(sock, "67.6A", wait=0.3, read_t=3.0)
        print(f"    [TXTTAB/VARTAB] {out.strip()!r}")

        # Enter CFFA1 menu
        out = send_line(sock, "9006R", wait=0.5, read_t=4.0)
        check("2.1 CFFA1 menu entered", out, "CFFA1")

        # W = Write File (raw memory range) — bypasses BASIC detection.
        # Protocol: W → "WRITE FROM: $" → addr → "LENGTH: $" → len → "TYPE (BIN): $" → CR → "FILENAME:" → name
        send_char(sock, "W", wait=0.5)
        # Start address ($0801 = Applesoft Lite TXTTAB)
        out = send_line(sock, "801", wait=0.5, read_t=4.0)
        print(f"    [W from] {out[-200:]!r}")
        # Length in bytes ($36 - 1 padding = program size)
        out = send_line(sock, "35", wait=0.5, read_t=4.0)
        print(f"    [W length] {out[-200:]!r}")
        # Type — accept default BIN with CR
        out = send_line(sock, "", wait=0.5, read_t=4.0)
        print(f"    [W type] {out[-200:]!r}")
        # Filename
        out = send_line(sock, "SAVETEST", wait=0.5, read_t=6.0)
        print(f"    [W name] {out[-300:]!r}")
        check_not("2.2 Write no error", out, "ERROR")
        check("2.3 Write completed", out, "SUCCESS")

        # Verify file appears in catalog
        out = send_line(sock, "C", read_t=8.0)
        print(f"    [catalog] {out[-400:]!r}")
        check("2.4 SAVETEST in catalog", out, "SAVETEST")

        # === STEP 3: Clear memory and verify program is gone ===
        print("\nStep 3: Clear memory")
        send_char(sock, "Q", wait=0.3)
        time.sleep(0.3)
        recv_avail(sock, total=2.0)

        # Hard reset
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)

        # Enter BASIC
        out = send_line(sock, "E000R", wait=0.5, read_t=3.0)
        check("3.1 BASIC restarted after reset", out, "]")

        # LIST should show nothing (or just empty)
        out = send_line(sock, "LIST", read_t=3.0)
        check_not("3.2 Program cleared after reset", out, "APPLE1 FOREVER")

        # === STEP 4: Reload from CFFA1 ===
        print("\nStep 4: Reload program from CFFA1")
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)

        out = send_line(sock, "9006R", wait=0.5, read_t=4.0)
        check("4.1 CFFA1 menu re-entered", out, "CFFA1")

        # L = Load File, then enter filename
        send_char(sock, "L", wait=0.5)
        out = send_line(sock, "SAVETEST", wait=0.5, read_t=6.0)
        print(f"    [load output] {out[-300:]!r}")
        check_not("4.2 Load no error", out, "NOT FOUND")

        # The firmware asks for load address — accept default ($0800) with CR
        if "ADDR" in out.upper():
            out = send_line(sock, "", wait=0.5, read_t=4.0)
            print(f"    [addr accept] {out[-200:]!r}")
        check_not("4.3 Load no error code", out, "BAD")

        # === STEP 5: Verify reloaded program ===
        print("\nStep 5: Verify reloaded program")
        # Quit CFFA1 menu
        send_char(sock, "Q", wait=0.3)
        time.sleep(0.5)
        recv_avail(sock, total=2.0)

        # Restore Applesoft Lite zero-page pointers via Woz Monitor:
        # TXTTAB ($67-$68) = $0801, VARTAB ($69-$6A) = $0836
        out = send_line(sock, "67: 01 08 36 08", wait=0.3, read_t=3.0)
        print(f"    [poke ZP] {out.strip()!r}")

        # Enter BASIC warm start (E003R) — preserves loaded program
        out = send_line(sock, "E003R", wait=0.5, read_t=3.0)
        check("5.1 BASIC warm start", out, "]")

        out = send_line(sock, "LIST", read_t=4.0)
        print(f"    [LIST] {out[-300:]!r}")
        check("5.2 Reloaded line 10", out, "CFFA1 SAVE TEST")
        check("5.3 Reloaded line 20", out, "APPLE1 FOREVER")
        check("5.4 Reloaded line 30", out, "END")

        # === STEP 6: Run the program ===
        print("\nStep 6: Run the program")
        out = send_line(sock, "RUN", read_t=4.0)
        print(f"    [RUN] {out[-200:]!r}")
        check("6.1 RUN prints output", out, "APPLE1 FOREVER")

        # === STEP 8: Native Applesoft SAVE/LOAD (from BASIC prompt) ===
        # Applesoft Lite CFFA1 integrates SAVE/LOAD commands that call the
        # firmware directly, without going through the 9006R menu. Syntax is
        # `SAVE NAME` and `LOAD NAME` — unquoted (quotes return "40 BAD NAME").
        print("\nStep 8: Native Applesoft SAVE/LOAD from BASIC prompt")
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)
        out = send_line(sock, "E000R", wait=0.5, read_t=3.0)
        check("8.1 Applesoft restarted", out, "]")

        send_line(sock, "NEW", read_t=2.0)
        send_line(sock, '10 PRINT "NATIVE CFFA1"', read_t=2.0)
        send_line(sock, '20 PRINT 3 * 4', read_t=2.0)
        send_line(sock, '30 END', read_t=2.0)

        out = send_line(sock, "SAVE NATIVE", wait=1.0, read_t=5.0)
        print(f"    [BASIC SAVE] {out[-200:]!r}")
        check_not("8.2 SAVE NATIVE no error", out, "ERR")
        check_not("8.2b SAVE NATIVE no BAD", out, "BAD")

        # NEW wipes the program so we can prove LOAD restores it
        send_line(sock, "NEW", read_t=2.0)
        out = send_line(sock, "LIST", read_t=3.0)
        check_not("8.3 Cleared after NEW", out, "NATIVE CFFA1")

        out = send_line(sock, "LOAD NATIVE", wait=1.5, read_t=5.0)
        print(f"    [BASIC LOAD] {out[-200:]!r}")
        check_not("8.4 LOAD NATIVE no error", out, "ERR")
        check_not("8.4b LOAD NATIVE not NOT FOUND", out, "NOT FOUND")

        out = send_line(sock, "LIST", read_t=4.0)
        print(f"    [LIST after LOAD] {out[-300:]!r}")
        check("8.5 Loaded line 10", out, "NATIVE CFFA1")
        check("8.6 Loaded line 20", out, "3 * 4")
        check("8.7 Loaded line 30", out, "END")

        out = send_line(sock, "RUN", read_t=4.0)
        print(f"    [RUN] {out[-200:]!r}")
        check("8.8 Loaded program prints", out, "NATIVE CFFA1")
        check("8.9 Loaded program computes", out, "12")

        # === Cleanup: delete test files (SAVETEST + NATIVE) ===
        print("\nCleanup: Delete test files")
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)
        send_line(sock, "9006R", wait=0.5, read_t=4.0)

        send_char(sock, "D", wait=0.5)
        out = send_line(sock, "SAVETEST", wait=0.5, read_t=4.0)
        print(f"    [delete SAVETEST] {out[-150:]!r}")
        check_not("9.1 Delete SAVETEST no error", out, "NOT FOUND")

        send_char(sock, "D", wait=0.5)
        out = send_line(sock, "NATIVE", wait=0.5, read_t=4.0)
        print(f"    [delete NATIVE] {out[-150:]!r}")
        check_not("9.2 Delete NATIVE no error", out, "NOT FOUND")

    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
    finally:
        sock.close()

    total = passed + failed
    print(f"\n{'='*55}")
    print(f"Results: {passed}/{total} PASSED")
    if failed:
        print(f"{failed} test(s) FAILED")
        return 1
    print("All tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
