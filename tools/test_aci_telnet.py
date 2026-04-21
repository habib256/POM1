#!/usr/bin/env python3
"""
test_aci_telnet.py -- End-to-end ACI load + save round-trip through the
Terminal Card (telnet).

Unlike tests/aci_tape_loading_test.cpp / tests/aci_tape_saving_test.cpp
which drive the M6502 directly against a headless Memory, this script
exercises the *real* POM1 binary the user runs — the Apple 1
keyboard/display, the Terminal Card TCP server, EmulationController's
mutex stack, the snapshot publisher, GLFW's main loop. If one of those
layers breaks the tape path, the headless tests stay green and this
one goes red.

Scenarios:
  A. LOAD   — preload cassettes/APPLE50TH.ogg via --tape, drive the ACI ROM
              in READ mode (`C100R` then `0300.037FR`), Wozmon-peek $0300
              and assert the APPLE50TH demo signature `A9 FF 48`
              (LDA #$FF / PHA).
  B. SAVE   — plant a known pattern at $0300-$033F via Wozmon poke, drive
              the ACI ROM in WRITE mode (`0300.033FW`), SIGTERM the
              emulator so --save-tape flushes the capture to disk.
  C. ROUND  — preload the `.aci` file B just produced, drive READ on a
              FRESH pom1 instance, Wozmon-peek $0300-$033F, assert the
              bytes round-trip byte-for-byte.

Requirements:
  - POM1 built in build/
  - roms/ACI.rom + roms/basic.rom present (both included in the repo)
  - cassettes/APPLE50TH.ogg present
  - No other process holding TCP port 6502

The script launches and kills POM1 itself — no manual setup.

Usage:
  python3 tools/test_aci_telnet.py [--verbose] [--keep-tape]
"""
from __future__ import annotations

import argparse
import os
import re
import select
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18  # Apple 1 hard reset via Terminal Card
ACI_PRESET = 1  # "Apple-1 with ACI & Integer BASIC (October 1976)"

# Test pattern planted in phase B/C. 64 bytes at $0300-$033F. Avoids long
# constant runs so any framing bug corrupts visible nibbles first.
TEST_FROM = 0x0300
TEST_TO   = 0x033F
TEST_LEN  = TEST_TO - TEST_FROM + 1
TEST_PATTERN = bytes(((0xA5 ^ (i * 37)) & 0xFF) for i in range(TEST_LEN))

# APPLE50TH demo signature at $0280 (LDA #$FF / PHA) in cassettes/APPLE50TH.ogg.
APPLE50TH_SIG = bytes([0xA9, 0xFF, 0x48])
APPLE50TH_FROM = 0x0280
APPLE50TH_TO   = 0x0FFF

VERBOSE = False
passed = 0
failed = 0


# ---------------------------------------------------------------------------
# POM1 process helpers
# ---------------------------------------------------------------------------

def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def pom1_bin() -> Path:
    p = repo_root() / "build" / "POM1"
    if not p.exists():
        sys.exit(f"ERROR: {p} not found — build POM1 first (cd build && make POM1)")
    return p


def launch_pom1(extra_args: list[str], log_path: Path) -> subprocess.Popen:
    """Launch POM1 in the background with --preset 1 --terminal --cpu-max.
    Returns the Popen once the Terminal Card is listening on PORT (verified by
    actually opening a TCP connection — the log message order is not
    reliable enough on slower boxes). --cpu-max is critical: at the default
    1× speed a 30-second tape takes ~30 s of wallclock to load; at MAX it
    drops to ~1 s."""
    cmd = [str(pom1_bin()), "--preset", str(ACI_PRESET), "--terminal",
           "--cpu-max", *extra_args]
    if VERBOSE:
        print(f"  [launch] {' '.join(cmd)}")
    log = open(log_path, "w")
    proc = subprocess.Popen(cmd, cwd=str(repo_root()), stdout=log, stderr=log,
                            start_new_session=True)
    # Wait for the Terminal Card to accept connections (up to 8 s).
    deadline = time.time() + 8.0
    while time.time() < deadline:
        if proc.poll() is not None:
            sys.exit(f"ERROR: POM1 exited early (code {proc.returncode}); see {log_path}")
        try:
            with socket.create_connection((HOST, PORT), timeout=0.3):
                time.sleep(0.5)  # let the ACI preset finish plugging + auto-load
                return proc
        except OSError:
            time.sleep(0.15)
    proc.kill()
    sys.exit(f"ERROR: Terminal Card never came up on {HOST}:{PORT}; see {log_path}")


def shutdown_pom1(proc: subprocess.Popen, log_path: Path, *, sig=signal.SIGTERM,
                  wait_save_s: float = 3.0) -> None:
    """SIGTERM (not SIGKILL) so --save-tape can flush via the destructor."""
    if proc.poll() is not None:
        return
    proc.send_signal(sig)
    try:
        proc.wait(timeout=wait_save_s)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    if VERBOSE:
        print(f"  [shutdown] exit={proc.returncode} — log tail:")
        print("    " + "    ".join(log_path.read_text().splitlines(keepends=True)[-8:]))


# ---------------------------------------------------------------------------
# Telnet helpers (same shape as test_cffa1_telnet.py / test_jukebox_telnet.py)
# ---------------------------------------------------------------------------

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


def send_line(sock: socket.socket, cmd: str, *, wait: float = 0.3,
              read_t: float = 3.0) -> str:
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    out = recv_avail(sock, total=read_t, idle=0.35)
    if VERBOSE:
        print(f"    > {cmd!r}  <- {out!r}")
    return out


def send_ctrl(sock: socket.socket, byte: int) -> None:
    sock.sendall(bytes([byte & 0xFF]))


def check(name: str, cond: bool, detail: str = "") -> bool:
    global passed, failed
    if cond:
        passed += 1
        print(f"  [PASS] {name}")
        return True
    failed += 1
    print(f"  [FAIL] {name}" + (f" — {detail}" if detail else ""))
    return False


# ---------------------------------------------------------------------------
# Wozmon scraping
# ---------------------------------------------------------------------------

# Wozmon prints memory as `AAAA: BB BB BB BB BB BB BB BB`. Multiple lines on
# a range dump; the first address on each line is the row base.
WOZMON_LINE = re.compile(r"([0-9A-F]{4}):\s+((?:[0-9A-F]{2}\s+)+)")


def peek_range(sock: socket.socket, start: int, end: int, *,
               read_t: float = 5.0) -> bytes:
    """Ask Wozmon for `AAAA.BBBB`, parse its dump back into raw bytes."""
    cmd = f"{start:04X}.{end:04X}"
    out = send_line(sock, cmd, wait=0.4, read_t=read_t)
    # Start with the target address's row; skip anything Wozmon might have
    # printed before (like the echo of the command itself).
    out_up = out.upper()
    result: dict[int, int] = {}
    for match in WOZMON_LINE.finditer(out_up):
        row = int(match.group(1), 16)
        bytes_on_row = [int(b, 16) for b in match.group(2).split()]
        for offset, val in enumerate(bytes_on_row):
            result[row + offset] = val
    if VERBOSE:
        print(f"    [peek_range] {start:04X}.{end:04X} -> {len(result)} bytes parsed")
    return bytes(result.get(a, 0x00) for a in range(start, end + 1))


def poke_bytes(sock: socket.socket, addr: int, data: bytes) -> None:
    """Woz-monitor store: `AAAA: VV VV VV ...`. Wozmon accepts up to ~20
    bytes per line comfortably; we chunk conservatively at 16."""
    for chunk_start in range(0, len(data), 16):
        chunk = data[chunk_start:chunk_start + 16]
        hex_bytes = " ".join(f"{b:02X}" for b in chunk)
        cmd = f"{addr + chunk_start:04X}: {hex_bytes}"
        send_line(sock, cmd, wait=0.1, read_t=1.0)


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

def wait_for_tape_load(sock: socket.socket, probe_addr: int,
                       expected_first_byte: int,
                       deadline_s: float = 30.0) -> bytes:
    """Poll a 1-byte Wozmon peek until the expected byte shows up or the
    deadline expires. Returns whatever peek_range saw last.

    During an ACI READ the CPU is not running the Wozmon parser, so any
    keystrokes we send while the tape is streaming just queue in the PIA
    buffer. They get processed the moment the ACI ROM's terminal JMP
    $FF1A executes — so a peek issued early still lands once the read
    finishes, but we keep re-issuing it inside the wait loop in case the
    tape needs longer than the initial budget."""
    deadline = time.time() + deadline_s
    got = b""
    while time.time() < deadline:
        got = peek_range(sock, probe_addr, probe_addr, read_t=4.0)
        if got and got[0] == expected_first_byte:
            return got
        time.sleep(1.0)
    return got


def scenario_load() -> None:
    """A: preload APPLE50TH.ogg, drive ACI READ via telnet, verify signature."""
    print(f"\n=== Scenario A: LOAD "
          f"(cassettes/APPLE50TH.ogg -> ${APPLE50TH_FROM:04X}) ===")
    tape = repo_root() / "cassettes" / "APPLE50TH.ogg"
    if not tape.exists():
        check("A.0 APPLE50TH.ogg present", False, f"not found at {tape}")
        return
    log = repo_root() / "build" / "pom1_telnet_load.log"
    proc = launch_pom1(["--tape", str(tape)], log)
    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
        sock.settimeout(15)
        recv_avail(sock, total=1.2)        # drain banner

        # Hard-reset so we start from a known Wozmon state.
        send_ctrl(sock, CTRL_R)
        time.sleep(0.8)
        recv_avail(sock, total=1.5)

        # Plant $55 sentinels at the load address — they differ from every
        # byte of the expected signature (A9 FF 48), so a no-op ACI READ
        # would leave all three visibly wrong rather than partially matching.
        sentinel = bytes([0x55] * 16)
        poke_bytes(sock, APPLE50TH_FROM, sentinel)
        pre = peek_range(sock, APPLE50TH_FROM, APPLE50TH_FROM + 15, read_t=3.0)
        check(f"A.1 ${APPLE50TH_FROM:04X} seeded with $55 sentinels",
              pre == sentinel,
              f"first 16 = {pre.hex(' ')}")

        # C100R jumps into the ACI ROM; <from>.<to>R asks it to read the tape.
        # While the READ runs (CPU busy consuming pulses), nothing reads the
        # PIA keyboard buffer, so anything we push queues up for Wozmon to
        # eat once ACI jumps back to $FF1A.
        send_line(sock, "C100R", wait=0.4, read_t=2.5)
        send_line(sock, f"{APPLE50TH_FROM:04X}.{APPLE50TH_TO:04X}R",
                  wait=0.3, read_t=1.5)

        # Poll for the signature. APPLE50TH.ogg loads in ~10 s wallclock at
        # --cpu-max; 45 s budget is conservative.
        sig = b"\x55" * 3
        deadline = time.time() + 45.0
        while time.time() < deadline:
            time.sleep(1.5)
            peek16 = peek_range(sock, APPLE50TH_FROM,
                                APPLE50TH_FROM + 15, read_t=4.0)
            if peek16[:3] == APPLE50TH_SIG:
                sig = peek16[:3]
                break
        check(f"A.2 APPLE50TH signature at ${APPLE50TH_FROM:04X}",
              sig == APPLE50TH_SIG,
              f"got {sig.hex(' ')}, expected A9 FF 48")

        sock.close()
    finally:
        shutdown_pom1(proc, log)


def scenario_save_then_roundtrip(keep_tape: bool = False) -> None:
    """B + C: poke a pattern, drive ACI WRITE, --save-tape dumps on SIGTERM;
    then a fresh pom1 reloads the saved .aci and we read it back."""
    print("\n=== Scenario B: SAVE ($0300-$033F pattern -> .aci) ===")
    tape_path = Path("/tmp/pom1_telnet_aci_roundtrip.aci")
    if tape_path.exists():
        tape_path.unlink()

    log_b = repo_root() / "build" / "pom1_telnet_save.log"
    proc = launch_pom1(["--save-tape", str(tape_path)], log_b)
    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
        sock.settimeout(15)
        recv_avail(sock, total=1.2)

        send_ctrl(sock, CTRL_R)
        time.sleep(0.8)
        recv_avail(sock, total=1.5)

        # Plant the test pattern at $0300-$033F and read it back. Wozmon
        # dumps 8 bytes per line; for 64 bytes the output is 8 lines which
        # tends to stream in across several 50-ms sockets reads — hence
        # the generous read_t.
        poke_bytes(sock, TEST_FROM, TEST_PATTERN)
        planted = peek_range(sock, TEST_FROM, TEST_TO, read_t=6.0)
        check("B.1 Pattern planted at $0300-$033F",
              planted == TEST_PATTERN,
              f"plant got {len(planted)} bytes, first-16 diff: "
              f"{planted[:16].hex(' ')} vs {TEST_PATTERN[:16].hex(' ')}")

        # Drive WRITE. Same pattern as LOAD: send both lines back-to-back,
        # then wait for the ACI to finish. When ACI finishes writing it
        # JMPs to $FF1A, and `saveTape` on SIGTERM captures whatever the
        # deck recorded.
        send_line(sock, "C100R", wait=0.4, read_t=2.5)
        send_line(sock, f"{TEST_FROM:04X}.{TEST_TO:04X}W", wait=0.3, read_t=1.5)

        # ACI WRITE for 64 bytes takes ~15 s of emulated time (leader +
        # data + trailer). At --cpu-max under the live GLFW+Term+publisher
        # stack, that's ~5-10 s of wallclock — not the "~1 s" the previous
        # comment claimed, which would SIGTERM the process mid-write and
        # produce a truncated .aci (scenario C's pre-fix failure mode:
        # saved 8075 transitions vs the 17354 a complete write needs, no
        # data pulses reach the file). 15 s gives comfortable headroom;
        # scenario C has been observed to pass consistently at this value.
        time.sleep(15.0)
        sock.close()
    finally:
        # SIGTERM triggers ~MainWindow_ImGui -> saveTape() -> on-disk .aci.
        shutdown_pom1(proc, log_b, wait_save_s=5.0)

    check("B.3 Save file exists on disk",
          tape_path.exists(),
          f"{tape_path} missing — SIGTERM save handler didn't fire")
    if not tape_path.exists():
        return
    size = tape_path.stat().st_size
    check("B.4 Save file non-trivial (> 1 KB)",
          size > 1024,
          f"only {size} bytes — looks like the capture was empty")

    # --- Scenario C ------------------------------------------------------
    print("\n=== Scenario C: ROUND-TRIP (.aci -> fresh pom1 -> ACI READ) ===")
    log_c = repo_root() / "build" / "pom1_telnet_roundtrip.log"
    proc = launch_pom1(["--tape", str(tape_path)], log_c)
    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
        sock.settimeout(15)
        recv_avail(sock, total=1.2)

        send_ctrl(sock, CTRL_R)
        time.sleep(0.8)
        recv_avail(sock, total=1.5)

        # Clear target so a failed load shows up as zeros.
        poke_bytes(sock, TEST_FROM, bytes(TEST_LEN))

        send_line(sock, "C100R", wait=0.4, read_t=2.5)
        send_line(sock, f"{TEST_FROM:04X}.{TEST_TO:04X}R", wait=0.3, read_t=1.5)

        # Wait for the first byte to match, same approach as scenario A.
        probe = wait_for_tape_load(sock, TEST_FROM, TEST_PATTERN[0],
                                   deadline_s=30.0)
        readback = peek_range(sock, TEST_FROM, TEST_TO, read_t=6.0)

        if not check("C.1 Round-trip bytes match",
                     readback == TEST_PATTERN,
                     f"diff first 16: got {readback[:16].hex(' ')}, "
                     f"want {TEST_PATTERN[:16].hex(' ')}"):
            # Handy diff dump for debugging.
            diffs = [i for i in range(TEST_LEN) if readback[i] != TEST_PATTERN[i]]
            print(f"      {len(diffs)}/{TEST_LEN} bytes differ: "
                  f"{diffs[:12]}{' ...' if len(diffs) > 12 else ''}")

        sock.close()
    finally:
        shutdown_pom1(proc, log_c)
        if not keep_tape and tape_path.exists():
            tape_path.unlink()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--verbose", action="store_true",
                    help="print every telnet exchange + subprocess log tails")
    ap.add_argument("--keep-tape", action="store_true",
                    help="leave /tmp/pom1_telnet_aci_roundtrip.aci on disk "
                         "after the test (useful when a round-trip failure "
                         "leaves a broken tape to inspect)")
    args = ap.parse_args()
    VERBOSE = args.verbose

    print("=" * 60)
    print("ACI telnet test — load + save round-trip through Terminal Card")
    print("=" * 60)

    scenario_load()
    scenario_save_then_roundtrip(keep_tape=args.keep_tape)

    total = passed + failed
    print(f"\n{'=' * 60}")
    print(f"Results: {passed}/{total} PASSED")
    if failed:
        print(f"{failed} test(s) FAILED")
        return 1
    print("All tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
