#!/usr/bin/env python3
"""test_logo_telnet.py -- Smoke test for the P-LAB LOGO V2.6 interpreter.

Boots a headless POM1 on preset 9 (P-LAB Apple-1 with TMS9918 + CodeTank) with
the BASIC_LOGO cartridge in the lower jumper, starts it with `4000R`, and drives
the turtle over the scripting control channel.

Verifies:
  - the banner "APPLE-1 LOGO V2.6" appears and the "? " prompt follows;
  - TR / FD / REPEAT / PU / PD are accepted (each answers OK);
  - an unknown word raises "UNK CMD";
  - BYE returns to a live Woz Monitor (it answers a memory dump);
  - the standalone image under software/ runs the same words on its own machine.

WHICH LOGO THIS DRIVES
    Both shipped copies, on the two machines each needs:

      * the BASIC_LOGO cartridge (preset 9), which CLAUDE.md names as the home
        of LOGO V2.6 (`-D CODETANK_BUILD`) and which
        tools/verify_codetank_roms.py gates before any EPROM burn;
      * the standalone image under software/Graphic TMS9918/, on preset 10 with
        two cards unplugged (phase 8).

    It used to load `build/TMS_Logo.bin`, built by
    `software/tms9918/emit_TMS_Logo_txt.py` — neither has existed since the
    juillet-2026 software/ reorganisation, so the test could not have run even
    by hand.

    Two things about the standalone image, both found by running it and both
    the reason phase 8 spells its machine out:

      * it spans $0280-$2D00, so it needs more than the 8 KB that presets 9 and
        1 give (preset 1 mirrors preset 9 exactly, pinned by
        preset_ram_profiles_smoke) — on those it prints its banner and runs to
        PC=$0000;
      * on preset 10, which has the room, the A1-IO RTC's 65C22 sits at
        $2000-$200F — INSIDE the program. Rotation words still work; the first
        drawing word (FD 30) runs to PC=$0000. That is not a defect in the
        image, it is Parmigiani's one-board rule showing through the
        Multiplexing Fantasy preset, and `--disable a1io` is the whole fix.
        Worth knowing because a GUI user meets it too: the auto-plug rule for
        software/Graphic TMS9918/ evicts storage cards, not the RTC.

The name still says "telnet" because that is what it used to be: it drove the
Terminal Card on a hardcoded port 6502 and slept between commands. It now runs
headless over the control channel (tools/pom1_control.py, src/CommandPort.h) on
a free port, launching and reaping its own emulator, which is what let it into
ctest as `logo_interpreter`.

Run from anywhere: python3 tools/test_logo_telnet.py [-v]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pom1_control import Checks, Pom1, Pom1Error, REPO_ROOT, skip  # noqa: E402

LOGO_ROM = REPO_ROOT / "roms" / "codetank" / "Codetank_BASIC_LOGO.rom"
LOGO_DUMP = REPO_ROOT / "software" / "Graphic TMS9918" / "TMS_Logo_16k.txt"
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

PROMPT = "? "


def main() -> int:
    if not LOGO_ROM.is_file():
        skip(f"{LOGO_ROM} not found — rebuild with tools/build_codetank_rom.py")

    c = Checks("P-LAB LOGO V2.6 -- turtle interpreter smoke")

    with Pom1(preset=9, verbose=VERBOSE,
              extra_args=["--codetank-rom", str(LOGO_ROM),
                          "--codetank-jumper", "lower"]) as m:
        # --- 1: the interpreter comes up ---
        m.monitor()
        out = m.command("4000R", expect=PROMPT, timeout_ms=10000)
        c.contains("1.1 banner LOGO V2.6", out, "APPLE-1 LOGO V2.6")
        c.contains("1.2 first prompt", out, PROMPT)

        # Every word anchors on the prompt that follows it, so the captured text
        # is exactly that word's reply — an error, when it comes, is inside this
        # window and nowhere else. That is what replaced the sleeps.
        def word(name, line, *, timeout_ms=10000):
            out = m.command(line, expect=PROMPT, timeout_ms=timeout_ms)
            c.contains(name, out, "OK")
            return out

        word("2.1 TR 90 accepted",         "TR 90")
        word("3.1 FD 30 accepted",         "FD 30")
        word("4.1 REPEAT square accepted", "REPEAT 4 [FD 50 TR 90]", timeout_ms=20000)
        word("5.1 PU accepted",            "PU")
        word("5.2 PD accepted",            "PD")

        # --- 6: an unknown word must be refused, not silently accepted ---
        # Anchored on the message, NOT on the prompt: LOGO prefixes its error
        # with "? " ("ZZZZ 1\r? UNK CMD\r? "), so a prompt anchor matches that
        # prefix and stops one word short of the very thing being asserted.
        # Re-sync on the real prompt afterwards so step 7 starts clean.
        out = m.command("ZZZZ 1", expect="UNK CMD", timeout_ms=8000)
        c.contains("6.1 unknown word -> UNK CMD", out, "UNK CMD")
        m.expect(PROMPT, timeout_ms=4000)

        # --- 7: BYE hands control back to the Monitor ---
        # Asserted by the Monitor ANSWERING, not by a "\" prompt: the original
        # harness waited for a backslash, but the Apple-1 only echoes one on the
        # Monitor's escape/error path — BYE jumps straight into GETLINE
        # ($FF2C), which prints nothing at all. A machine that answers a memory
        # dump is the stronger claim anyway.
        m.command("BYE")
        out = m.command("FF00.FF03", expect="FF00: D8 58", timeout_ms=8000)
        c.contains("7.1 BYE returns to a live Woz Monitor", out, "FF00: D8 58")

    # --- Phase 8: the standalone image under software/ ---
    # A different machine, so a second instance. Preset 10 is the only shipped
    # TMS9918 profile with room for a $0280-$2D00 program; CodeTank comes off
    # because its cart would autostart its own menu over this, and the A1-IO RTC
    # because its VIA occupies $2000-$200F, inside the program.
    if not LOGO_DUMP.is_file():
        print(f"\nPhase 8: skipped — {LOGO_DUMP} not found")
        return c.summary()

    print("\nPhase 8: the standalone image on preset 10")
    with Pom1(preset=10, disable=["codetank", "a1io"], verbose=VERBOSE,
              extra_args=["--load", f"0280:{LOGO_DUMP}"]) as m:
        out = m.expect(PROMPT, timeout_ms=15000)
        c.contains("8.1 standalone banner LOGO V2.6", out, "APPLE-1 LOGO V2.6")
        c.excludes("8.2 rotation accepted",
                   m.command("TR 90", expect=PROMPT, timeout_ms=10000), "?\r")
        # The word that fails when the RTC shadows the program.
        c.excludes("8.3 drawing accepted",
                   m.command("FD 30", expect=PROMPT, timeout_ms=10000), "?\r")
        c.ok("8.4 the interpreter is still running, not at $0000",
             m.pc() >= 0x0280, f"pc=${m.pc():04X}")

    return c.summary()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Pom1Error as e:
        print(f"\nHARNESS ERROR: {e}")
        sys.exit(1)
