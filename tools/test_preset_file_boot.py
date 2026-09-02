#!/usr/bin/env python3
"""test_preset_file_boot.py -- boot a machine POM1 does not ship.

The end-to-end half of the external-preset format: `preset_file_smoke` proves
the parser turns text into the right MachineConfig; this proves the emulator
then BOOTS that machine, and that a bad description boots nothing at all.

It is also the first test where the two halves of TODO.md's "d'application à
plateforme" meet: the machine is DEFINED from outside (--preset-file) and
DRIVEN from outside (--cmd-port). Neither existed a week ago.

Cases:
  1. a machine that is in no preset table — TMS9918 + CodeTank + microSD, 32 KB,
     Applesoft Lite — boots, and the card windows really answer;
  2. `mode = fantasy` is what allows it: the same file in strict mode is refused;
  3. --enable layers on top of a preset file, in the file's own mode;
  4. every refusal exits non-zero and boots NOTHING — a bad description must not
     silently fall back to a default machine and run the user's program on
     hardware they did not ask for;
  5. a preset file wins over --preset.

Run from anywhere: python3 tools/test_preset_file_boot.py [-v]
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT, pom1_binary  # noqa: E402

VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

# A machine no kMachinePresets[] row describes: the TMS9918 graphic card and the
# microSD storage card on the same bus, with Applesoft Lite in card RAM.
MULTIPLEX = """\
# written by test_preset_file_boot.py
pom1-preset 1
name = Test Multiplex
description = TMS9918 + CodeTank + microSD, which no shipped preset pairs
cards = codetank, microsd
ram = 32
basic = applesoft-lite
mode = fantasy
"""


def write(tmp: Path, name: str, text: str) -> Path:
    p = tmp / name
    p.write_text(text)
    return p


def run_headless(preset: Path, *extra: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(pom1_binary()), "--headless", "--preset-file", str(preset),
         "--exit-after-cycles", "200000", *extra],
        cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=120)


def log(r: subprocess.CompletedProcess) -> str:
    """Both streams. POM1's TeeLogger sends warnings and info to stdout and
    errors to stderr, and a refusal is an error — checking only stdout is how
    this file first reported that a correctly-refused preset said nothing."""
    return r.stdout + r.stderr


def main() -> int:
    c = Checks("External preset files -- a machine POM1 does not ship")
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        good = write(tmp, "multiplex.preset", MULTIPLEX)

        # --- 1: it boots, and the cards are really on the bus ---
        print("\nCase 1: a machine that is in no preset table")
        with Pom1(extra_args=["--preset-file", str(good)], verbose=VERBOSE) as m:
            m.monitor()
            # Applesoft Lite lives in the microSD card's RAM window at $6000.
            # Asserting the BYTES is the point: a preset that reported success
            # and left $6000 empty is exactly the pass a log line cannot catch.
            c.ok("1.1 Applesoft Lite is in microSD card RAM at $6000",
                 m.peek(0x6000, 3) != b"\x00\x00\x00",
                 f"$6000 reads {m.peek(0x6000, 3).hex(' ')}")
            # The CodeTank ROM window answers at $4000 (the cart autostarts a
            # menu from there), which the preset only asked for by naming the
            # daughterboard — dependencies were closed for it.
            c.ok("1.2 the CodeTank ROM window answers at $4000",
                 m.peek(0x4000, 1) != b"\xFF",
                 f"$4000 reads {m.peek(0x4000, 1).hex()}")
            # And the machine is alive.
            c.ok("1.3 the CPU is running in ROM", m.pc() >= 0x4000, f"pc=${m.pc():04X}")

        # --- 2: fantasy is what permits it ---
        print("\nCase 2: the same machine in strict mode is refused")
        strict = write(tmp, "strict.preset", MULTIPLEX.replace("mode = fantasy", ""))
        r = run_headless(strict)
        c.ok("2.1 strict mode refuses the multiplexed bus", r.returncode != 0,
             f"exit {r.returncode}")
        c.ok("2.2 the refusal names the two cards",
             "cannot share the bus" in log(r), log(r)[-300:])
        c.ok("2.3 …and says how to mean it on purpose",
             "mode = fantasy" in log(r), log(r)[-300:])

        # --- 3: --enable layers on top ---
        print("\nCase 3: --enable layers on top of a preset file")
        with Pom1(extra_args=["--preset-file", str(good)], enable=["terminal"],
                  verbose=VERBOSE) as m:
            m.monitor()
            c.ok("3.1 the file's machine is still there",
                 m.peek(0x6000, 3) != b"\x00\x00\x00")
        r = run_headless(good, "--enable", "sid")
        # A fantasy file lets --enable add a card a strict bus would refuse
        # (A1-SID $C800-$CFFF against the TMS9918 at $CC00).
        c.ok("3.2 a fantasy file lets --enable add a multiplexing card",
             r.returncode == 0, f"exit {r.returncode}: {log(r)[-300:]}")

        # --- 4: every refusal boots nothing ---
        print("\nCase 4: a bad description boots nothing")
        for name, text, needle in [
            ("typo.preset",    MULTIPLEX.replace("cards =", "cardz ="), "unknown key"),
            ("badcard.preset", MULTIPLEX.replace("microsd", "microsdd"), "unknown card"),
            ("future.preset",  MULTIPLEX.replace("pom1-preset 1", "pom1-preset 99"),
                               "newer than this POM1 understands"),
            ("noversion.preset", MULTIPLEX.replace("pom1-preset 1\n", ""), "first directive"),
            ("noram.preset",   MULTIPLEX.replace("ram = 32\n", ""), "missing 'ram'"),
        ]:
            r = run_headless(write(tmp, name, text))
            c.ok(f"4.x {name} is refused", r.returncode != 0, f"exit {r.returncode}")
            c.ok(f"4.x {name} says why", needle in log(r), log(r)[-300:])
            c.ok(f"4.x {name} boots no machine",
                 "headless run complete" not in log(r), log(r)[-200:])
        missing = run_headless(tmp / "does-not-exist.preset")
        c.ok("4.z a missing file is refused", missing.returncode != 0)

        # --- 5: a preset file wins over --preset ---
        print("\nCase 5: --preset-file wins over --preset")
        r = run_headless(good, "--preset", "3")     # 3 = Bare Apple-1 (July 1976)
        # Anchored on the APPLY line ("headless preset: ..."), not on any
        # mention: the parser logs the table row it resolved for --preset before
        # the file wins, and that line is not what booted.
        applied = [ln for ln in log(r).splitlines() if "headless preset:" in ln]
        c.ok("5.1 exactly one machine was applied", len(applied) == 1, str(applied))
        c.ok("5.2 …and it is the file's", applied and "Test Multiplex" in applied[0],
             str(applied))

        # --- 6: discovery, and the index a preset gets ---
        print("\nCase 6: a presets/ directory, discovered and indexed")
        pdir = tmp / "presets"
        pdir.mkdir()
        # Deliberately created out of alphabetical order: the loader sorts by
        # filename because the index a preset gets is the key its saved window
        # layout lives under, and directory iteration order is undefined.
        write(pdir, "zulu.preset", MULTIPLEX.replace("Test Multiplex", "Zulu"))
        write(pdir, "alpha.preset", MULTIPLEX.replace("Test Multiplex", "Alpha"))
        listing = subprocess.run(
            [str(pom1_binary()), "--preset-dir", str(pdir), "--list-presets"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60).stdout
        rows = [ln.strip() for ln in listing.splitlines() if ln.strip().startswith(("13:", "14:"))]
        c.ok("6.1 both presets are listed after the built-ins", len(rows) == 2, listing)
        c.ok("6.2 filename order decides the index",
             rows and rows[0].startswith("13:") and "Alpha" in rows[0], str(rows))
        c.ok("6.3 …and the second follows", len(rows) > 1 and "Zulu" in rows[1], str(rows))
        # The shipped thirteen keep their indices whatever the user drops in.
        c.ok("6.4 built-in 12 is still POM1 Fantasy",
             "12: POM1 Apple-1 Multiplexing Fantasy" in listing, listing[-300:])

        # An external preset is selectable by index and by name, like any other.
        r = subprocess.run(
            [str(pom1_binary()), "--preset-dir", str(pdir), "--headless",
             "--preset", "14", "--exit-after-cycles", "200000"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=120)
        c.ok("6.5 --preset 14 boots the discovered machine",
             "headless preset: Zulu" in log(r), log(r)[-300:])
        r = subprocess.run(
            [str(pom1_binary()), "--preset-dir", str(pdir), "--headless",
             "--preset", "alpha", "--exit-after-cycles", "200000"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=120)
        c.ok("6.6 --preset by name resolves an external preset",
             "headless preset: Alpha" in log(r), log(r)[-300:])

        # A broken file is reported and skipped — and takes no slot, so adding
        # one cannot silently renumber the good presets after it.
        write(pdir, "broken.preset", "pom1-preset 1\nname = Broken\ncardz = aci\n")
        listing = subprocess.run(
            [str(pom1_binary()), "--preset-dir", str(pdir), "--list-presets"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60)
        both = listing.stdout + listing.stderr
        c.ok("6.7 a broken file is reported", "unknown key" in both, both[-300:])
        c.ok("6.8 …and occupies no index",
             "13: Alpha" in both and "14: Zulu" in both and "Broken" not in both,
             both[-400:])

        # --preset-dir "" discovers nothing: how a test isolates itself from the
        # developer's own presets/.
        empty = subprocess.run(
            [str(pom1_binary()), "--preset-dir", "", "--list-presets"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60).stdout
        c.ok("6.9 --preset-dir '' discovers nothing",
             "13:" not in empty, empty[-200:])

    return c.summary()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        sys.exit(1)
