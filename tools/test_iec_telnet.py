#!/usr/bin/env python3
"""test_iec_telnet.py -- End-to-end smoke for the P-LAB IEC daughterboard.

Boots a headless POM1 with --preset 8 (microSD + Applesoft Lite) and
--enable iec, then exercises the @-prefixed IEC commands of SD CARD OS 1.3
against the bundled disks/iec/dev8.d64 fixture (label "POM1 IEC", id "01",
with a single PRG file "HELLO" of 256 bytes).

Tests:
  1. 8000R          -> SD CARD OS prompt
  2. @DEV           -> "DEVICE:" + "8"      (default device)
  3. @DEV 9 ; @DEV  -> "DEVICE: 9"          (set/get round-trip)
  4. @DEV 8         -> back to default
  5. @$             -> contains "POM1" + "HELLO" + "BLOCKS FREE"
  6. @ERR           -> a status code, no timeout

This is the live verification that the IECCard byte-frame FSM can talk to the
firmware without timing out (?DEVICE NOT PRESENT). If 5. fails but 2./3./4.
pass, the FSM is plugged in but the directory transmission path is broken. If
2. fails (no @DEV echo), the firmware can't even reach the drive -- most likely
VIA pin polarity is wrong.

The name still says "telnet" because that is what it used to be: the harness
drove the Terminal Card's port 6502 and slept between commands. It now runs
headless over the scripting control channel (tools/pom1_control.py,
src/CommandPort.h), so it picks a free port, launches and reaps its own
emulator, and waits on the display instead of on the clock. That is what let it
into ctest as `iec_daughterboard`.

Run from anywhere: python3 tools/test_iec_telnet.py [-v]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT, skip  # noqa: E402

DEV8_D64 = REPO_ROOT / "disks" / "iec" / "dev8.d64"
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv


def main() -> int:
    if not DEV8_D64.is_file():
        skip(f"{DEV8_D64} not found — from build/, run "
             f"`make make_iec_fixture && ./tools/make_iec_fixture {DEV8_D64}`")

    c = Checks("P-LAB IEC daughterboard -- @-commands smoke")

    with Pom1(preset=8, enable=["iec"], verbose=VERBOSE) as m:
        print("\nStep 1: launch SD CARD OS (8000R)")
        m.monitor()
        out = m.command("8000R", expect="/>", timeout_ms=8000)
        c.contains("1.1 SD CARD OS prompt visible", out, "/>")

        # Every command below anchors on the SD CARD OS prompt that follows it,
        # never on the answer itself: `expect` consumes up to and including its
        # match, so anchoring on "DEVICE" would leave the ": 8" that matters
        # unread — and would leave it sitting in front of the NEXT command's
        # expect. Anchoring on the prompt captures the whole reply and lands the
        # mark at a known place. That property is the reason this file no longer
        # needs a single sleep.
        print("\nStep 2: @DEV (read default device)")
        out = m.command("@DEV", expect="/>", timeout_ms=6000)
        c.contains("2.1 @DEV emits 'DEVICE'", out, "DEVICE")
        c.contains("2.2 @DEV default = 8", out, "DEVICE: 8")

        print("\nStep 3: @DEV 9 ; @DEV (set + read back)")
        m.command("@DEV 9", expect="/>", timeout_ms=6000)
        out = m.command("@DEV", expect="/>", timeout_ms=6000)
        c.contains("3.1 @DEV after set -> 9", out, "DEVICE: 9")

        print("\nStep 4: @DEV 8 (back to default)")
        m.command("@DEV 8", expect="/>", timeout_ms=6000)
        out = m.command("@DEV", expect="/>", timeout_ms=6000)
        c.contains("4.1 @DEV after reset -> 8", out, "DEVICE: 8")

        print("\nStep 5: @$ directory listing (live IEC bus traffic)")
        out = m.command("@$", expect="/>", timeout_ms=20000)
        c.excludes("5.1 @$ does not time out (?DEVICE NOT PRESENT)",
                   out, "DEVICE NOT PRESENT")
        c.contains("5.2 @$ shows disk label POM1", out, "POM1")
        c.contains("5.3 @$ shows HELLO file entry", out, "HELLO")
        c.contains("5.4 @$ shows BLOCKS FREE trailer", out, "BLOCKS FREE")

        print("\nStep 6: @ERR (read error channel)")
        out = m.command("@ERR", expect="/>", timeout_ms=10000)
        c.excludes("6.1 @ERR does not time out", out, "DEVICE NOT PRESENT")
        # 73 = power-on banner ("CBM DOS V2.6 1541"); 00 = OK after a good op.
        c.contains("6.2 @ERR shows a status code", out, ",")

    rc = c.summary()
    if rc:
        print("\nPossible causes if all @ commands fail:")
        print("  - VIA pin polarity / map mismatch in IECCard.h "
              "(kAtnOutBit/kClkOutBit/kDataOutBit/kClkInBit/kDataInBit)")
        print("  - Frame timing too coarse — kTxBitSettleCycles / "
              "kTxByteAckCycles in IECCard.cpp may need tuning")
        print("  - VIA T2 not ticking — check MicroSD.cpp::advanceCycles t2Running")
    return rc


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        sys.exit(1)
