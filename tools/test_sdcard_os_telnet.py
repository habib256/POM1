#!/usr/bin/env python3
"""test_sdcard_os_telnet.py -- P-LAB microSD "SD CARD OS" firmware, end to end.

Boots a headless POM1 on preset 8 (P-LAB Apple-1 with microSD & Applesoft Lite),
seeds a fixture tree under sdcard/, starts the firmware with `8000R` and
exercises the whole command set over the scripting control channel.

Phases:
  1. read-only commands: ?, HELP, PWD, DIR, LS, TYPE, DUMP, MOUNT, BAS, TEST
  2. directory navigation: CD (relative, absolute, .., fuzzy, trailing slash)
  3. file reads: LOAD (exact / fuzzy / tagged / missing), DUMP, READ, RUN
  4. file writes: MKDIR, RMDIR, MD/RD, DEL/RM, WRITE, SAVE -- each checked on
     the HOST filesystem, not only in the firmware's reply
  5. EXIT and re-entry
  6. error cases, including the two path-traversal escapes

The fixtures are created here and removed afterwards, so the test does not
depend on what sdcard/ happens to hold — that directory ships as a copy of
Claudio Parmigiani's real card and its layout is not ours to pin.

The name still says "telnet" because that is what it used to be: it drove the
Terminal Card on a hardcoded port 6502 and, like the CFFA1 and Juke-Box
harnesses, expected the operator to have POM1 already running with the right
preset. It now runs headless over the control channel (tools/pom1_control.py,
src/CommandPort.h) on a free port, launching and reaping its own emulator,
which is what let it into ctest as `sdcard_os_commands`.

Run from anywhere: python3 tools/test_sdcard_os_telnet.py [-v]
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT  # noqa: E402

SDCARD = REPO_ROOT / "sdcard"
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

PROMPT = ">"          # SD CARD OS prompt is "<path]>" — ">" is its tail
ROOT_PROMPT = "/>"

# Prints "OK" + CR through the Woz Monitor's ECHO, then returns to the Monitor.
SMALL_PROGRAM = bytes([
    0xA9, 0xCF, 0x20, 0xEF, 0xFF,   # LDA #'O'|$80 ; JSR ECHO
    0xA9, 0xCB, 0x20, 0xEF, 0xFF,   # LDA #'K'|$80 ; JSR ECHO
    0xA9, 0x8D, 0x20, 0xEF, 0xFF,   # LDA #CR |$80 ; JSR ECHO
    0x4C, 0x00, 0xFF,               # JMP $FF00
])

# Fixtures are removed by PATH, one by one, and directories only when this
# harness created them.
#
# The version this replaces ended its run with
#     for d in ("TESTDIR", "EMPTYDIR", ..., "HGR"): shutil.rmtree(d)
# and sdcard/HGR/ is a SHIPPED directory holding nine committed HGR images
# (Lezard, UBERNIE, alien, dragon, gobelin, maze3D, ours, tiger, N001). Running
# the test deleted them from the working tree. Nobody had noticed, for the
# reason this whole conversion exists: the test never ran. A harness may only
# remove what it made.
FIXTURE_FILES = (
    Path("HELLO.TXT"), Path("SMALL#060300"), Path("TESTBIN"),
    Path("DELME.TXT"), Path("DELME2.TXT"), Path("WTEST"), Path("STEST#060400"),
    Path("TESTDIR") / "FILE1#F10800",
    Path("NOTEMPTY") / "KEEP.TXT",
    Path("HGR") / "N000#062000",
    Path("HGR") / "PIC#062000",
    Path("HGR") / "DELTAG#060280",
)

# Removed only if empty, deepest first — so a shipped sibling, or anything the
# firmware created that we did not, survives.
FIXTURE_DIRS = (
    Path("TESTDIR") / "SUB1", Path("TESTDIR"),
    Path("EMPTYDIR"), Path("NOTEMPTY"),
    Path("NEWDIR"), Path("TEMPDIR"), Path("MDTEST"), Path("A"),
    Path("HGR"),
)


def prepare_fixtures() -> None:
    (SDCARD / "HELLO.TXT").write_text("HELLO WORLD FROM APPLE 1\r\n"
                                      "THIS IS A TEST FILE\r\n")
    (SDCARD / "SMALL#060300").write_bytes(SMALL_PROGRAM)
    (SDCARD / "TESTBIN").write_bytes(bytes(range(256)))
    for name in ("DELME.TXT", "DELME2.TXT"):
        (SDCARD / name).write_text("DELETE ME\r\n")

    (SDCARD / "TESTDIR" / "SUB1").mkdir(parents=True, exist_ok=True)
    (SDCARD / "TESTDIR" / "FILE1#F10800").write_bytes(b"\x00" * 16)
    (SDCARD / "EMPTYDIR").mkdir(exist_ok=True)
    (SDCARD / "NOTEMPTY").mkdir(exist_ok=True)
    (SDCARD / "NOTEMPTY" / "KEEP.TXT").write_text("KEEP\r\n")

    # HGR ships with nine images of its own; these two are ours, and phase 1
    # asserts against them by name rather than against whatever the card holds.
    hgr = SDCARD / "HGR"
    hgr.mkdir(exist_ok=True)
    for name in ("N000#062000", "PIC#062000"):
        (hgr / name).write_bytes(b"\xAA" * 8192)
    # For the DEL-by-display-name regression (4.17): on disk DELTAG#060280,
    # typed as `DEL DELTAG`.
    (hgr / "DELTAG#060280").write_bytes(b"\x00" * 16)


def cleanup_fixtures() -> None:
    for rel in FIXTURE_FILES:
        p = SDCARD / rel
        if p.is_file():
            p.unlink()
    for rel in FIXTURE_DIRS:
        p = SDCARD / rel
        if p.is_dir() and not any(p.iterdir()):
            p.rmdir()


class Sd:
    """The firmware, driven one command at a time.

    WAITING ON THE PROMPT IS NOT A SUBSTRING SEARCH. The SD CARD OS prompt is
    the current path followed by '>' — "/>", "/HGR>" — and a DIR listing is
    full of lines ending in "<DIR>". Anchoring on ">" therefore matched the
    FIRST directory entry and returned a one-line "listing", after which every
    later assertion read the previous command's leftovers. What identifies a
    prompt is not the character but its POSITION: it is what the machine has
    printed when it has stopped printing. So this waits for the output to go
    quiet AND end in '>'. Still a condition rather than a clock — the old
    harness's fixed read_t of 8 to 15 seconds per command is what this replaces
    — and it costs one cheap `screen` round-trip every 50 ms.
    """

    def __init__(self, m: Pom1) -> None:
        self.m = m

    def _wait_prompt(self, timeout_ms: int, quiet_ms: int = 250) -> str:
        deadline = time.time() + timeout_ms / 1000.0
        last, stable_since = None, time.time()
        while time.time() < deadline:
            cur = self.m.screen()
            if cur != last:
                last, stable_since = cur, time.time()
            elif (cur.rstrip().endswith(">")
                  and (time.time() - stable_since) * 1000 >= quiet_ms):
                return cur
            time.sleep(0.05)
        raise Pom1Error(
            f"SD CARD OS never came back to a prompt within {timeout_ms}ms; "
            f"saw: {(last or '')[-300:]!r}")

    def enter(self, timeout_ms: int = 20000) -> str:
        self.m.monitor()
        self.m.screen_clear()
        self.m.type_line("8000R")
        return self._wait_prompt(timeout_ms)

    def cmd(self, line: str, timeout_ms: int = 20000) -> str:
        self.m.screen_clear()
        self.m.type_line(line)
        return self._wait_prompt(timeout_ms)


def recover_from_test(sd: "Sd", m: Pom1, attempts: int = 8) -> None:
    """Get the firmware back after the TEST loop.

    The MCU needs its idle timeout to drop out of TEST_ECHO, and until it does
    every SD command answers "?I/O ERROR". **A prompt is not evidence that it is
    back**: the firmware prints one over the error, so both an unconditional
    sleep and a wait-for-prompt let the harness march on — which is what made
    the whole of phases 2-4 fail with I/O errors, reproducibly but not every
    run. Re-enter and probe with a command whose success is unambiguous, until
    it succeeds. A condition, not a clock, like everywhere else here.
    """
    for _ in range(attempts):
        out = sd.enter()
        if "?I/O ERROR" not in out.upper() and ROOT_PROMPT in out:
            probe = sd.cmd("PWD")
            if "?I/O ERROR" not in probe.upper():
                return
        time.sleep(0.5)
    raise Pom1Error("SD CARD OS never left TEST_ECHO after the TEST loop")


def main() -> int:
    if not SDCARD.is_dir():
        SDCARD.mkdir(parents=True)

    cleanup_fixtures()      # from a previous failed run
    prepare_fixtures()

    c = Checks("SD CARD OS -- full command set")
    try:
        with Pom1(preset=8, verbose=VERBOSE) as m:
            sd = Sd(m)

            # ---------------- Phase 0 ----------------
            print("\nPhase 0: firmware entry")
            out = sd.enter()
            c.contains("0.1 SD CARD OS banner", out, "SD CARD OS")
            c.contains("0.2 prompt at root", out, ROOT_PROMPT)

            # ---------------- Phase 1: read-only ----------------
            print("\nPhase 1: read-only commands")
            c.contains("1.1 ? lists commands", sd.cmd("?"), "COMMANDS")
            out = sd.cmd("HELP SAVE")
            c.contains("1.2 HELP SAVE shows syntax", out, "SYNTAX")
            c.contains("1.3 HELP SAVE mentions SAVE FILENAME", out, "SAVE FILENAME")
            c.contains("1.4 HELP DIR shows syntax", sd.cmd("HELP DIR"), "SYNTAX")
            c.contains("1.5 PWD at root", sd.cmd("PWD"), "/")
            c.contains("1.6 DIR lists HGR", sd.cmd("DIR"), "HGR")
            c.contains("1.7 LS lists HGR", sd.cmd("LS"), "HGR")
            out = sd.cmd("DIR HGR")
            c.contains("1.8 DIR HGR shows N000", out, "N000")
            c.contains("1.9 DIR HGR shows the BIN type", out, "BIN")
            c.contains("1.10 DIR HGR shows the load address", out, "$2000")
            c.contains("1.11 DIR HGR shows the size", out, "8192")
            c.contains("1.12 LS HGR shows the size", sd.cmd("LS HGR"), "8192")
            c.contains("1.13 LS marks directories", sd.cmd("LS"), "<DIR>")

            # TEST loops until ESC: the ROM sends 0x00-0xFF to the MCU, which
            # echoes each byte XOR 0xFF, and a '*' marks each verified pass.
            m.screen_clear()
            m.type_line("TEST")
            out = m.expect("TESTING", timeout_ms=8000)
            c.contains("1.14 TEST starts", out, "TESTING")
            star = m.try_expect("*", 8000)
            c.ok("1.15 TEST verifies a full 256-byte pass", star is not None,
                 "no '*' within 8 s")
            c.excludes("1.16 TEST reports no transfer error",
                       (star or "") + m.screen(), "TRANSFER ERROR")
            m.key("\x1b")               # ESC leaves the loop
            recover_from_test(sd, m)

            out = sd.cmd("TYPE HELLO.TXT")
            c.contains("1.17 TYPE shows the first line", out, "HELLO WORLD")
            c.contains("1.18 TYPE shows the second line", out, "THIS IS A TEST FILE")
            c.contains("1.19 DUMP shows hex bytes", sd.cmd("DUMP SMALL#060300"), "A9")
            c.excludes("1.20 MOUNT reports no error", sd.cmd("MOUNT"), "ERROR")
            out = sd.cmd("BAS")
            c.ok("1.21 BAS answers", "LOMEM" in out.upper() or "NO BASIC" in out.upper(),
                 repr(out))
            # MOUNT must reset the working directory to the root.
            sd.cmd("CD HGR")
            c.excludes("1.22 MOUNT from a subdir reports no error", sd.cmd("MOUNT"), "ERROR")
            c.contains("1.23 MOUNT returns to root", sd.cmd("PWD"), "/")

            # ---------------- Phase 2: navigation ----------------
            print("\nPhase 2: directory navigation")
            sd.cmd("CD /")
            c.excludes("2.1 CD HGR accepted", sd.cmd("CD HGR"), "NOT FOUND")
            c.contains("2.2 PWD shows /HGR", sd.cmd("PWD"), "/HGR")
            c.contains("2.3 DIR in HGR shows PIC", sd.cmd("DIR"), "PIC")
            c.excludes("2.4 CD .. accepted", sd.cmd("CD .."), "NOT FOUND")
            c.contains("2.5 PWD back at root", sd.cmd("PWD"), "/")
            sd.cmd("CD TESTDIR")
            sd.cmd("CD SUB1")
            c.contains("2.6 nested CD reaches /TESTDIR/SUB1", sd.cmd("PWD"), "/TESTDIR/SUB1")
            c.contains("2.7 CD .. climbs to /TESTDIR", sd.cmd("CD ..") + sd.cmd("PWD"),
                       "/TESTDIR")
            sd.cmd("CD ..")
            c.contains("2.8 CD .. climbs to the root", sd.cmd("PWD"), "/")
            c.excludes("2.9 fuzzy CD hgr accepted", sd.cmd("CD hgr"), "NOT FOUND")
            c.contains("2.10 fuzzy CD resolves to /HGR", sd.cmd("PWD"), "/HGR")
            sd.cmd("CD /")
            c.contains("2.11 CD to a missing directory is refused",
                       sd.cmd("CD NOSUCHDIR"), "PATH NOT FOUND")
            sd.cmd("CD HGR")
            c.excludes("2.12 absolute CD /TESTDIR accepted", sd.cmd("CD /TESTDIR"), "NOT FOUND")
            c.contains("2.13 PWD shows /TESTDIR", sd.cmd("PWD"), "/TESTDIR")
            sd.cmd("CD /")
            c.excludes("2.14 trailing-slash CD HGR/ accepted", sd.cmd("CD HGR/"), "NOT FOUND")
            c.contains("2.15 CD HGR/ resolves to /HGR", sd.cmd("PWD"), "/HGR")
            sd.cmd("CD /")

            # ---------------- Phase 3: reads ----------------
            print("\nPhase 3: file reads")
            sd.cmd("CD HGR")
            out = sd.cmd("LOAD PIC", timeout_ms=25000)
            c.contains("3.1 LOAD PIC found", out, "FOUND")
            c.contains("3.2 LOAD PIC names the tagged file", out, "PIC#062000")
            out = sd.cmd("LOAD N00", timeout_ms=25000)
            c.contains("3.3 LOAD N00 fuzzy-matches", out, "FOUND")
            c.contains("3.4 fuzzy match resolved to N000", out, "N000")
            c.contains("3.5 LOAD of a missing file is refused",
                       sd.cmd("LOAD NONEXIST"), "FILE NOT FOUND")
            sd.cmd("CD /")

            out = sd.cmd("DUMP TESTBIN", timeout_ms=30000)
            c.contains("3.6 DUMP shows the first bytes", out, "00 01 02")
            c.contains("3.7 DUMP reaches the $80 region", out, "80 81 82")
            m.poke(0x0400, [0xFF] * 8)
            sd.cmd("READ TESTBIN 0400", timeout_ms=20000)
            c.ok("3.8 READ landed the file's bytes at $0400",
                 m.peek(0x0400, 4) == bytes([0x00, 0x01, 0x02, 0x03]),
                 f"got {m.peek(0x0400, 4).hex(' ')}")

            # RUN loads and executes; the program prints OK and returns to the
            # Monitor, so the SD CARD OS prompt does not come back on its own.
            m.screen_clear()
            m.type_line("RUN SMALL")
            out = m.expect("FOUND", timeout_ms=15000)
            c.contains("3.9 RUN SMALL found the program", out, "FOUND")
            c.ok("3.10 RUN SMALL executed and printed OK",
                 m.try_expect("OK", 10000) is not None, repr(m.screen()))

            sd.enter()
            out = sd.cmd("LOAD SMALL#060300", timeout_ms=15000)
            c.contains("3.11 LOAD by full tagged name found", out, "FOUND")
            c.contains("3.12 LOAD by tagged name echoes it", out, "SMALL#060300")
            sd.cmd("CD HGR")
            m.poke(0x2000, [0x00] * 4)
            sd.cmd("READ PIC#062000 2000", timeout_ms=30000)
            c.ok("3.13 READ in a subdirectory landed at $2000",
                 m.peek(0x2000, 4) == bytes([0xAA] * 4),
                 f"got {m.peek(0x2000, 4).hex(' ')}")
            sd.cmd("CD /")

            # ---------------- Phase 4: writes ----------------
            print("\nPhase 4: file writes")
            c.excludes("4.1 MKDIR NEWDIR reports no error", sd.cmd("MKDIR NEWDIR"), "ERROR")
            c.ok("4.2 NEWDIR exists on the host", (SDCARD / "NEWDIR").is_dir())
            c.contains("4.3 NEWDIR appears in LS", sd.cmd("LS"), "NEWDIR")
            c.contains("4.4 MKDIR of an existing name is refused",
                       sd.cmd("MKDIR NEWDIR"), "ALREADY EXISTS")
            c.contains("4.5 MKDIR does not create nested paths",
                       sd.cmd("MKDIR A/B"), "FAILED")
            c.excludes("4.6 RMDIR EMPTYDIR reports no error", sd.cmd("RMDIR EMPTYDIR"), "ERROR")
            c.ok("4.7 EMPTYDIR removed on the host", not (SDCARD / "EMPTYDIR").exists())
            c.contains("4.8 RMDIR of a non-empty directory is refused",
                       sd.cmd("RMDIR NOTEMPTY"), "NOT EMPTY")
            c.contains("4.9 RMDIR of a missing directory is refused",
                       sd.cmd("RMDIR XYZNODIR"), "PATH NOT FOUND")
            sd.cmd("MD TEMPDIR")
            c.ok("4.10 MD alias creates the directory", (SDCARD / "TEMPDIR").is_dir())
            sd.cmd("RD TEMPDIR")
            c.ok("4.11 RD alias removes it", not (SDCARD / "TEMPDIR").exists())
            sd.cmd("DEL DELME.TXT")
            c.ok("4.12 DEL removes the host file", not (SDCARD / "DELME.TXT").exists())
            sd.cmd("RM DELME2.TXT")
            c.ok("4.13 RM alias removes the host file", not (SDCARD / "DELME2.TXT").exists())
            c.contains("4.14 DEL of a missing file is refused",
                       sd.cmd("DEL NOSUCHFILE"), "FILE NOT FOUND")
            c.contains("4.15 DEL of a directory is refused",
                       sd.cmd("DEL HGR"), "IS A DIRECTORY")

            # Regression: cmdDel used to demand an exact filename, so `DEL PIC`
            # failed on a file stored as PIC#062000. It now falls back to the
            # same fuzzy match LOAD uses.
            deltag = SDCARD / "HGR" / "DELTAG#060280"
            c.ok("4.16 DELTAG fixture present", deltag.is_file())
            sd.cmd("CD HGR")
            c.excludes("4.17 DEL by display name accepted", sd.cmd("DEL DELTAG"),
                       "FILE NOT FOUND")
            c.ok("4.18 DELTAG#060280 removed on the host", not deltag.exists())
            sd.cmd("CD /")

            # WRITE / SAVE take their bytes from RAM. poke plants them directly
            # instead of exiting to the Monitor to type them in.
            m.poke(0x0400, b"HELLO")
            sd.cmd("WRITE WTEST 0400 0404", timeout_ms=15000)
            wtest = SDCARD / "WTEST"
            c.ok("4.19 WTEST written to the host", wtest.is_file())
            c.ok("4.20 WTEST holds the right bytes",
                 wtest.is_file() and wtest.read_bytes() == b"HELLO",
                 f"got {wtest.read_bytes()!r}" if wtest.is_file() else "missing")
            sd.cmd("WRITE WTEST 0400 0402", timeout_ms=15000)
            c.ok("4.21 WRITE overwrites in place",
                 wtest.is_file() and wtest.read_bytes() == b"HEL",
                 f"got {wtest.read_bytes()!r}" if wtest.is_file() else "missing")
            sd.cmd("SAVE STEST 0400 0404", timeout_ms=15000)
            c.ok("4.22 SAVE writes a tagged file",
                 (SDCARD / "STEST#060400").is_file())
            sd.cmd("DEL WTEST")
            sd.cmd("DEL STEST#060400")
            sd.cmd("RMDIR NEWDIR")

            # ---------------- Phase 5: exit and re-entry ----------------
            print("\nPhase 5: EXIT and re-entry")
            m.screen_clear()
            m.type_line("EXIT")
            c.contains("5.1 EXIT says BYE", m.expect("BYE", timeout_ms=8000), "BYE")
            c.contains("5.2 8000R re-enters the firmware", sd.enter(), "SD CARD OS")

            # ---------------- Phase 6: error cases ----------------
            print("\nPhase 6: error cases")
            c.excludes("6.1 an empty command is harmless", sd.cmd(""), "ERROR")
            c.contains("6.2 an unknown command is rejected", sd.cmd("XYZZY"), "??")
            c.contains("6.3 LOAD without a filename", sd.cmd("LOAD"), "MISSING")
            c.contains("6.4 DEL without a filename", sd.cmd("DEL"), "MISSING")
            c.contains("6.5 CD path traversal is blocked",
                       sd.cmd("CD ../../etc"), "PATH NOT FOUND")
            c.contains("6.6 DIR of a missing path", sd.cmd("DIR NOSUCHPATH"), "PATH NOT FOUND")
            c.contains("6.7 RMDIR on a file is refused",
                       sd.cmd("RMDIR HELLO.TXT"), "NOT A DIRECTORY")
            out = sd.cmd("TYPE ../../etc/passwd")
            c.excludes("6.8 TYPE traversal leaks no host file", out, "root:")
            c.ok("6.9 TYPE traversal is answered with an error",
                 any(k in out.upper() for k in ("?", "IS A DIRECTORY",
                                                "FILE NOT FOUND", "ERROR")),
                 repr(out))
            c.contains("6.10 HELP for an unknown command",
                       sd.cmd("HELP NOSUCHCMD"), "UNKNOWN COMMAND")
            c.contains("6.11 READ of a missing file",
                       sd.cmd("READ NOSUCHFILE 0400"), "FILE NOT FOUND")
    finally:
        cleanup_fixtures()

    return c.summary()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        cleanup_fixtures()
        sys.exit(1)
