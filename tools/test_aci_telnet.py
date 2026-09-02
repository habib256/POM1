#!/usr/bin/env python3
"""test_aci_telnet.py -- End-to-end ACI cassette load + save round-trip.

Unlike tests/aci_tape_loading_test.cpp / aci_tape_saving_test.cpp, which drive
the M6502 directly against a headless Memory, this script exercises the *real*
POM1 binary: EmulationController's mutex stack, the emulation thread, the
cassette deck's audio path, the snapshot publisher, the --tape / --save-tape
plumbing and the shutdown flush. If one of those layers breaks the tape path,
the unit tests stay green and this one goes red.

Scenarios:
  A. LOAD  -- preload cassettes/APPLE50TH.ogg via --tape, drive the ACI ROM in
              READ mode (`C100R`, then `0280.0FFFR`), and wait for the
              APPLE50TH signature `A9 FF 48` (LDA #$FF / PHA) at $0280.
  B. SAVE  -- plant a known 64-byte pattern at $0300-$033F, drive the ACI ROM
              in WRITE mode (`0300.033FW`), let the write finish, then shut the
              emulator down so --save-tape flushes the capture to disk.
  C. ROUND -- preload the .aci that B produced into a FRESH instance, drive
              READ, and assert the bytes come back identical.

The name still says "telnet" because that is what it used to be: it drove the
Terminal Card on a hardcoded port 6502, planting and reading its test pattern
by typing Woz Monitor pokes and parsing the dump that echoed back. It now runs
headless over the scripting control channel (tools/pom1_control.py,
src/CommandPort.h), which changes two things beyond the port:

  * memory is read and written with `peek`/`poke`, so a scenario no longer
    fails because a 64-byte Monitor dump arrived across several socket reads;
  * the two long waits are now CONDITIONS rather than sleeps. Scenario B used
    to `time.sleep(15.0)` for the ACI write, with a comment recording that an
    earlier `~1 s` guess had been silently truncating the tape (8075
    transitions saved against the 17354 a complete write needs). The ACI jumps
    back to $FF1A when it is done, so the harness now waits for the PC to
    return to the Monitor and moves on the moment it does.

That is what let this into ctest as `aci_cassette_roundtrip`.

Run from anywhere: python3 tools/test_aci_telnet.py [-v] [--keep-tape]
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT, skip  # noqa: E402

ACI_PRESET = 4          # "Apple-1 with ACI & BASIC cassette (October 1976)"

# 64 bytes at $0300-$033F. No long constant runs, so a framing bug corrupts
# visible nibbles rather than hiding inside a stretch of identical values.
TEST_FROM = 0x0300
TEST_TO = 0x033F
TEST_LEN = TEST_TO - TEST_FROM + 1
TEST_PATTERN = bytes(((0xA5 ^ (i * 37)) & 0xFF) for i in range(TEST_LEN))

APPLE50TH_TAPE = REPO_ROOT / "cassettes" / "APPLE50TH.ogg"
APPLE50TH_SIG = bytes([0xA9, 0xFF, 0x48])
APPLE50TH_FROM = 0x0280
APPLE50TH_TO = 0x0FFF

SAVED_TAPE = REPO_ROOT / "build" / "pom1_aci_roundtrip.aci"

VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv
KEEP_TAPE = "--keep-tape" in sys.argv


def read_bytes(m: Pom1, start: int, length: int) -> bytes:
    """peek in 256-byte chunks — the channel caps one request at 256."""
    out = b""
    while len(out) < length:
        n = min(256, length - len(out))
        out += m.peek(start + len(out), n)
    return out


def write_bytes(m: Pom1, start: int, data: bytes) -> None:
    for off in range(0, len(data), 128):
        m.poke(start + off, data[off:off + 128])


def drive_aci(m: Pom1, command: str) -> None:
    """`C100R` into the ACI ROM, then its <from>.<to>R / W command line.

    Nothing reads the PIA keyboard buffer while the ACI is busy, so both lines
    can be queued back to back: the second waits in the queue until the ROM
    hands control back to the Monitor at $FF1A.
    """
    m.type_line("C100R")
    time.sleep(0.4)
    m.type_line(command)


def wait_for(predicate, timeout_s: float, poll_s: float = 0.5) -> bool:
    end = time.time() + timeout_s
    while time.time() < end:
        if predicate():
            return True
        time.sleep(poll_s)
    return False


def scenario_load(c: Checks) -> None:
    print(f"\n=== Scenario A: LOAD (APPLE50TH.ogg -> ${APPLE50TH_FROM:04X}) ===")
    with Pom1(preset=ACI_PRESET, verbose=VERBOSE,
              extra_args=["--tape", str(APPLE50TH_TAPE)]) as m:
        m.monitor()
        # $55 sentinels differ from every byte of the expected signature, so a
        # no-op READ leaves all three visibly wrong instead of half-matching.
        write_bytes(m, APPLE50TH_FROM, bytes([0x55] * 16))
        c.ok(f"A.1 ${APPLE50TH_FROM:04X} seeded with $55 sentinels",
             read_bytes(m, APPLE50TH_FROM, 16) == bytes([0x55] * 16))

        drive_aci(m, f"{APPLE50TH_FROM:04X}.{APPLE50TH_TO:04X}R")
        got = wait_for(
            lambda: m.peek(APPLE50TH_FROM, 3) == APPLE50TH_SIG,
            timeout_s=60.0, poll_s=1.0)
        c.ok(f"A.2 APPLE50TH signature at ${APPLE50TH_FROM:04X}", got,
             f"got {m.peek(APPLE50TH_FROM, 3).hex(' ')}, expected A9 FF 48")


def scenario_save(c: Checks) -> bool:
    print("\n=== Scenario B: SAVE ($0300-$033F pattern -> .aci) ===")
    if SAVED_TAPE.exists():
        SAVED_TAPE.unlink()

    with Pom1(preset=ACI_PRESET, verbose=VERBOSE,
              extra_args=["--save-tape", str(SAVED_TAPE)]) as m:
        m.monitor()
        write_bytes(m, TEST_FROM, TEST_PATTERN)
        c.ok("B.1 pattern planted at $0300-$033F",
             read_bytes(m, TEST_FROM, TEST_LEN) == TEST_PATTERN)

        drive_aci(m, f"{TEST_FROM:04X}.{TEST_TO:04X}W")
        # The ACI jumps to $FF1A when the write completes. Waiting for that is
        # what replaced a 15 s sleep whose predecessor (~1 s) had been quietly
        # truncating the tape. Give it room: 64 bytes is leader + data +
        # trailer, ~15 s of emulated time.
        time.sleep(1.0)   # let it leave the Monitor first
        done = wait_for(lambda: 0xFF00 <= m.pc() <= 0xFFFF, timeout_s=90.0)
        c.ok("B.2 ACI WRITE returned to the Monitor", done, f"pc=${m.pc():04X}")

    # SIGTERM on close() runs the shutdown handler, which is what flushes
    # --save-tape to disk.
    if not c.ok("B.3 save file exists on disk", SAVED_TAPE.is_file(),
                f"{SAVED_TAPE} missing — the shutdown save handler did not fire"):
        return False
    size = SAVED_TAPE.stat().st_size
    return c.ok("B.4 save file is non-trivial (> 1 KB)", size > 1024,
                f"only {size} bytes — the capture looks empty")


def scenario_roundtrip(c: Checks) -> None:
    print("\n=== Scenario C: ROUND-TRIP (.aci -> fresh POM1 -> ACI READ) ===")
    with Pom1(preset=ACI_PRESET, verbose=VERBOSE,
              extra_args=["--tape", str(SAVED_TAPE)]) as m:
        m.monitor()
        # Zero the target so a failed load reads as zeros, not as leftovers.
        write_bytes(m, TEST_FROM, bytes(TEST_LEN))
        drive_aci(m, f"{TEST_FROM:04X}.{TEST_TO:04X}R")

        wait_for(lambda: m.peek(TEST_FROM, 1)[0] == TEST_PATTERN[0],
                 timeout_s=60.0, poll_s=1.0)
        # One more poll interval: the first byte landing means the read is
        # under way, not that it has finished.
        wait_for(lambda: read_bytes(m, TEST_FROM, TEST_LEN) == TEST_PATTERN,
                 timeout_s=20.0, poll_s=1.0)
        readback = read_bytes(m, TEST_FROM, TEST_LEN)

        if not c.ok("C.1 round-trip bytes match", readback == TEST_PATTERN,
                    f"first 16: got {readback[:16].hex(' ')}, "
                    f"want {TEST_PATTERN[:16].hex(' ')}"):
            diffs = [i for i in range(TEST_LEN) if readback[i] != TEST_PATTERN[i]]
            print(f"      {len(diffs)}/{TEST_LEN} bytes differ: "
                  f"{diffs[:12]}{' ...' if len(diffs) > 12 else ''}")


def main() -> int:
    if not APPLE50TH_TAPE.is_file():
        skip(f"{APPLE50TH_TAPE} not found")

    c = Checks("ACI cassette -- load, save and round-trip")
    try:
        scenario_load(c)
        if scenario_save(c):
            scenario_roundtrip(c)
    finally:
        if not KEEP_TAPE and SAVED_TAPE.exists():
            SAVED_TAPE.unlink()
    return c.summary()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        sys.exit(1)
