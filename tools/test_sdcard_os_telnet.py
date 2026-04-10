#!/usr/bin/env python3
"""
test_sdcard_os_telnet.py -- Test complet du SD CARD OS via Terminal Card telnet.

Pre-requis :
  - POM1 en cours d'execution avec microSD + Terminal Card actifs
    (preset "Replica 1", "P-LAB Apple 1" ou "POM1 Full")
  - Le Terminal Card ecoute sur localhost:6502

Usage :
  python3 tools/test_sdcard_os_telnet.py [--setup] [--cleanup] [--verbose]

  --setup    Prepare les donnees de test dans sdcard/ avant de lancer les tests
  --cleanup  Nettoie les artefacts de test apres execution
  --verbose  Affiche la sortie brute de chaque commande
"""
from __future__ import annotations

import argparse
import os
import select
import shutil
import socket
import struct
import sys
import time

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
SDCARD_PATH = os.path.join(PROJECT_DIR, "sdcard")

# 6502 program: prints "OK" + CR via Woz Monitor ECHO ($FFEF), then JMP $FF00
# LDA #$CF; JSR $FFEF; LDA #$CB; JSR $FFEF; LDA #$8D; JSR $FFEF; JMP $FF00
SMALL_PROGRAM = bytes([
    0xA9, 0xCF, 0x20, 0xEF, 0xFF,  # LDA #'O'|$80, JSR ECHO
    0xA9, 0xCB, 0x20, 0xEF, 0xFF,  # LDA #'K'|$80, JSR ECHO
    0xA9, 0x8D, 0x20, 0xEF, 0xFF,  # LDA #CR|$80,  JSR ECHO
    0x4C, 0x00, 0xFF,              # JMP $FF00
])

VERBOSE = False


# ---------------------------------------------------------------------------
# Test data setup / cleanup
# ---------------------------------------------------------------------------

def prepare_test_data() -> None:
    """Create test files and directories in sdcard/."""
    print("Preparing test data in", SDCARD_PATH)

    # Root-level text file for TYPE
    with open(os.path.join(SDCARD_PATH, "HELLO.TXT"), "w") as f:
        f.write("HELLO WORLD FROM APPLE 1\r\n")
        f.write("THIS IS A TEST FILE\r\n")

    # Small 6502 binary (tagged)
    with open(os.path.join(SDCARD_PATH, "SMALL#060300"), "wb") as f:
        f.write(SMALL_PROGRAM)

    # 256 bytes sequential for READ/DUMP tests
    with open(os.path.join(SDCARD_PATH, "TESTBIN"), "wb") as f:
        f.write(bytes(range(256)))

    # Files for DEL/RM tests
    for name in ("DELME.TXT", "DELME2.TXT"):
        with open(os.path.join(SDCARD_PATH, name), "w") as f:
            f.write("DELETE ME\r\n")

    # TESTDIR with sub-directory and a BASIC-tagged file
    testdir = os.path.join(SDCARD_PATH, "TESTDIR")
    os.makedirs(os.path.join(testdir, "SUB1"), exist_ok=True)
    with open(os.path.join(testdir, "FILE1#F10800"), "wb") as f:
        f.write(b"\x00" * 16)

    # EMPTYDIR for RMDIR success test
    os.makedirs(os.path.join(SDCARD_PATH, "EMPTYDIR"), exist_ok=True)

    # NOTEMPTY for RMDIR failure test
    notempty = os.path.join(SDCARD_PATH, "NOTEMPTY")
    os.makedirs(notempty, exist_ok=True)
    with open(os.path.join(notempty, "KEEP.TXT"), "w") as f:
        f.write("KEEP\r\n")

    print("Test data ready.")


def cleanup_test_data() -> None:
    """Remove test artifacts from sdcard/."""
    print("Cleaning up test data...")
    for name in ("HELLO.TXT", "SMALL#060300", "TESTBIN", "DELME.TXT", "DELME2.TXT",
                 "WTEST", "STEST#060400"):
        p = os.path.join(SDCARD_PATH, name)
        if os.path.exists(p):
            os.remove(p)
    for d in ("TESTDIR", "EMPTYDIR", "NOTEMPTY", "NEWDIR", "TEMPDIR", "MDTEST"):
        p = os.path.join(SDCARD_PATH, d)
        if os.path.isdir(p):
            shutil.rmtree(p)
    print("Cleanup done.")


# ---------------------------------------------------------------------------
# Telnet helpers
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
    text = buf.decode("latin-1", errors="replace")
    return text


def send_line(sock: socket.socket, cmd: str, wait: float = 0.3, read_t: float = 5.0) -> str:
    """Send command + CR, wait, read response."""
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t, idle=0.4)


def send_ctrl(sock: socket.socket, byte: int) -> None:
    """Send a control byte."""
    sock.sendall(bytes([byte & 0xFF]))


def enter_sdcard_os(sock: socket.socket) -> str:
    """Reset Apple 1 and enter SD CARD OS. Returns captured output."""
    # CTRL-R to reset
    send_ctrl(sock, CTRL_R)
    time.sleep(0.9)
    out = recv_avail(sock, total=2.0)

    # Send 8000R to enter SD CARD OS
    out += send_line(sock, "8000R", wait=0.5, read_t=4.0)
    return out


def enter_woz_monitor(sock: socket.socket) -> str:
    """Exit SD CARD OS back to Woz Monitor."""
    return send_line(sock, "EXIT", wait=0.3, read_t=3.0)


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
                print(f"         Output: {output[-300:]!r}")
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
            return False

    def check_host_exists(self, name: str, path: str, should_exist: bool) -> bool:
        """Check host filesystem for existence."""
        exists = os.path.exists(path)
        if exists == should_exist:
            self.passed.append(name)
            print(f"  [PASS] {name}")
            return True
        else:
            verb = "exist" if should_exist else "not exist"
            self.failed.append((name, f"Expected path to {verb}: {path}"))
            print(f"  [FAIL] {name} -- expected path to {verb}: {path}")
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
        return 0


def vprint(label: str, output: str) -> None:
    """Print output if verbose mode is on."""
    if VERBOSE:
        print(f"    [{label}] {output[-400:]!r}")


# ---------------------------------------------------------------------------
# Test phases
# ---------------------------------------------------------------------------

def phase0_connectivity(sock: socket.socket, results: TestResults) -> bool:
    """Phase 0: Verify connectivity and enter SD CARD OS."""
    print("\nPhase 0: Connectivity")

    # 0.1 TCP connection + welcome banner
    out = recv_avail(sock, total=1.5)
    vprint("banner", out)
    results.check("0.1 TCP connection & banner", out, "POM1")

    # 0.2 Enter SD CARD OS
    out = enter_sdcard_os(sock)
    vprint("sdcard_os", out)
    ok = results.check("0.2 Enter SD CARD OS", out, "SD CARD OS")
    return ok


def phase1_readonly(sock: socket.socket, results: TestResults) -> None:
    """Phase 1: Read-only commands."""
    print("\nPhase 1: Read-Only Commands")

    # 1.1 ? command list (ROM reads /HELP/COMMANDS.TXT from SD card)
    out = send_line(sock, "?", read_t=6.0)
    vprint("?", out)
    results.check("1.1 ? command list", out, "COMMANDS")

    # 1.2 HELP SAVE (ROM reads /HELP/SAVE.TXT)
    out = send_line(sock, "HELP SAVE", read_t=6.0)
    vprint("HELP SAVE", out)
    results.check("1.2 HELP SAVE shows syntax", out, "SYNTAX")
    results.check("1.2b HELP SAVE mentions SAVE", out, "SAVE FILENAME")

    # 1.3 HELP DIR (ROM reads /HELP/DIR.TXT)
    out = send_line(sock, "HELP DIR", read_t=6.0)
    vprint("HELP DIR", out)
    results.check("1.3 HELP DIR shows syntax", out, "SYNTAX")

    # 1.4 PWD at root
    out = send_line(sock, "PWD")
    vprint("PWD", out)
    results.check("1.4 PWD at root", out, "/")

    # 1.5 DIR at root
    out = send_line(sock, "DIR", read_t=8.0)
    vprint("DIR", out)
    results.check("1.5 DIR lists HGR", out, "HGR")

    # 1.6 LS at root
    out = send_line(sock, "LS", read_t=8.0)
    vprint("LS", out)
    results.check("1.6 LS shows directories", out, "HGR")

    # 1.7 DIR HGR
    out = send_line(sock, "DIR HGR", read_t=8.0)
    vprint("DIR HGR", out)
    results.check("1.7 DIR HGR shows files", out, "N000")
    results.check("1.7b DIR HGR shows type", out, "BIN")

    # 1.8 LS HGR
    out = send_line(sock, "LS HGR", read_t=8.0)
    vprint("LS HGR", out)
    results.check("1.8 LS HGR shows files", out, "8192")

    # 1.9 TEST — ROM sends bytes 0x00-0xFF to MCU, MCU echoes each XOR 0xFF.
    # ROM displays '*' after each full pass of 256 bytes. User presses ESC to exit.
    sock.sendall(("TEST" + "\r").encode("ascii"))
    time.sleep(0.3)
    out = recv_avail(sock, total=3.0, idle=0.3)
    vprint("TEST start", out)
    results.check("1.9 TEST command starts", out, "TESTING")
    # Wait for at least one '*' (one full 256-byte pass verified)
    if "*" not in out:
        out += recv_avail(sock, total=4.0, idle=0.5)
    results.check("1.9b TEST shows verified pass", out, "*")
    results.check_not("1.9c TEST no transfer error", out, "TRANSFER ERROR")
    # Send ESC to exit the test loop cleanly (like real hardware)
    send_ctrl(sock, 0x1B)  # ESC
    time.sleep(0.3)
    extra = recv_avail(sock, total=3.0, idle=0.5)
    vprint("TEST after ESC", extra)
    combined = out + extra
    # If not back at prompt, reset and re-enter SD CARD OS.
    # Wait for MCU idle timeout (~0.5s) so TEST_ECHO clears before re-entry.
    if "/>" not in combined:
        send_ctrl(sock, CTRL_R)
        time.sleep(1.0)
        recv_avail(sock, total=2.0, idle=0.5)
        out = send_line(sock, "8000R", wait=0.5, read_t=4.0)
        vprint("TEST recovery", out)

    # 1.10 TYPE HELLO.TXT
    out = send_line(sock, "TYPE HELLO.TXT", read_t=8.0)
    vprint("TYPE", out)
    results.check("1.10 TYPE HELLO.TXT", out, "HELLO WORLD")

    # 1.11 DUMP SMALL#060300
    out = send_line(sock, "DUMP SMALL#060300", read_t=8.0)
    vprint("DUMP", out)
    # The hex dump should show A9 (first byte of LDA #$CF)
    results.check("1.11 DUMP shows hex data", out, "A9")

    # 1.12 MOUNT
    out = send_line(sock, "MOUNT", read_t=4.0)
    vprint("MOUNT", out)
    # MOUNT should not produce an error
    results.check_not("1.12 MOUNT no error", out, "ERROR")

    # 1.13 BAS (no BASIC program loaded)
    out = send_line(sock, "BAS", read_t=4.0)
    vprint("BAS", out)
    # Might show LOMEM/HIMEM or "NO BASIC PROGRAM" -- either is valid
    has_lomem = "LOMEM" in out.upper()
    has_no_basic = "NO BASIC" in out.upper()
    if has_lomem or has_no_basic:
        results.passed.append("1.13 BAS command")
        print("  [PASS] 1.13 BAS command")
    else:
        results.failed.append(("1.13 BAS command", "Neither LOMEM nor NO BASIC found"))
        print("  [FAIL] 1.13 BAS command")

    # 1.14 MOUNT resets directory — navigate away first, then MOUNT, verify PWD
    send_line(sock, "CD HGR")
    out = send_line(sock, "MOUNT", read_t=4.0)
    vprint("MOUNT from HGR", out)
    results.check_not("1.14 MOUNT no error", out, "ERROR")
    out = send_line(sock, "PWD")
    vprint("PWD after MOUNT", out)
    results.check("1.14b MOUNT resets to root", out, "/")

    # 1.15 DIR format: long format shows display name, size, type, $addr
    out = send_line(sock, "DIR HGR", read_t=8.0)
    vprint("DIR HGR format", out)
    results.check("1.15 DIR shows BIN type", out, "BIN")
    results.check("1.15b DIR shows $addr", out, "$2000")
    results.check("1.15c DIR shows size", out, "8192")

    # 1.16 LS format: short format shows <DIR> prefix for directories
    out = send_line(sock, "LS", read_t=8.0)
    vprint("LS format", out)
    results.check("1.16 LS shows <DIR> prefix", out, "<DIR>")

    # 1.17 TYPE full file content (test 1.10 only checks first line)
    out = send_line(sock, "TYPE HELLO.TXT", read_t=8.0)
    vprint("TYPE full", out)
    results.check("1.17 TYPE shows second line", out, "THIS IS A TEST FILE")


def phase2_navigation(sock: socket.socket, results: TestResults) -> None:
    """Phase 2: Directory navigation."""
    print("\nPhase 2: Directory Navigation")

    # Make sure we're at root after MOUNT
    send_line(sock, "CD /", read_t=3.0)

    # 2.1 CD HGR
    out = send_line(sock, "CD HGR")
    vprint("CD HGR", out)
    results.check_not("2.1 CD HGR no error", out, "NOT FOUND")

    # 2.2 PWD after CD
    out = send_line(sock, "PWD")
    vprint("PWD", out)
    results.check("2.2 PWD shows /HGR", out, "/HGR")

    # 2.3 DIR in HGR
    out = send_line(sock, "DIR", read_t=8.0)
    vprint("DIR in HGR", out)
    results.check("2.3 DIR in HGR shows PIC", out, "PIC")

    # 2.4 CD .. (parent)
    out = send_line(sock, "CD ..")
    vprint("CD ..", out)
    results.check_not("2.4 CD .. no error", out, "NOT FOUND")

    # 2.5 PWD back at root
    out = send_line(sock, "PWD")
    vprint("PWD root", out)
    results.check("2.5 PWD back at root", out, "/")

    # 2.6 CD nested: TESTDIR/SUB1
    send_line(sock, "CD TESTDIR")
    out = send_line(sock, "CD SUB1")
    vprint("CD SUB1", out)
    out = send_line(sock, "PWD")
    vprint("PWD nested", out)
    results.check("2.6 Nested CD to /TESTDIR/SUB1", out, "/TESTDIR/SUB1")

    # 2.7 CD / (absolute root)
    out = send_line(sock, "CD /")
    out = send_line(sock, "PWD")
    vprint("PWD after CD /", out)
    results.check("2.7 CD / returns to root", out, "/")

    # 2.8 CD hgr (lowercase, fuzzy match)
    out = send_line(sock, "CD hgr")
    vprint("CD hgr fuzzy", out)
    results.check_not("2.8 CD hgr fuzzy no error", out, "NOT FOUND")
    out = send_line(sock, "PWD")
    results.check("2.8b fuzzy resolves to /HGR", out, "/HGR")
    send_line(sock, "CD /")  # return to root

    # 2.9 CD to non-existent directory
    out = send_line(sock, "CD NOSUCHDIR")
    vprint("CD NOSUCHDIR", out)
    results.check("2.9 CD NOSUCHDIR error", out, "PATH NOT FOUND")

    # 2.10 CD absolute path from subdirectory
    send_line(sock, "CD HGR")
    out = send_line(sock, "CD /TESTDIR")
    vprint("CD /TESTDIR from HGR", out)
    results.check_not("2.10 CD /TESTDIR no error", out, "NOT FOUND")
    out = send_line(sock, "PWD")
    results.check("2.10b PWD shows /TESTDIR", out, "/TESTDIR")
    send_line(sock, "CD /")  # return to root

    # 2.11 CD with trailing slash
    out = send_line(sock, "CD HGR/")
    vprint("CD HGR/", out)
    results.check_not("2.11 CD HGR/ no error", out, "NOT FOUND")
    out = send_line(sock, "PWD")
    results.check("2.11b CD HGR/ resolves", out, "/HGR")
    send_line(sock, "CD /")

    # 2.12 Multiple CD .. from deeply nested directory
    send_line(sock, "CD TESTDIR")
    send_line(sock, "CD SUB1")
    out = send_line(sock, "PWD")
    results.check("2.12 start at /TESTDIR/SUB1", out, "/TESTDIR/SUB1")
    send_line(sock, "CD ..")
    out = send_line(sock, "PWD")
    results.check("2.12b CD .. to /TESTDIR", out, "/TESTDIR")
    send_line(sock, "CD ..")
    out = send_line(sock, "PWD")
    results.check("2.12c CD .. to root", out, "/")


def phase3_file_read(sock: socket.socket, results: TestResults) -> None:
    """Phase 3: File read operations."""
    print("\nPhase 3: File Read Operations")

    # Ensure we're at root
    send_line(sock, "CD /")

    # 3.1 LOAD PIC (exact match, in HGR)
    send_line(sock, "CD HGR")
    out = send_line(sock, "LOAD PIC", read_t=15.0)
    vprint("LOAD PIC", out)
    results.check("3.1 LOAD PIC found", out, "FOUND")
    results.check("3.1b LOAD PIC filename", out, "PIC#062000")

    # 3.2 LOAD N00 (fuzzy prefix match)
    out = send_line(sock, "LOAD N00", read_t=15.0)
    vprint("LOAD N00", out)
    results.check("3.2 LOAD N00 fuzzy match", out, "FOUND")
    results.check("3.2b matched N000", out, "N000")

    # 3.3 LOAD non-existent
    out = send_line(sock, "LOAD NONEXIST", read_t=4.0)
    vprint("LOAD NONEXIST", out)
    results.check("3.3 LOAD NONEXIST error", out, "FILE NOT FOUND")

    # 3.4 DUMP TESTBIN — verify actual file content (bytes 0x00..0xFF)
    send_line(sock, "CD /")
    out = send_line(sock, "DUMP TESTBIN", wait=0.5, read_t=15.0)
    vprint("DUMP TESTBIN", out)
    results.check_not("3.4 DUMP TESTBIN no error", out, "?")
    results.check("3.4b DUMP shows start bytes", out, "00 01 02")
    results.check("3.4c DUMP shows 0x80 region", out, "80 81 82")

    # 3.4d READ TESTBIN to $0400 (load to memory, no content visible)
    # Drain any remaining DUMP output first
    recv_avail(sock, total=1.5, idle=0.5)
    out = send_line(sock, "READ TESTBIN 0400", read_t=8.0)
    vprint("READ TESTBIN", out)
    results.check_not("3.4d READ TESTBIN no error", out, "?")

    # 3.5 RUN SMALL (executes program that prints OK)
    out = send_line(sock, "RUN SMALL", wait=0.5, read_t=8.0)
    vprint("RUN SMALL", out)
    results.check("3.5 RUN SMALL found", out, "FOUND")
    # After RUN, the program prints OK and jumps to Woz Monitor
    # We need to wait for the OK output and the Woz prompt
    extra = recv_avail(sock, total=4.0)
    vprint("RUN SMALL extra", extra)
    combined = out + extra
    results.check("3.5b RUN SMALL prints OK", combined, "OK")

    # Re-enter SD CARD OS
    out = send_line(sock, "8000R", wait=0.5, read_t=4.0)
    vprint("re-enter SDCARD OS", out)

    # 3.6 LOAD with full tagged filename
    send_line(sock, "CD /")
    out = send_line(sock, "LOAD SMALL#060300", read_t=8.0)
    vprint("LOAD SMALL#060300", out)
    results.check("3.6 LOAD with tag found", out, "FOUND")
    results.check("3.6b LOAD tag filename", out, "SMALL#060300")

    # 3.7 READ in subdirectory (relative path resolution)
    send_line(sock, "CD HGR")
    out = send_line(sock, "READ PIC#062000 2000", read_t=15.0)
    vprint("READ PIC in HGR", out)
    results.check_not("3.7 READ in subdir no error", out, "?")
    send_line(sock, "CD /")


def phase4_file_write(sock: socket.socket, results: TestResults) -> None:
    """Phase 4: File write/modify operations."""
    print("\nPhase 4: File Write Operations")

    # Ensure we're at root
    send_line(sock, "CD /")

    # 4.1 MKDIR NEWDIR
    out = send_line(sock, "MKDIR NEWDIR")
    vprint("MKDIR NEWDIR", out)
    results.check_not("4.1 MKDIR NEWDIR no error", out, "ERROR")
    results.check_host_exists("4.1b NEWDIR on host", os.path.join(SDCARD_PATH, "NEWDIR"), True)

    # Verify via LS
    out = send_line(sock, "LS", read_t=8.0)
    results.check("4.1c NEWDIR in LS", out, "NEWDIR")

    # 4.2 MKDIR NEWDIR again (duplicate)
    out = send_line(sock, "MKDIR NEWDIR")
    vprint("MKDIR NEWDIR dup", out)
    results.check("4.2 MKDIR duplicate error", out, "ALREADY EXISTS")

    # 4.15 MKDIR nested path (should fail — no recursive create)
    out = send_line(sock, "MKDIR A/B")
    vprint("MKDIR A/B", out)
    results.check("4.15 MKDIR nested fails", out, "FAILED")

    # 4.3 RMDIR EMPTYDIR (success)
    out = send_line(sock, "RMDIR EMPTYDIR")
    vprint("RMDIR EMPTYDIR", out)
    results.check_not("4.3 RMDIR EMPTYDIR no error", out, "ERROR")
    results.check_host_exists("4.3b EMPTYDIR removed", os.path.join(SDCARD_PATH, "EMPTYDIR"), False)

    # 4.4 RMDIR NOTEMPTY (should fail)
    out = send_line(sock, "RMDIR NOTEMPTY")
    vprint("RMDIR NOTEMPTY", out)
    results.check("4.4 RMDIR NOTEMPTY error", out, "NOT EMPTY")

    # 4.5 RMDIR non-existent
    out = send_line(sock, "RMDIR XYZNODIR")
    vprint("RMDIR XYZNODIR", out)
    results.check("4.5 RMDIR non-existent error", out, "PATH NOT FOUND")

    # 4.6 MD / RD aliases
    out = send_line(sock, "MD TEMPDIR")
    vprint("MD TEMPDIR", out)
    results.check_host_exists("4.6 MD TEMPDIR created", os.path.join(SDCARD_PATH, "TEMPDIR"), True)
    out = send_line(sock, "RD TEMPDIR")
    vprint("RD TEMPDIR", out)
    results.check_host_exists("4.6b RD TEMPDIR removed", os.path.join(SDCARD_PATH, "TEMPDIR"), False)

    # 4.7 DEL DELME.TXT
    out = send_line(sock, "DEL DELME.TXT")
    vprint("DEL DELME.TXT", out)
    results.check_host_exists("4.7 DEL DELME.TXT", os.path.join(SDCARD_PATH, "DELME.TXT"), False)

    # 4.8 RM DELME2.TXT (alias)
    out = send_line(sock, "RM DELME2.TXT")
    vprint("RM DELME2.TXT", out)
    results.check_host_exists("4.8 RM DELME2.TXT", os.path.join(SDCARD_PATH, "DELME2.TXT"), False)

    # 4.9 DEL non-existent
    out = send_line(sock, "DEL NOSUCHFILE")
    vprint("DEL NOSUCHFILE", out)
    results.check("4.9 DEL non-existent error", out, "FILE NOT FOUND")

    # 4.10 DEL on directory
    out = send_line(sock, "DEL HGR")
    vprint("DEL HGR", out)
    results.check("4.10 DEL directory error", out, "IS A DIRECTORY")

    # 4.11 WRITE via Woz Monitor
    # Exit to Woz Monitor, write data, come back
    out = enter_woz_monitor(sock)
    vprint("EXIT", out)

    # Poke "HELLO" (48 45 4C 4C 4F) at $0400
    out = send_line(sock, "0400: 48 45 4C 4C 4F", wait=0.3, read_t=3.0)
    vprint("poke data", out)

    # Re-enter SD CARD OS
    out = send_line(sock, "8000R", wait=0.5, read_t=4.0)
    vprint("re-enter", out)

    out = send_line(sock, "WRITE WTEST 0400 0404", read_t=6.0)
    vprint("WRITE WTEST", out)
    results.check_not("4.11 WRITE WTEST no error", out, "?")

    # Verify on host
    wtest_path = os.path.join(SDCARD_PATH, "WTEST")
    results.check_host_exists("4.11b WTEST exists", wtest_path, True)
    if os.path.exists(wtest_path):
        with open(wtest_path, "rb") as f:
            data = f.read()
        if data == b"HELLO":
            results.passed.append("4.11c WTEST content correct")
            print("  [PASS] 4.11c WTEST content correct")
        else:
            results.failed.append(("4.11c WTEST content", f"Expected b'HELLO', got {data!r}"))
            print(f"  [FAIL] 4.11c WTEST content -- got {data!r}")

    # 4.14 WRITE overwrite — memory at $0400 still has "HELLO", overwrite with 3 bytes
    out = send_line(sock, "WRITE WTEST 0400 0402", read_t=6.0)
    vprint("WRITE WTEST overwrite", out)
    results.check_not("4.14 WRITE overwrite no error", out, "?")
    if os.path.exists(wtest_path):
        with open(wtest_path, "rb") as f:
            data = f.read()
        if data == b"HEL":
            results.passed.append("4.14b overwrite content correct")
            print("  [PASS] 4.14b overwrite content correct")
        else:
            results.failed.append(("4.14b overwrite content", f"Expected b'HEL', got {data!r}"))
            print(f"  [FAIL] 4.14b overwrite content -- got {data!r}")

    # 4.12 SAVE STEST 0400 0404 (binary with tag)
    out = send_line(sock, "SAVE STEST 0400 0404", read_t=6.0)
    vprint("SAVE STEST", out)
    results.check_not("4.12 SAVE STEST no error", out, "?")
    results.check_host_exists("4.12b STEST#060400 exists",
                               os.path.join(SDCARD_PATH, "STEST#060400"), True)

    # 4.13 Cleanup created files via SD CARD OS
    send_line(sock, "DEL WTEST", read_t=3.0)
    send_line(sock, "DEL STEST#060400", read_t=3.0)
    send_line(sock, "RMDIR NEWDIR", read_t=3.0)


def phase5_exit_reentry(sock: socket.socket, results: TestResults) -> None:
    """Phase 5: EXIT and re-entry."""
    print("\nPhase 5: EXIT and Re-entry")

    # 5.1 EXIT
    out = send_line(sock, "EXIT", wait=0.5, read_t=3.0)
    # Woz Monitor prompt (\) may arrive with a small delay
    out += recv_avail(sock, total=2.0, idle=0.3)
    vprint("EXIT", out)
    results.check("5.1 EXIT prints BYE", out, "BYE")

    # 5.2 Re-enter SD CARD OS
    out = send_line(sock, "8000R", wait=0.5, read_t=4.0)
    vprint("8000R", out)
    results.check("5.2 Re-enter SD CARD OS", out, "SD CARD OS")


def phase6_errors(sock: socket.socket, results: TestResults) -> None:
    """Phase 6: Error cases."""
    print("\nPhase 6: Error Cases")

    # 6.1 Empty command
    out = send_line(sock, "", read_t=3.0)
    vprint("empty cmd", out)
    results.check_not("6.1 Empty command no crash", out, "ERROR")

    # 6.2 Unknown command — ROM v1.3 echoes the input with ?? appended
    out = send_line(sock, "XYZZY", read_t=4.0)
    vprint("XYZZY", out)
    results.check("6.2 Unknown command rejected", out, "??")

    # 6.3 LOAD without argument
    out = send_line(sock, "LOAD", read_t=4.0)
    vprint("LOAD no arg", out)
    results.check("6.3 LOAD missing filename", out, "MISSING")

    # 6.4 DEL without argument
    out = send_line(sock, "DEL", read_t=4.0)
    vprint("DEL no arg", out)
    results.check("6.4 DEL missing filename", out, "MISSING")

    # 6.5 Path traversal CD ../../etc
    out = send_line(sock, "CD ../../etc")
    vprint("CD traversal", out)
    results.check("6.5 Path traversal blocked", out, "PATH NOT FOUND")

    # 6.6 DIR non-existent path
    out = send_line(sock, "DIR NOSUCHPATH", read_t=4.0)
    vprint("DIR NOSUCH", out)
    results.check("6.6 DIR non-existent path", out, "PATH NOT FOUND")

    # 6.7 RMDIR on a file
    out = send_line(sock, "RMDIR HELLO.TXT")
    vprint("RMDIR file", out)
    results.check("6.7 RMDIR on file", out, "NOT A DIRECTORY")

    # 6.8 Path traversal via TYPE (sandbox security)
    out = send_line(sock, "TYPE ../../etc/passwd", read_t=4.0)
    vprint("TYPE traversal", out)
    results.check_not("6.8 TYPE traversal no passwd", out, "root:")
    # resolveHostPath clamps to sdcard root (a directory) -> error
    has_error = ("?" in out or "IS A DIRECTORY" in out.upper()
                 or "FILE NOT FOUND" in out.upper() or "ERROR" in out.upper())
    if has_error:
        results.passed.append("6.8b TYPE traversal error response")
        print("  [PASS] 6.8b TYPE traversal error response")
    else:
        results.failed.append(("6.8b TYPE traversal", "No error response"))
        print("  [FAIL] 6.8b TYPE traversal -- no error response")

    # 6.9 HELP for unknown command — ROM validates command name before reading file
    out = send_line(sock, "HELP NOSUCHCMD", read_t=6.0)
    vprint("HELP NOSUCHCMD", out)
    results.check("6.9 HELP unknown cmd error", out, "UNKNOWN COMMAND")

    # 6.10 READ nonexistent file (distinct from LOAD nonexistent in 3.3)
    out = send_line(sock, "READ NOSUCHFILE 0400", read_t=4.0)
    vprint("READ NOSUCH", out)
    results.check("6.10 READ nonexistent", out, "FILE NOT FOUND")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    global VERBOSE

    parser = argparse.ArgumentParser(description="Test SD CARD OS via Terminal Card telnet")
    parser.add_argument("--setup", action="store_true", help="Prepare test data before testing")
    parser.add_argument("--cleanup", action="store_true", help="Clean up test artifacts after testing")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show raw output")
    args = parser.parse_args()

    VERBOSE = args.verbose

    if args.setup:
        prepare_test_data()

    # Verify test data exists
    if not os.path.exists(os.path.join(SDCARD_PATH, "HELLO.TXT")):
        print("ERROR: Test data not found. Run with --setup first.", file=sys.stderr)
        return 2

    print("=" * 50)
    print("SD CARD OS Test Suite")
    print(f"Target: {HOST}:{PORT}")
    print("=" * 50)

    results = TestResults()

    try:
        sock = socket.create_connection((HOST, PORT), timeout=10)
    except OSError as e:
        print(f"\nERROR: Cannot connect to {HOST}:{PORT} ({e})")
        print("Make sure POM1 is running with Terminal Card + microSD enabled.")
        return 2

    try:
        # Phase 0: Connectivity
        if not phase0_connectivity(sock, results):
            print("\nABORT: Cannot enter SD CARD OS. Check emulator state.")
            return results.report()

        # Phase 1: Read-only commands
        phase1_readonly(sock, results)

        # Phase 2: Directory navigation
        phase2_navigation(sock, results)

        # Phase 3: File read operations
        phase3_file_read(sock, results)

        # Phase 4: File write operations
        phase4_file_write(sock, results)

        # Phase 5: EXIT and re-entry
        phase5_exit_reentry(sock, results)

        # Phase 6: Error cases
        phase6_errors(sock, results)

    except (BrokenPipeError, ConnectionResetError) as e:
        print(f"\nConnection lost: {e}")
    finally:
        try:
            sock.close()
        except Exception:
            pass

    rc = results.report()

    if args.cleanup:
        cleanup_test_data()

    return rc


if __name__ == "__main__":
    sys.exit(main())
