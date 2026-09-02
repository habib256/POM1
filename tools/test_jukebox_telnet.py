#!/usr/bin/env python3
"""test_jukebox_telnet.py -- P-LAB Apple-1 Juke-Box, end to end.

Boots a headless POM1 on preset 4 with --enable jukebox (there is no dedicated
Juke-Box preset) and drives the Program Manager firmware at $BD00 over the
scripting control channel, following the official P-LAB Juke-Box v1.09 manual:

  H    help screen              P0-F page select (hex digit)
  D    directory of the page    B    enter BASIC (E2B3 soft entry)
  L?   load program (letter)    X    exit to the Woz Monitor

Phases:
  0. firmware signature at $BD00
  1. Program Manager entry and H)elp
  2. D)IR listing -- parsed, then checked for shape and known entries
  3. page select, including the single-page 28c256 mirror
  4. an invalid command prints '!'
  5. load a machine-code program and verify the bytes reached RAM
  6. load the BASIC interpreter, enter it, and run a program
  7. the canonical LA + L<BAS> + B + LIST flow
  8. L<BAS> + B with no prior LA must not hang

WHY THE CATALOGUE IS PARSED RATHER THAN PINNED
    The old harness carried a 12-entry EXPECTED_CATALOG naming every program,
    its letter and its address range, and asserted all three for each. That
    list no longer matches roms/jukebox.rom: E and F are now DEMO40TH and
    DEMO MLR, STARTREK has moved from E to G, CHECKERS / AMAZING / BLACKJAK /
    BATNUM / REVERSE are gone and SUDOKU / MASTRMND have arrived. Pinning a
    generated ROM's contents by hand is a list that rots every time the ROM is
    rebuilt, and this one rotted unnoticed because the test never ran. What is
    checked instead is what the FIRMWARE promises: that every line has the
    documented `<letter> <name> $from-$to [BAS]` shape, that the BAS tags agree
    with the entries carrying them, and that the two entries the later phases
    actually need are present and are found BY NAME, not by position.

The name still says "telnet" because that is what it used to be: it drove the
Terminal Card on a hardcoded port 6502 and, like the CFFA1 harness, did not
launch POM1 at all — the operator had to start one with the right flags. It now
runs headless over the control channel (tools/pom1_control.py,
src/CommandPort.h), which is what let it into ctest as `jukebox_program_manager`.

Run from anywhere: python3 tools/test_jukebox_telnet.py [-v]
"""
from __future__ import annotations

import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT, skip  # noqa: E402

JUKEBOX_ROM = REPO_ROOT / "roms" / "jukebox.rom"
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

PM = "&"                 # Program Manager prompt
BASIC_PROMPT = ">"       # Integer BASIC prompt

# " G STARTREK  $0430-$1000 BAS"
ENTRY_RE = re.compile(
    r"^\s*([A-Z])\s+(\S.*?)\s+\$([0-9A-F]{4})-\$([0-9A-F]{4})(\s+BAS)?\s*$")


class Entry:
    def __init__(self, letter, name, start, end, is_basic):
        self.letter, self.name = letter, name
        self.start, self.end, self.is_basic = start, end, is_basic

    def __repr__(self):
        return (f"{self.letter} {self.name} ${self.start:04X}-${self.end:04X}"
                f"{' BAS' if self.is_basic else ''}")


def parse_catalog(listing: str) -> list[Entry]:
    out = []
    for line in listing.replace("\r", "\n").split("\n"):
        mo = ENTRY_RE.match(line)
        if mo:
            out.append(Entry(mo.group(1), mo.group(2).strip(),
                             int(mo.group(3), 16), int(mo.group(4), 16),
                             mo.group(5) is not None))
    return out


def find(entries: list[Entry], name: str) -> Entry | None:
    for e in entries:
        if e.name.upper().startswith(name.upper()):
            return e
    return None


def enter_basic(m: Pom1) -> str:
    """`B` from the Program Manager. The firmware wants a second RETURN before
    Integer BASIC shows its prompt."""
    m.type_line("B")
    time.sleep(0.5)
    m.key("\r")
    return m.expect(BASIC_PROMPT, timeout_ms=10000)


def main() -> int:
    if not JUKEBOX_ROM.is_file():
        skip(f"{JUKEBOX_ROM} not found — generate it with "
             f"doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py")

    c = Checks("P-LAB Juke-Box -- Program Manager, pages, loads and BASIC")

    with Pom1(preset=4, enable=["jukebox"], verbose=VERBOSE) as m:
        # --- Phase 0: the firmware is there ---
        print("\nPhase 0: firmware signature")
        m.monitor()
        c.ok("0.1 firmware signature $BD00 = $A5", m.peek(0xBD00) == b"\xA5",
             f"got {m.peek(0xBD00).hex()}")

        # --- Phase 1: Program Manager + help ---
        print("\nPhase 1: Program Manager entry and H)elp")
        out = m.command("BD00R", expect=PM, timeout_ms=10000)
        c.contains("1.1 Program Manager '&' prompt", out, PM)
        out = m.command("H", expect=PM, timeout_ms=8000)
        for n, line in enumerate(["D)IR", "L)OAD", "S)ET", "P)AGE", "B)ASIC", "E(X)IT"], 2):
            c.contains(f"1.{n} help lists {line}", out, line)

        # --- Phase 2: the directory ---
        print("\nPhase 2: D)IR listing")
        out = m.command("D", expect=PM, timeout_ms=15000)
        c.contains("2.1 header shows PAGE 0", out, "PAGE 0")
        entries = parse_catalog(out)
        c.ok("2.2 directory parses into entries", len(entries) >= 8,
             f"parsed {len(entries)}: {entries}")
        c.ok("2.3 entry letters are contiguous from A",
             [e.letter for e in entries] ==
             [chr(ord("A") + i) for i in range(len(entries))],
             f"letters: {[e.letter for e in entries]}")
        c.ok("2.4 every entry has a sane address range",
             all(e.start < e.end for e in entries),
             f"bad: {[repr(e) for e in entries if e.start >= e.end]}")
        # Count the tag at END OF LINE. A bare `" BAS" in text` count reads the
        # interpreter's own name — "A BASIC" contains " BAS" — and reports one
        # tag more than there are BAS programs.
        text_bas = len(re.findall(r"\bBAS\s*(?=[\r\n]|$)", out))
        c.ok("2.5 BAS tags agree with the parsed entries",
             sum(1 for e in entries if e.is_basic) == text_bas,
             f"{sum(1 for e in entries if e.is_basic)} parsed vs {text_bas} in text")

        basic_entry = find(entries, "BASIC")
        life_entry = find(entries, "LIFE")
        bas_entry = next((e for e in entries if e.is_basic), None)
        c.ok("2.6 catalogue holds the BASIC interpreter", basic_entry is not None)
        c.ok("2.7 catalogue holds LIFE", life_entry is not None)
        c.ok("2.8 catalogue holds at least one BAS program", bas_entry is not None)
        if not (basic_entry and life_entry and bas_entry):
            return c.summary()
        c.ok("2.9 BASIC interpreter loads at $E000", basic_entry.start == 0xE000,
             repr(basic_entry))

        # --- Phase 3: page select ---
        print("\nPhase 3: page select (P)")
        for page in "012389ABCDEF":
            out = m.command(f"P{page}", expect=PM, timeout_ms=6000)
            c.ok(f"3.{page} P{page} accepted",
                 "OK" in out.upper() and "!" not in out, repr(out))
        m.command("P2", expect=PM, timeout_ms=6000)
        out = m.command("D", expect=PM, timeout_ms=15000)
        c.contains("3.M header shows PAGE 2 after P2", out, "PAGE 2")
        # Page 2 holds its OWN programs, and that is the point: roms/jukebox.rom
        # is 256 KB, i.e. 16 real pages. The old harness asserted that page 2
        # MIRRORED page 0 — true of a single-page 28c256, and false of the ROM
        # that has shipped since. What is asserted here is that a second page is
        # a valid, distinct catalogue.
        page2 = parse_catalog(out)
        c.ok("3.N page 2 is a valid catalogue of its own",
             len(page2) >= 1 and all(e.start < e.end for e in page2),
             f"page 2 parsed as {page2}")
        c.ok("3.O page 2 differs from page 0 (multi-page ROM)",
             [repr(e) for e in page2] != [repr(e) for e in entries],
             "page 2 mirrors page 0 — is this a single-page 28c256 image?")
        m.command("P0", expect=PM, timeout_ms=6000)

        # --- Phase 4: an invalid command ---
        print("\nPhase 4: invalid command handling")
        # 'Z' is neither a command letter nor a catalogue index. Manual §5.8.
        out = m.command("Z", expect=PM, timeout_ms=6000)
        c.contains("4.1 invalid command prints '!'", out, "!")

        # --- Phase 5: load machine code ---
        print(f"\nPhase 5: load machine code ({life_entry.name})")
        m.poke(life_entry.start, [0xFF] * 16)   # so a no-op load is visible
        out = m.command(f"L{life_entry.letter}", expect=PM, timeout_ms=10000)
        c.ok(f"5.1 L{life_entry.letter} ({life_entry.name}) returns OK",
             "OK" in out.upper() and "!" not in out, repr(out))
        c.ok(f"5.2 bytes landed at ${life_entry.start:04X}",
             m.peek(life_entry.start, 16) != bytes([0xFF] * 16),
             "target still holds the $FF sentinels")
        out = m.command("X", expect="\\", timeout_ms=6000)
        c.contains("5.3 X returns to the Woz Monitor", out, "\\")

        # --- Phase 6: BASIC interpreter, entered and run ---
        print("\nPhase 6: load the BASIC interpreter and run a program")
        m.command("BD00R", expect=PM, timeout_ms=10000)
        out = m.command(f"L{basic_entry.letter}", expect=PM, timeout_ms=10000)
        c.ok("6.1 LA (BASIC interpreter) returns OK", "OK" in out.upper(), repr(out))
        c.ok("6.2 $E000 = $4C (BASIC JMP)", m.peek(0xE000) == b"\x4C",
             f"got {m.peek(0xE000).hex()}")
        c.contains("6.3 B reaches the BASIC prompt", enter_basic(m), BASIC_PROMPT)

        # Cold-start for a clean program area, then type and run.
        m.monitor()
        m.type_line("E000R")
        time.sleep(0.6)
        m.key("\r")
        c.contains("6.4 E000R cold start reaches the prompt",
                   m.expect(BASIC_PROMPT, timeout_ms=10000), BASIC_PROMPT)
        m.command("NEW", expect=BASIC_PROMPT, timeout_ms=6000)
        m.command('10 PRINT "JBOXOK"', expect=BASIC_PROMPT, timeout_ms=6000)
        out = m.command("RUN", expect=BASIC_PROMPT, timeout_ms=10000)
        c.contains("6.5 RUN prints JBOXOK", out, "JBOXOK")

        # --- Phase 7: the canonical BASIC-program flow (manual §5.5) ---
        print(f"\nPhase 7: LA + L{bas_entry.letter} ({bas_entry.name}) + B + LIST")
        m.monitor()
        out = m.command("BD00R", expect=PM, timeout_ms=10000)
        c.contains("7.1 Program Manager re-entered", out, PM)
        out = m.command(f"L{basic_entry.letter}", expect=PM, timeout_ms=10000)
        c.ok("7.2 LA returns OK", "OK" in out.upper(), repr(out))
        out = m.command(f"L{bas_entry.letter}", expect=PM, timeout_ms=10000)
        c.ok(f"7.3 L{bas_entry.letter} ({bas_entry.name}, BAS) returns OK",
             "OK" in out.upper() and "!" not in out, repr(out))
        c.contains("7.4 B reaches the BASIC prompt", enter_basic(m), BASIC_PROMPT)

        m.type_line("LIST")
        time.sleep(2.5)
        listing = m.screen()
        c.excludes("7.5 LIST raises no BAD BRANCH ERR", listing, "BAD BRANCH")
        c.excludes("7.6 LIST raises no SYNTAX ERR", listing, "SYNTAX")
        c.ok("7.7 LIST produces a real listing", len(listing.strip()) > 120,
             f"only {len(listing.strip())} bytes of output")
        c.ok("7.8 LIST shows a numbered line",
             bool(re.search(r"[\r\n]\s*\d+\s", listing)),
             "no line-number pattern in the listing")

        # --- Phase 8: L<BAS> + B with no prior LA must not hang ---
        print("\nPhase 8: L<BAS> then B without a prior LA (regression)")
        # Before the preset carried Integer BASIC at $E000, $E000 read $00 and
        # B hung in an endless BRK loop. A warm reset keeps RAM, so the preset's
        # interpreter is still there.
        m.monitor()
        out = m.command("BD00R", expect=PM, timeout_ms=10000)
        c.contains("8.1 Program Manager re-entered", out, PM)
        out = m.command(f"L{bas_entry.letter}", expect=PM, timeout_ms=10000)
        c.ok(f"8.2 L{bas_entry.letter} returns OK",
             "OK" in out.upper() and "!" not in out, repr(out))
        c.contains("8.3 B reaches the BASIC prompt with no prior LA",
                   enter_basic(m), BASIC_PROMPT)

    return c.summary()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        sys.exit(1)
