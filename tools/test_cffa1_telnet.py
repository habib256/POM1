#!/usr/bin/env python3
"""test_cffa1_telnet.py -- Rich Dreher's CFFA1 CompactFlash card, end to end.

Boots a headless POM1 on preset 7 (Replica-1 with CFFA1 & Applesoft Lite),
backed by the shipped ProDOS image cfcard/cfcard.po, and drives the card's
firmware menu over the scripting control channel.

Phases:
  0. ID bytes at $AFDC/$AFDD, and the firmware menu comes up.
  1. Menu navigation: ?, CATALOG, PREFIX.
  2. Prefix into a directory, CATALOG it, LOAD a file, verify the bytes
     actually landed in RAM.
  3. Block-level I/O: read block 0 and block 2.
  4. QUIT returns to a live Woz Monitor.
  5. Applesoft Lite still comes up and runs a program.

TWO THINGS THIS HARNESS USED TO GET WRONG, both found when it was automated:

  * IT ENTERED THE MENU AT THE WRONG ADDRESS. The firmware has three entry
    points and they differ only in where QUIT goes: $9000 returns to the
    Monitor, $9003 to BASIC, $9006 to user code (doc/Apple1_Peripherals_
    Inventory_FR.md §CFFA1). The old harness entered at $9006 and then asserted
    that QUIT came back to the Monitor — which is the one thing that entry
    point promises not to do. It leaves the CPU at $0000, on a machine with no
    user code to return to. This one enters at $9000. (README.md still suggests
    `9006R` for opening the menu; that is fine for opening it and wrong for
    anyone who then types Q.)

  * ITS DISK EXPECTATIONS WERE FROM AN OLDER IMAGE. It looked for volume
    /CFFA1 with GAMES and UTILITIES holding LUNAR and STARTREK. The shipped
    cfcard.po is the "Ultimate Apple 1" collection: volume ULTIMATE.A1, with
    ASOFT / BASIC / FORTH / LANGS / MCODE / UTILS. Every assertion below names
    what is actually on the disk.

The name still says "telnet" because that is what it used to be: it drove the
Terminal Card on a hardcoded port 6502 and, uniquely among these harnesses,
did not even launch POM1 — the operator had to have one running with the right
preset. It now runs headless over the control channel (tools/pom1_control.py,
src/CommandPort.h) on a free port, which is what let it into ctest as
`cffa1_compactflash`.

Run from anywhere: python3 tools/test_cffa1_telnet.py [-v]
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT, skip  # noqa: E402

CF_IMAGE = REPO_ROOT / "cfcard" / "cfcard.po"
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

MENU_PROMPT = "CFFA1> "
PAGINATOR = "[ SPACE/CR OR ESC ]"


def drain(m: Pom1, timeout_s: float = 30.0) -> str:
    """Read menu output back to the `CFFA1> ` prompt, dismissing paginators.

    CATALOG and block dumps paginate every 20 lines and wait for a key; without
    this the next command lands inside the paginator and the menu reads it as a
    continuation. ESC is what dismisses it — and ESC is exactly the byte the
    CLI's --paste filter drops, which is why the control channel's `key` verb
    is literal (see src/CommandPort.h).
    """
    out, end = "", time.time() + timeout_s
    while time.time() < end:
        got = m.try_expect(MENU_PROMPT, 600)
        if got is not None:
            return out + got
        got = m.try_expect(PAGINATOR, 400)
        if got is not None:
            out += got
            m.key("\x1b")
            continue
    raise Pom1Error(f"CFFA1 menu never came back to its prompt; saw: {out[-400:]!r}")


def menu(m: Pom1, letter: str, *answers: str, timeout_s: float = 30.0) -> str:
    """Pick a single-letter menu command, then answer its prompts.

    The menu reads ONE character with no RETURN, so sending "C\\r" runs CATALOG
    and then feeds the CR to the *next* read as a command of its own — which is
    how the old harness ended up reading each command's output under the
    following command's name.
    """
    m.key(letter)
    for a in answers:
        time.sleep(0.25)
        m.key(a + "\r")
    return drain(m, timeout_s)


def main() -> int:
    if not CF_IMAGE.is_file():
        skip(f"{CF_IMAGE} not found — the shipped ProDOS image is required")

    c = Checks("CFFA1 CompactFlash -- firmware menu, files and blocks")

    with Pom1(preset=7, verbose=VERBOSE) as m:
        # --- Phase 0: the card answers at all ---
        print("\nPhase 0: firmware detection")
        m.monitor()
        c.ok("0.1 CFFA1 ID byte 1 ($AFDC = CF)", m.peek(0xAFDC) == b"\xCF",
             f"got {m.peek(0xAFDC).hex()}")
        c.ok("0.2 CFFA1 ID byte 2 ($AFDD = FA)", m.peek(0xAFDD) == b"\xFA",
             f"got {m.peek(0xAFDD).hex()}")
        # $9000 — the entry whose QUIT returns to the Monitor (phase 4).
        out = m.command("9000R", expect=MENU_PROMPT, timeout_ms=10000)
        c.contains("0.3 firmware menu banner", out, MENU_PROMPT)

        # --- Phase 1: menu navigation ---
        print("\nPhase 1: menu commands")
        out = menu(m, "?")
        c.contains("1.1 ? lists CATALOG", out, "CATALOG")
        c.contains("1.2 ? lists LOAD", out, "LOAD")
        c.contains("1.3 ? lists QUIT", out, "QUIT")

        out = menu(m, "C")
        c.contains("1.4 catalog shows the volume", out, "ULTIMATE.A1")
        c.contains("1.5 catalog shows MCODE", out, "MCODE")
        c.contains("1.6 catalog shows UTILS", out, "UTILS")
        c.contains("1.7 catalog reports free blocks", out, "BLKS FREE")

        # --- Phase 2: prefix, catalog, load ---
        print("\nPhase 2: prefix and load")
        out = menu(m, "P", "MCODE")
        c.contains("2.1 prefix to MCODE succeeds", out, "SUCCESS")
        c.excludes("2.2 prefix to MCODE no error", out, "NOT FOUND")

        out = menu(m, "C")
        c.contains("2.3 MCODE catalog shows HELLOWORLD", out, "HELLOWORLD")

        # LOAD asks for a name AND a load address; the empty second answer takes
        # the file's own ($0280). Leaving it out lets the NEXT command's first
        # letter answer the address prompt, which the firmware rejects with
        # "56 BAD BUFF ADDRESS" — a failure that shows up one command later.
        out = menu(m, "L", "HELLOWORLD", "")
        c.contains("2.4 LOAD HELLOWORLD succeeds", out, "SUCCESS")
        c.excludes("2.5 LOAD HELLOWORLD not NOT FOUND", out, "NOT FOUND")
        # The bytes must actually be in RAM: "00 SUCCESS" on screen and an empty
        # $0280 is exactly the kind of pass a display-only assertion cannot tell
        # from a real load. HELLOWORLD starts LDX #$0C / LDA $028B,X / JSR $FFEF.
        c.ok("2.6 loaded bytes are in RAM at $0280",
             m.peek(0x0280, 6) == bytes([0xA2, 0x0C, 0xBD, 0x8B, 0x02, 0x20]),
             f"got {m.peek(0x0280, 6).hex(' ')}")

        # --- Phase 3: block-level I/O ---
        print("\nPhase 3: block I/O")
        out = menu(m, "B", "0")
        c.excludes("3.1 read block 0 no error", out, "ERROR")
        out = menu(m, "B", "2")
        c.excludes("3.2 read block 2 no error", out, "ERROR")

        # --- Phase 4: quit ---
        print("\nPhase 4: quit to the Monitor")
        m.key("Q")
        time.sleep(0.5)
        c.ok("4.1 QUIT lands in the Woz Monitor", 0xFF00 <= m.pc() <= 0xFFFF,
             f"pc=${m.pc():04X}")
        out = m.command("FF00.FF03", expect="FF00: D8 58", timeout_ms=8000)
        c.contains("4.2 the Monitor answers a dump", out, "FF00: D8 58")

        # --- Phase 5: Applesoft Lite is still there ---
        print("\nPhase 5: Applesoft Lite")
        m.monitor()
        out = m.command("E000R", expect="]", timeout_ms=8000)
        c.contains("5.1 Applesoft Lite prompt", out, "]")
        m.command("NEW", expect="]", timeout_ms=5000)
        m.command('10 PRINT "HELLO CFFA1"', expect="]", timeout_ms=5000)
        out = m.command("LIST", expect="]", timeout_ms=8000)
        c.contains("5.2 LIST shows the program", out, "HELLO CFFA1")
        out = m.command("RUN", expect="]", timeout_ms=8000)
        c.contains("5.3 RUN prints the string", out, "HELLO CFFA1")

    return c.summary()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        sys.exit(1)
