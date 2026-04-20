#!/usr/bin/env python3
"""
test_applesoft_microsd_save_load.py -- Test ASAVE/LOAD of Applesoft Lite program via SD CARD OS.

Scenario (P-LAB preset: microSD + Applesoft Lite at $6000):
  1. Enter Applesoft Lite (6000R), blindly type a BASIC program
     (P-LAB "Fast Terminal" ROM does NOT echo to $D012, so Terminal Card
      sees no output — we verify via memory reads after reset)
  2. Reset to Woz Monitor, verify program in RAM at $0801
  3. Enter SD CARD OS (8000R), ASAVE the program
  4. Verify #F8 tagged file exists on the host filesystem
  5. Hard reset, verify program is gone
  6. Enter SD CARD OS, LOAD the file back
  7. Verify program data at $0801 matches original
  8. Cleanup: delete the test file

Pre-requisites:
  ./POM1 --preset p-lab
"""
from __future__ import annotations

import glob
import os
import select
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18
SDCARD_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sdcard")
TEST_FILE = "SDTEST"

VERBOSE = "--verbose" in sys.argv or "-v" in sys.argv


def vprint(label: str, output: str):
    if VERBOSE:
        print(f"    [{label}] {output[-500:]!r}")


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


def send_blind(sock, cmd, wait=0.5):
    """Send a command without reading response (for P-LAB Applesoft which has no Terminal Card echo)."""
    sock.sendall((cmd + "\r").encode("ascii"))
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


def read_byte_via_monitor(sock, addr_hex):
    """Read a byte via Woz Monitor. Returns hex string like 'XX'."""
    out = send_line(sock, addr_hex, wait=0.3, read_t=3.0)
    # Parse "ADDR: XX" from output
    for line in out.strip().split("\n"):
        line = line.strip()
        if ":" in line and addr_hex.upper()[:4] in line.upper():
            _, _, hexpart = line.partition(":")
            return hexpart.strip()
    return ""


def read_memory_range(sock, start_hex, end_hex):
    """Read a memory range via Woz Monitor. Returns raw hex bytes string."""
    out = send_line(sock, f"{start_hex}.{end_hex}", wait=0.3, read_t=4.0)
    vprint(f"${start_hex}-${end_hex}", out)
    result = []
    for line in out.strip().split("\n"):
        line = line.strip()
        if ":" in line:
            _, _, hexpart = line.partition(":")
            result.append(hexpart.strip())
    return " ".join(result)


def find_test_file():
    """Find the ASAVE'd file on the host filesystem (SDTEST#F8*)."""
    pattern = os.path.join(SDCARD_PATH, f"{TEST_FILE}#F8*")
    matches = glob.glob(pattern)
    return matches[0] if matches else None


def cleanup_test_file():
    """Remove any leftover test file from host filesystem."""
    f = find_test_file()
    if f and os.path.exists(f):
        os.remove(f)
        print(f"    Cleaned up {os.path.basename(f)}")


def main():
    global passed, failed
    print("=" * 60)
    print("Applesoft Lite (P-LAB microSD) — ASAVE / LOAD Round-Trip")
    print("=" * 60)
    print("NOTE: P-LAB 'Fast Terminal' ROM does not echo to $D012.")
    print("      Applesoft interaction is blind; verified via memory reads.")

    # Clean up from any previous failed run
    cleanup_test_file()

    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
    except (ConnectionRefusedError, OSError) as e:
        print(f"\nERROR: Cannot connect to {HOST}:{PORT}: {e}")
        print("Start POM1 with: ./POM1 --preset p-lab")
        return 1

    sock.settimeout(10)

    try:
        recv_avail(sock, total=1.5)

        # ============================================================
        # STEP 1: Verify P-LAB Applesoft ROM at $6000
        # ============================================================
        print("\nStep 1: Verify P-LAB Applesoft ROM")
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)

        out = read_byte_via_monitor(sock, "6000")
        check("1.1 ROM at $6000 (JMP opcode)", out, "4C")

        # ============================================================
        # STEP 2: Enter Applesoft (blind) and type a program
        # ============================================================
        print("\nStep 2: Enter Applesoft Lite (6000R) — blind mode")
        send_blind(sock, "6000R", wait=1.0)

        # Type BASIC program blindly
        send_blind(sock, "NEW", wait=0.5)
        send_blind(sock, '10 REM SDCARD SAVE TEST', wait=0.5)
        send_blind(sock, '20 PRINT "PLAB FOREVER"', wait=0.5)
        send_blind(sock, '30 END', wait=0.5)

        # ============================================================
        # STEP 3: Reset and verify program in memory
        # ============================================================
        print("\nStep 3: Verify program in RAM via Woz Monitor")
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)

        # Read tokenized program at $0801
        mem = read_memory_range(sock, "0801", "0840")
        print(f"    $0801+: {mem[:80]}...")

        # The program should contain "SDCARD SAVE TEST" as ASCII bytes
        # In tokenized Applesoft: REM token ($B2) followed by literal text
        check("3.1 Program in RAM (contains SDCARD)", mem, "53 44 43 41 52 44")

        # Read TXTTAB/VARTAB for later restore
        zp_hex = read_memory_range(sock, "0067", "006A")
        print(f"    TXTTAB/VARTAB ($67-$6A): {zp_hex}")

        # ============================================================
        # STEP 4: ASAVE via SD CARD OS
        # ============================================================
        print("\nStep 4: ASAVE via SD CARD OS (8000R)")
        out = send_line(sock, "8000R", wait=0.5, read_t=4.0)
        vprint("8000R", out)
        check("4.1 SD CARD OS started", out, "SD CARD OS")

        out = send_line(sock, f"ASAVE {TEST_FILE}", read_t=8.0)
        vprint("ASAVE", out)
        check("4.2 ASAVE reports OK", out, "OK")
        check_not("4.3 ASAVE no error", out, "?")

        # ============================================================
        # STEP 5: Verify file on host filesystem
        # ============================================================
        print("\nStep 5: Verify file on host filesystem")
        time.sleep(0.3)
        test_file_path = find_test_file()
        if test_file_path:
            fname = os.path.basename(test_file_path)
            fsize = os.path.getsize(test_file_path)
            passed += 1
            print(f"  [PASS] 5.1 File exists: {fname} ({fsize} bytes)")
            check("5.2 File has #F8 tag (Applesoft)", fname, "#F8")

            # Read file content for later comparison
            with open(test_file_path, "rb") as f:
                saved_data = f.read()
            print(f"    Saved {len(saved_data)} bytes")
        else:
            failed += 1
            print(f"  [FAIL] 5.1 File not found matching {TEST_FILE}#F8*")
            print(f"    sdcard/ contents: {os.listdir(SDCARD_PATH)[:20]}")
            saved_data = None

        # ============================================================
        # STEP 6: Hard reset — verify program is cleared
        # ============================================================
        print("\nStep 6: Hard reset — verify program cleared")
        send_line(sock, "EXIT", read_t=3.0)
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)

        # Zero out the program area to prove LOAD restores it
        send_line(sock, "0801: 00 00 00 00 00 00 00 00", wait=0.3, read_t=3.0)
        mem_after_clear = read_memory_range(sock, "0801", "0808")
        check("6.1 Program area zeroed", mem_after_clear, "00 00 00 00 00 00 00 00")

        # ============================================================
        # STEP 7: LOAD via SD CARD OS
        # ============================================================
        print("\nStep 7: LOAD via SD CARD OS")
        out = send_line(sock, "8000R", wait=0.5, read_t=4.0)
        vprint("8000R", out)

        out = send_line(sock, f"LOAD {TEST_FILE}", read_t=10.0)
        vprint("LOAD", out)
        check("7.1 LOAD found file", out, "FOUND")
        check("7.2 LOAD reports OK", out, "OK")

        # Exit SD CARD OS
        send_line(sock, "EXIT", read_t=3.0)

        # ============================================================
        # STEP 8: Verify reloaded program in memory
        # ============================================================
        print("\nStep 8: Verify reloaded program in memory")
        mem_after_load = read_memory_range(sock, "0801", "0840")
        print(f"    $0801+: {mem_after_load[:80]}...")

        check("8.1 Reloaded program (contains SDCARD)", mem_after_load, "53 44 43 41 52 44")

        # Compare with saved file data (should match memory at $0801)
        if saved_data:
            # Read back the exact same range that was saved
            reload_range = read_memory_range(sock, "0801", f"{0x0801 + len(saved_data) - 1:04X}")
            # Convert hex string to bytes for comparison
            reload_bytes = bytes(int(b, 16) for b in reload_range.split() if len(b) == 2)
            if reload_bytes == saved_data:
                passed += 1
                print(f"  [PASS] 8.2 Reloaded data matches saved file ({len(saved_data)} bytes)")
            else:
                failed += 1
                print(f"  [FAIL] 8.2 Data mismatch: saved {len(saved_data)}B vs loaded {len(reload_bytes)}B")
                if VERBOSE:
                    print(f"    saved:  {saved_data[:32].hex()}")
                    print(f"    loaded: {reload_bytes[:32].hex()}")

        # ============================================================
        # STEP 9: Warm-start Applesoft and RUN (blind)
        # ============================================================
        print("\nStep 9: Warm-start Applesoft and RUN (blind)")
        # Restore TXTTAB/VARTAB pointers
        if zp_hex:
            # Parse the 4 bytes from the zp_hex string
            zp_bytes = [b for b in zp_hex.split() if len(b) == 2]
            if len(zp_bytes) >= 4:
                poke_cmd = f"67: {' '.join(zp_bytes[:4])}"
                send_line(sock, poke_cmd, wait=0.3, read_t=3.0)
                print(f"    Restored TXTTAB/VARTAB: {poke_cmd}")

        # Warm-start Applesoft (6003R) — blind
        send_blind(sock, "6003R", wait=0.5)
        # RUN the program — blind (output not visible via Terminal Card)
        send_blind(sock, "RUN", wait=1.0)

        # Reset and check if RUN left any trace
        # (RUN prints "PLAB FOREVER" to Apple 1 screen, which we can't see via terminal)
        # Just verify Applesoft didn't crash by checking we can still enter the monitor
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)
        out = send_line(sock, "FF00", wait=0.3, read_t=3.0)
        check("9.1 Monitor still responsive after RUN", out, "FF00")

        # ============================================================
        # STEP 11: Native Applesoft SAVE/LOAD (from BASIC prompt)
        # ============================================================
        # Applesoft Lite SD1.3 integrates SAVE/LOAD commands that call the
        # SD CARD OS directly, without going through 8000R first. Syntax is
        # `SAVE "NAME"` and `LOAD "NAME"` — quoted string literal, unlike
        # the CFFA1 variant which uses unquoted names. The firmware echoes
        # "SAVING\n<NAME>#F80801\n$hhhh-$hhhh (NN BYTES)\nOK" on success.
        print("\nStep 11: Native Applesoft SAVE/LOAD from BASIC prompt")
        send_ctrl(sock, CTRL_R)
        time.sleep(0.9)
        recv_avail(sock, total=2.0)
        out = send_line(sock, "6000R", wait=0.5, read_t=3.0)
        check("11.1 Applesoft cold start", out, "]")

        send_line(sock, "NEW", read_t=2.0)
        send_line(sock, '10 PRINT "NATIVE MSD"', read_t=2.0)
        send_line(sock, '20 PRINT 7 * 6', read_t=2.0)
        send_line(sock, '30 END', read_t=2.0)

        out = send_line(sock, 'SAVE "NATIVE"', wait=1.5, read_t=5.0)
        print(f"    [BASIC SAVE] {out[-300:]!r}")
        check("11.2 SAVE reports SAVING", out, "SAVING")
        check("11.3 SAVE writes tagged name", out, "NATIVE#F80801")
        check_not("11.4 SAVE no error", out, "ERR")

        # Host-side: verify the file really landed on disk
        native_path = os.path.join(SDCARD_PATH, "NATIVE#F80801")
        if os.path.exists(native_path):
            passed += 1
            size = os.path.getsize(native_path)
            print(f"  [PASS] 11.5 File NATIVE#F80801 exists ({size} bytes)")
        else:
            failed += 1
            print(f"  [FAIL] 11.5 File NATIVE#F80801 not found on host")

        # NEW wipes the in-memory program
        send_line(sock, "NEW", read_t=2.0)
        out = send_line(sock, "LIST", read_t=3.0)
        check_not("11.6 Cleared after NEW", out, "NATIVE MSD")

        out = send_line(sock, 'LOAD "NATIVE"', wait=1.5, read_t=5.0)
        print(f"    [BASIC LOAD] {out[-300:]!r}")
        check("11.7 LOAD finds file", out, "FOUND NATIVE#F80801")
        check("11.8 LOAD reports LOADING", out, "LOADING")
        check_not("11.9 LOAD no error", out, "ERR")

        out = send_line(sock, "LIST", read_t=4.0)
        print(f"    [LIST after LOAD] {out[-300:]!r}")
        check("11.10 Loaded line 10", out, "NATIVE MSD")
        check("11.11 Loaded line 20", out, "7 * 6")
        check("11.12 Loaded line 30", out, "END")

        out = send_line(sock, "RUN", read_t=4.0)
        print(f"    [RUN] {out[-200:]!r}")
        check("11.13 RUN prints line 10", out, "NATIVE MSD")
        check("11.14 RUN computes 7*6", out, "42")

        # Cleanup NATIVE#F80801
        if os.path.exists(native_path):
            try:
                os.remove(native_path)
                print(f"    Cleaned up NATIVE#F80801")
            except OSError as e:
                print(f"    WARN: could not remove {native_path}: {e}")

        # ============================================================
        # CLEANUP: Delete test file
        # ============================================================
        print("\nCleanup: Delete test file")
        cleanup_test_file()
        if not find_test_file():
            passed += 1
            print(f"  [PASS] 10.1 Test file cleaned up")
        else:
            failed += 1
            print(f"  [FAIL] 10.1 Test file still exists")

    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
    finally:
        sock.close()
        # Ensure cleanup even on failure
        cleanup_test_file()

    total = passed + failed
    print(f"\n{'='*60}")
    print(f"Results: {passed}/{total} PASSED")
    if failed:
        print(f"{failed} test(s) FAILED")
        return 1
    print("All tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
