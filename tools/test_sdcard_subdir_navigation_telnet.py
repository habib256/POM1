#!/usr/bin/env python3
"""test_sdcard_subdir_navigation_telnet.py -- Pin the SD CARD OS
"commands only search the current directory" invariant that confused a user
typing `LOAD YUM` at `/PLAB>` while YUM actually lived in `/PLAB/MCODE`.

Scenario ("CD before LOAD/DEL"):
  1. Boot headless with --preset 8 (P-LAB microSD + Applesoft Lite).
  2. Drop a throwaway tagged file at sdcard/testdir/HELLO#040300 (harmless
     NOP bytes that load at $0300 without bricking anything).
  3. 8000R -> SD CARD OS prompt at /.
  4. LOAD HELLO at /                -> FILE NOT FOUND.
  5. DEL  HELLO at /                -> FILE NOT FOUND.
  6. CD testdir                     -> ok.
  7. LOAD HELLO in /testdir         -> no FILE NOT FOUND.
  8. DEL  HELLO in /testdir         -> no FILE NOT FOUND, host file gone.
  9. CD ..                          -> back to /.
 10. LOAD HELLO at / again          -> FILE NOT FOUND (doubles as a
     negative-control for the fuzzy matcher — once the file is deleted the
     root is genuinely empty, not just invisibly shadowed).

Audit -- the SD CARD OS commands that resolve names against the CURRENT
DIRECTORY ONLY, with no recursive search. Verified in MicroSD.cpp on
2026-04-20:

  CMD_READ  (0)  / cmdRead   -- strict name match in currentDirectory.
  CMD_LOAD  (4)  / cmdRead   -- same path, fuzzy prefix match.
  CMD_WRITE (1)  / cmdWrite  -- writes into currentDirectory.
  CMD_DIR   (2)  / cmdDir    -- lists currentDirectory (or the arg).
  CMD_LS    (12) / cmdDir    -- same, short format.
  CMD_DEL   (11) / cmdDel    -- deletes from currentDirectory (fuzzy).
  CMD_MKDIR (14) / cmdMkdir  -- creates sub-dir inside currentDirectory.
  CMD_RMDIR (15) / cmdRmdir  -- removes sub-dir inside currentDirectory.

CMD_CD is the ONLY navigation primitive -- it accepts `..`, an absolute
`/PATH`, relative names, and a fuzzy leaf match. Always call it before any of
the eight file-ops above on a file/dir that lives deeper in the tree.

The name still says "telnet" because that is what it used to be: it drove the
Terminal Card on a hardcoded port 6502 and slept 3 s for the boot plus a fixed
delay per command. It now runs headless over the scripting control channel
(tools/pom1_control.py, src/CommandPort.h) on a free port, launching and
reaping its own emulator, which is what let it into ctest as
`sdcard_subdir_navigation`.

Run from anywhere: python3 tools/test_sdcard_subdir_navigation_telnet.py [-v]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT  # noqa: E402

SDCARD_DIR = REPO_ROOT / "sdcard"
TEST_SUBDIR = SDCARD_DIR / "testdir"
# NAME#TTAAAA -- type 04 (BIN), load address $0300. Four EA bytes = NOPs,
# completely inert if the LOAD command decides to transfer them to RAM.
TEST_FILE_NAME = "HELLO#040300"
TEST_FILE_PATH = TEST_SUBDIR / TEST_FILE_NAME
TEST_FILE_CONTENT = b"\xEA\xEA\xEA\xEA"

VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv
PROMPT = ">"


def setup_test_file() -> None:
    TEST_SUBDIR.mkdir(parents=True, exist_ok=True)
    TEST_FILE_PATH.write_bytes(TEST_FILE_CONTENT)


def cleanup_test_file() -> None:
    try:
        if TEST_FILE_PATH.exists():
            TEST_FILE_PATH.unlink()
        if TEST_SUBDIR.exists() and not any(TEST_SUBDIR.iterdir()):
            TEST_SUBDIR.rmdir()
    except OSError:
        pass


def main() -> int:
    cleanup_test_file()   # from a previous failed run
    setup_test_file()

    c = Checks("SD CARD OS -- 'LOAD/DEL need CD first' invariant")
    try:
        with Pom1(preset=8, verbose=VERBOSE) as m:
            print("\nStep 1: launch SD CARD OS (8000R)")
            # Wait for the '/' prompt, not a clock. The firmware needs ~1-2 s to
            # print its banner and initialise cwd to '/'; a command that arrives
            # first gets a stale-cwd '?I/O ERROR' instead of 'FILE NOT FOUND',
            # and the prompt comes back as '>' instead of '/>'. That race is
            # what the old harness's `time.sleep()` calls were guarding against.
            m.monitor()
            out = m.command("8000R", expect="/>", timeout_ms=10000)
            c.contains("1.1 SD CARD OS prompt at /", out, "/>")

            print("\nStep 2: LOAD / DEL at / (file lives in /testdir/)")
            out = m.command("LOAD HELLO", expect=PROMPT, timeout_ms=6000)
            c.contains("2.1 LOAD HELLO at / -> FILE NOT FOUND", out, "FILE NOT FOUND")
            out = m.command("DEL HELLO", expect=PROMPT, timeout_ms=6000)
            c.contains("2.2 DEL HELLO at / -> FILE NOT FOUND", out, "FILE NOT FOUND")

            print("\nStep 3: CD testdir")
            out = m.command("CD TESTDIR", expect=PROMPT, timeout_ms=6000)
            c.excludes("3.1 CD TESTDIR -> no error", out, "NOT FOUND")

            print("\nStep 4: LOAD from /testdir (should succeed)")
            out = m.command("LOAD HELLO", expect=PROMPT, timeout_ms=6000)
            c.excludes("4.1 LOAD HELLO from /testdir -> no FILE NOT FOUND",
                       out, "FILE NOT FOUND")

            print("\nStep 5: DEL from /testdir removes the host file")
            out = m.command("DEL HELLO", expect=PROMPT, timeout_ms=6000)
            c.excludes("5.1 DEL HELLO from /testdir -> no FILE NOT FOUND",
                       out, "FILE NOT FOUND")
            c.ok("5.2 host file gone from disk", not TEST_FILE_PATH.exists(),
                 f"still present: {TEST_FILE_PATH}")

            print("\nStep 6: CD .. and re-verify from the empty root")
            m.command("CD ..", expect=PROMPT, timeout_ms=6000)
            out = m.command("LOAD HELLO", expect=PROMPT, timeout_ms=6000)
            c.contains("6.1 LOAD HELLO at / -> FILE NOT FOUND (post-delete)",
                       out, "FILE NOT FOUND")
    finally:
        cleanup_test_file()

    return c.summary()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        sys.exit(1)
