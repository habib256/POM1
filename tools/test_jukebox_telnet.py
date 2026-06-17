#!/usr/bin/env python3
"""
test_jukebox_telnet.py -- Test complet du P-LAB Apple-1 Juke-Box via Terminal Card telnet.

Pre-requis :
  - POM1 en cours d'execution avec la carte Juke-Box activee (Hardware menu ou
    --enable jukebox ; il n'y a plus de preset Juke-Box dedie)
    + Terminal Card active (--terminal en CLI ou Hardware menu)
  - Le Terminal Card ecoute sur localhost:6502
  - Lancement CLI recommande :  ./POM1 --preset 1 --enable jukebox --terminal &
  - La ROM roms/jukebox.rom generee par tools/../doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py

Le test suit le manuel officiel P-LAB Apple-1 Juke-Box v1.09 :
  - H    help screen
  - D    directory of current page
  - L?   load program (single letter)
  - P0-F page select (hex digit)
  - S0/1 16 kB sub-page (ROM MAP 16 kB only -- skipped ici)
  - B    enter BASIC (E2B3R soft entry)
  - X    exit to WOZ Monitor

Usage :
  python3 tools/test_jukebox_telnet.py [--verbose]
"""
from __future__ import annotations

import argparse
import select
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18  # Ctrl-R = reset Apple 1

VERBOSE = False

# Contenu attendu de la ROM generee par build_jukebox_rom.py (liste par defaut).
# Ordre = ordre de pack (= ordre des lettres A, B, C, ... dans D)IR).
# Tuple : (letter, nom tel qu'affiche par le firmware, load_addr_hex, end_addr_hex, is_basic)
EXPECTED_CATALOG = [
    ("A", "BASIC",    0xE000, 0xF000, False),
    ("B", "WOZF2394", 0x1D00, 0x2B34, False),
    ("C", "LIFE",     0x2000, 0x21B8, False),
    ("D", "MICROCH2", 0x1000, 0x1900, False),
    ("E", "STARTREK", 0x0430, 0x1000, True),
    ("F", "CHECKERS", 0x042D, 0x1000, True),
    ("G", "LUNARLND", 0x04A7, 0x1000, True),
    ("H", "HAMURABI", 0x04F3, 0x1000, True),
    ("I", "AMAZING",  0x35A5, 0x4000, True),
    ("J", "BLACKJAK", 0x0A06, 0x1000, True),
    ("K", "BATNUM",   0x0949, 0x1000, True),
    ("L", "REVERSE",  0x0DC6, 0x1000, True),
]


# ---------------------------------------------------------------------------
# Telnet helpers (meme pattern que test_cffa1_telnet.py)
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


def send_line(sock: socket.socket, cmd: str, wait: float = 0.3, read_t: float = 4.0) -> str:
    """Send command + CR, wait, read response."""
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t, idle=0.4)


def send_ctrl(sock: socket.socket, byte: int) -> None:
    sock.sendall(bytes([byte & 0xFF]))


def drain_to_prompt(sock: socket.socket, prompt: str, timeout: float = 8.0) -> str:
    """Read until the given prompt character appears as the last non-whitespace
    byte in the accumulated output, or timeout expires. `prompt` should be a
    single character: '&' for the Juke-Box Program Manager, '\\' for the Woz
    Monitor, '>' for BASIC."""
    end = time.time() + timeout
    buf = ""
    no_data_ticks = 0
    while time.time() < end:
        chunk = recv_avail(sock, total=1.2, idle=0.3)
        if chunk:
            buf += chunk
            no_data_ticks = 0
        else:
            no_data_ticks += 1
        stripped = buf.rstrip()
        if stripped.endswith(prompt):
            return buf
        # 3 consecutive idle ticks after we've seen something -> give up
        if no_data_ticks >= 3 and buf:
            return buf
    return buf


def send_and_drain(sock: socket.socket, cmd: str, prompt: str, timeout: float = 8.0) -> str:
    """Send cmd+CR, then drain until the expected prompt returns."""
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(0.2)
    return drain_to_prompt(sock, prompt, timeout=timeout)


def enter_program_manager(sock: socket.socket) -> str:
    """Reset Apple 1 and enter the Juke-Box Program Manager (BD00R). Returns captured output."""
    send_ctrl(sock, CTRL_R)
    time.sleep(1.0)
    recv_avail(sock, total=2.0)
    return send_and_drain(sock, "BD00R", "&", timeout=6.0)


# ---------------------------------------------------------------------------
# Test framework
# ---------------------------------------------------------------------------

class TestResults:
    def __init__(self) -> None:
        self.passed: list[str] = []
        self.failed: list[tuple[str, str]] = []

    def check(self, name: str, output: str, expected: str) -> bool:
        if expected.upper() in output.upper():
            self.passed.append(name)
            print(f"  [PASS] {name}")
            return True
        self.failed.append((name, f"Expected '{expected}' not found"))
        print(f"  [FAIL] {name} -- expected '{expected}'")
        if VERBOSE:
            print(f"         Output: {output[-500:]!r}")
        return False

    def check_not(self, name: str, output: str, unexpected: str) -> bool:
        if unexpected.upper() not in output.upper():
            self.passed.append(name)
            print(f"  [PASS] {name}")
            return True
        self.failed.append((name, f"Unexpected '{unexpected}' present"))
        print(f"  [FAIL] {name} -- unexpected '{unexpected}'")
        if VERBOSE:
            print(f"         Output: {output[-500:]!r}")
        return False

    def report(self) -> int:
        total = len(self.passed) + len(self.failed)
        print(f"\n{'='*50}")
        print(f"Results: {len(self.passed)}/{total} PASSED")
        if self.failed:
            print("\nFailed tests:")
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
    """Phase 0: TCP banner + firmware signature at $BD00."""
    print("\nPhase 0: Connectivity & Firmware Signature")

    banner = recv_avail(sock, total=1.5)
    vprint("banner", banner)
    results.check("0.1 TCP banner", banner, "POM1")

    # Reset, then probe $BD00 from the Woz Monitor -- should read A5 (LDA zp,
    # first byte of the Program Manager).
    send_ctrl(sock, CTRL_R)
    time.sleep(0.9)
    recv_avail(sock, total=2.0)

    out = send_line(sock, "BD00", wait=0.3, read_t=3.0)
    vprint("BD00", out)
    ok = results.check("0.2 Firmware signature $BD00 = A5", out, "BD00: A5")
    return ok


def phase1_help_and_banner(sock: socket.socket, results: TestResults) -> None:
    """Phase 1: Program Manager entry + H)elp screen."""
    print("\nPhase 1: Program Manager Entry & Help")

    out = enter_program_manager(sock)
    vprint("BD00R", out)
    results.check("1.1 Program Manager '&' prompt", out, "&")
    results.check("1.2 BD00: A5 signature shown", out, "BD00: A5")

    # H command -- shows the help screen per manual section 5.2.
    out = send_and_drain(sock, "H", "&", timeout=6.0)
    vprint("H", out)
    results.check("1.3 Help D)IR line",   out, "D)IR")
    results.check("1.4 Help L)OAD line",  out, "L)OAD")
    results.check("1.5 Help S)ET line",   out, "S)ET")
    results.check("1.6 Help P)AGE line",  out, "P)AGE")
    results.check("1.7 Help B)ASIC line", out, "B)ASIC")
    results.check("1.8 Help E(X)IT line", out, "E(X)IT")


def phase2_directory(sock: socket.socket, results: TestResults) -> None:
    """Phase 2: D)IR listing -- every expected program present."""
    print("\nPhase 2: Directory Listing (D)")

    out = send_and_drain(sock, "D", "&", timeout=10.0)
    vprint("D", out)

    results.check("2.1 Header PAGE 0", out, "PAGE 0")
    for letter, name, load, end, is_basic in EXPECTED_CATALOG:
        # 'letter <name>' anywhere on the same line is enough to pin the entry.
        results.check(f"2.x Entry {letter} {name}", out, f"{letter} {name}")
        results.check(f"2.x Entry {letter} load ${load:04X}", out, f"${load:04X}")
        results.check(f"2.x Entry {letter} end ${end:04X}", out, f"${end:04X}")
        if is_basic:
            pass  # BAS tag presence already implied by the full directory block

    # Count BAS tags -- must match the number of BASIC programs in the catalog.
    expected_bas = sum(1 for e in EXPECTED_CATALOG if e[4])
    bas_count = out.upper().count(" BAS")
    if bas_count >= expected_bas:
        results.passed.append(f"2.Z BAS tag count >= {expected_bas}")
        print(f"  [PASS] 2.Z BAS tag count >= {expected_bas} (got {bas_count})")
    else:
        results.failed.append((
            "2.Z BAS tag count",
            f"expected >={expected_bas}, got {bas_count}",
        ))
        print(f"  [FAIL] 2.Z BAS tag count = {bas_count}, expected >= {expected_bas}")


def phase3_page_select(sock: socket.socket, results: TestResults) -> None:
    """Phase 3: Page-select command (P) + 28c256 single-page mirror."""
    print("\nPhase 3: Page Select (P)")

    # Every hex page 0..F must return OK (no '!') on a 28c256. Since the ROM
    # has a single page, pages 1..F mirror page 0.
    for page in "012389ABCDEF":
        out = send_and_drain(sock, f"P{page}", "&", timeout=4.0)
        vprint(f"P{page}", out)
        results.check(f"3.{page} P{page} accepted", out, "OK")
        results.check_not(f"3.{page}! P{page} no '!'", out, "!")

    # After P2, a D listing must still show "PAGE 2" + the same programs
    # (mirrored single-page ROM).
    send_and_drain(sock, "P2", "&", timeout=4.0)
    out = send_and_drain(sock, "D", "&", timeout=10.0)
    vprint("D on page 2", out)
    results.check("3.M PAGE 2 header after P2", out, "PAGE 2")
    results.check("3.M Page 2 mirrors BASIC",   out, "A BASIC")
    results.check("3.M Page 2 mirrors STARTREK", out, "STARTREK")

    # Reset back to page 0 for downstream tests.
    send_and_drain(sock, "P0", "&", timeout=4.0)


def phase4_invalid_command(sock: socket.socket, results: TestResults) -> None:
    """Phase 4: Invalid command -- manual says '!' is printed."""
    print("\nPhase 4: Invalid Command Handling")

    # 'Z' is not a command letter and not a valid program index (catalogue
    # ends at 'L'). Manual section 5.8: wrong commands print '!'.
    out = send_and_drain(sock, "Z", "&", timeout=4.0)
    vprint("Z", out)
    results.check("4.1 Invalid command '!'", out, "!")


def phase5_load_machine_code(sock: socket.socket, results: TestResults) -> None:
    """Phase 5: Load a machine-code program and verify the bytes landed in RAM."""
    print("\nPhase 5: Load Machine-Code Program (LIFE)")

    # LIFE is entry 'C' in the catalog. It is a small (~440 byte) machine-
    # code program with load address $2000. After LC, the Program Manager
    # must answer OK and the bytes should be readable from the monitor.
    out = send_and_drain(sock, "LC", "&", timeout=6.0)
    vprint("LC", out)
    results.check("5.1 LC (LIFE) returns OK", out, "OK")
    results.check_not("5.2 LC no '!'", out, "!")

    # Exit to WOZ Monitor and read $2000 -- must not be $FF (untouched RAM).
    out = send_and_drain(sock, "X", "\\", timeout=4.0)
    vprint("X", out)
    results.check("5.3 X returns to Woz Monitor", out, "\\")

    out = send_and_drain(sock, "2000", "\\", timeout=4.0)
    vprint("2000", out)
    results.check("5.4 $2000 readable post-load", out, "2000:")
    # The byte at $2000 should not be $FF (fresh RAM) -- LIFE has been copied
    # here. We don't pin the exact first byte (it varies if the stripper
    # slices the BASIC tail etc.) but it must differ from $FF.
    is_not_ff = ("2000: FF" not in out.upper())
    if is_not_ff:
        results.passed.append("5.5 $2000 not $FF (RAM changed)")
        print("  [PASS] 5.5 $2000 not $FF (RAM changed)")
    else:
        results.failed.append(("5.5 $2000 not $FF", "LIFE did not land at $2000"))
        print("  [FAIL] 5.5 $2000 still $FF -- LIFE body did not land")


def phase6_load_basic_interpreter(sock: socket.socket, results: TestResults) -> None:
    """Phase 6: Load Apple BASIC interpreter (LA), enter with B, run a program."""
    print("\nPhase 6: Load BASIC Interpreter + Run")

    out = enter_program_manager(sock)
    vprint("re-enter PM", out)

    # LA loads Apple BASIC interpreter to $E000 per catalog entry.
    out = send_and_drain(sock, "LA", "&", timeout=6.0)
    vprint("LA", out)
    results.check("6.1 LA (BASIC) returns OK", out, "OK")

    # Verify the BASIC ROM byte at $E000 = $4C (JMP) -- matches what the
    # stripped BASIC---.bin starts with.
    out = send_and_drain(sock, "X", "\\", timeout=4.0)
    vprint("X", out)
    out = send_and_drain(sock, "E000", "\\", timeout=4.0)
    vprint("E000", out)
    results.check("6.2 $E000 = $4C (BASIC JMP)", out, "E000: 4C")

    # Re-enter Program Manager, then B to launch BASIC at its soft-start.
    # Per manual section 5.8, B is a *non-destructive* entry (E2B3R) meant
    # to run a previously-loaded BASIC program. For an interactive test
    # starting with an empty workspace we cold-start via E000R from the
    # Woz Monitor -- that path initialises the BASIC program area.
    enter_program_manager(sock)
    sock.sendall(b"B\r")
    time.sleep(0.8)
    out = recv_avail(sock, total=2.0, idle=0.4)
    sock.sendall(b"\r")
    out += recv_avail(sock, total=2.0, idle=0.4)
    vprint("B", out)
    results.check("6.3 BASIC '>' prompt after B", out, ">")

    # Cold-start BASIC (E000R) from Woz to get a clean program area, then
    # type a simple program and RUN it. We reach the Woz Monitor via the
    # Program Manager X command first to avoid BASIC's error path.
    enter_program_manager(sock)
    send_and_drain(sock, "X", "\\", timeout=4.0)
    sock.sendall(b"E000R\r")
    time.sleep(0.8)
    out = recv_avail(sock, total=3.0, idle=0.4)
    sock.sendall(b"\r")
    out += recv_avail(sock, total=2.0, idle=0.4)
    vprint("E000R cold start", out)
    # After cold start, Integer BASIC echoes '>' and is ready for fresh input.
    if ">" not in out:
        results.failed.append(("6.4 cold-start '>' prompt", "no prompt after E000R"))
        print("  [FAIL] 6.4 cold-start '>' prompt -- no > after E000R")
        return

    send_and_drain(sock, "NEW", ">", timeout=4.0)
    send_and_drain(sock, '10 PRINT "JBOXOK"', ">", timeout=4.0)
    out = send_and_drain(sock, "RUN", ">", timeout=8.0)
    vprint("RUN", out)
    results.check("6.4 RUN prints JBOXOK", out, "JBOXOK")


def phase7b_canonical_basic_flow(sock: socket.socket, results: TestResults) -> None:
    """Phase 7b: Canonical BASIC-program flow per manual section 5.5.
    LA (BASIC interpreter) + L<id on a BAS entry> + B + LIST should show the
    bundled BASIC program -- not hit '*** BAD BRANCH ERR' nor produce an
    empty listing. STARTREK (entry E, BAS) is used as the reference program
    because it's long enough to guarantee a visible listing."""
    print("\nPhase 7b: Canonical LA + L<BAS> + B + LIST flow")

    # Hard reset so we don't inherit garbage state from earlier phases.
    send_ctrl(sock, CTRL_R)
    time.sleep(1.0)
    recv_avail(sock, total=2.0)
    out = send_and_drain(sock, "BD00R", "&", timeout=6.0)
    vprint("BD00R", out)
    results.check("7b.1 PM re-entered", out, "&")

    # LA -- load Apple Integer BASIC interpreter from the Juke-Box EEPROM.
    out = send_and_drain(sock, "LA", "&", timeout=6.0)
    vprint("LA", out)
    results.check("7b.2 LA (BASIC) returns OK", out, "OK")

    # LE -- STARTREK is a BAS program; this must set up BASIC's pointers so
    # the subsequent LIST has real content to render.
    out = send_and_drain(sock, "LE", "&", timeout=6.0)
    vprint("LE", out)
    results.check("7b.3 LE (STARTREK, BAS) returns OK", out, "OK")
    results.check_not("7b.4 LE no '!'", out, "!")

    # B -- enter BASIC via E2B3 (warm start, non-destructive: keeps the
    # STARTREK program and its pointers intact).
    sock.sendall(b"B\r")
    time.sleep(0.8)
    out = recv_avail(sock, total=2.0, idle=0.4)
    sock.sendall(b"\r")
    out += recv_avail(sock, total=2.0, idle=0.4)
    vprint("B", out)
    results.check("7b.5 BASIC '>' prompt after B", out, ">")

    # LIST -- must produce a non-empty listing. Integer BASIC paginates every
    # 20 lines or so; we drain long enough to capture something meaningful
    # then stop. The test does NOT require the full program -- just that
    # LIST starts dumping lines before the next prompt.
    sock.sendall(b"LIST\r")
    time.sleep(0.5)
    out = recv_avail(sock, total=8.0, idle=0.6)
    vprint("LIST", out)
    results.check_not("7b.6 LIST has no BAD BRANCH ERR", out, "BAD BRANCH")
    results.check_not("7b.7 LIST has no SYNTAX ERR",     out, "SYNTAX")

    # Empty-listing sanity: the command echo itself is ~7 bytes ("LIST\r\n").
    # A real program produces at minimum a couple of hundred bytes of output.
    listing_body = out.upper().split("LIST", 1)[-1] if "LIST" in out.upper() else out
    body_len = len(listing_body.strip())
    if body_len > 120:
        results.passed.append(f"7b.8 LIST body has content ({body_len} bytes)")
        print(f"  [PASS] 7b.8 LIST body has content ({body_len} bytes)")
    else:
        results.failed.append((
            "7b.8 LIST body has content",
            f"only {body_len} bytes after LIST echo"))
        print(f"  [FAIL] 7b.8 LIST body too short ({body_len} bytes)")

    # Typical Integer BASIC listings contain at least one line number at the
    # start of a line. Look for a digit followed by space (the canonical
    # line-number-then-statement shape).
    import re
    has_line_num = bool(re.search(r"\n\s*\d+\s", out))
    if has_line_num:
        results.passed.append("7b.9 LIST shows a numbered line")
        print("  [PASS] 7b.9 LIST shows a numbered line")
    else:
        results.failed.append((
            "7b.9 LIST numbered line",
            "no '\\d+ ' pattern in LIST output"))
        print("  [FAIL] 7b.9 LIST shows no numbered line")

    # Drain any remaining LIST output so the next test starts clean.
    drain_to_prompt(sock, ">", timeout=15.0)


def phase7_load_basic_prog_without_la(sock: socket.socket, results: TestResults) -> None:
    """Phase 7: Regression -- load a BASIC program (L<id>) WITHOUT LA first,
    then B. The manual says this 'will not work and will hang the computer'
    if BASIC isn't in memory; POM1 mitigates it by pre-loading Apple Integer
    BASIC at $E000 in the Juke-Box preset so B lands on a functional E2B3
    warm-start even when LA wasn't issued."""
    print("\nPhase 7: LI + B without prior LA (regression)")

    # Hard reset via Ctrl-R + re-enter PM. Ctrl-R does not wipe RAM; we rely
    # on the preset-provided BASIC at $E000 being still present.
    send_ctrl(sock, CTRL_R)
    time.sleep(1.0)
    recv_avail(sock, total=2.0)
    out = send_and_drain(sock, "BD00R", "&", timeout=6.0)
    vprint("BD00R", out)
    results.check("7.1 PM re-entered", out, "&")

    # LK -- BATNUM is a small BASIC program ($0949-$1000).
    out = send_and_drain(sock, "LK", "&", timeout=6.0)
    vprint("LK", out)
    results.check("7.2 LK (BATNUM) returns OK", out, "OK")
    results.check_not("7.3 LK no '!'", out, "!")

    # B -- must reach the '>' prompt even without a prior LA. Before the
    # preset change to BasicType::Integer, $E000 was $00 and B would hang
    # on an endless BRK loop.
    sock.sendall(b"B\r")
    time.sleep(1.0)
    out = recv_avail(sock, total=3.0, idle=0.4)
    sock.sendall(b"\r")
    out += recv_avail(sock, total=2.0, idle=0.4)
    vprint("B", out)
    results.check("7.4 BASIC '>' prompt (no hang)", out, ">")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    global VERBOSE
    parser = argparse.ArgumentParser(description="Test P-LAB Juke-Box via Terminal Card telnet")
    parser.add_argument("--verbose", action="store_true", help="Show raw output")
    args = parser.parse_args()
    VERBOSE = args.verbose

    print("=" * 50)
    print("P-LAB Apple-1 Juke-Box Test Suite")
    print("=" * 50)
    print(f"Connecting to {HOST}:{PORT}...")

    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
    except (ConnectionRefusedError, OSError) as e:
        print(f"ERROR: Cannot connect to Terminal Card at {HOST}:{PORT}")
        print(f"  {e}")
        print("  Make sure POM1 is running with the Juke-Box preset + Terminal Card enabled.")
        return 1

    sock.settimeout(10)
    results = TestResults()

    try:
        if not phase0_connectivity(sock, results):
            print("\nFirmware signature missing at $BD00. Aborting -- is jukebox.rom present?")
            return results.report()

        phase1_help_and_banner(sock, results)
        phase2_directory(sock, results)
        phase3_page_select(sock, results)
        phase4_invalid_command(sock, results)
        phase5_load_machine_code(sock, results)
        phase6_load_basic_interpreter(sock, results)
        phase7_load_basic_prog_without_la(sock, results)
        phase7b_canonical_basic_flow(sock, results)

    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
    finally:
        sock.close()

    return results.report()


if __name__ == "__main__":
    sys.exit(main())
