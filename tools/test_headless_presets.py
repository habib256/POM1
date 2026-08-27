#!/usr/bin/env python3
# Headless boot matrix: boots EVERY machine preset with no display and proves
# the whole card fan-out survives. For each preset `--list-presets` reports:
#   POM1 --headless --preset N --exit-after-cycles CYCLES
# Each preset first loads a three-byte loop and executes exactly one cycle with
# no rendered frame, then performs the normal long boot. Both passes assert
# exit status 0 and no ERROR; the first must remain at $0300, while the long
# boot must park in the Woz Monitor ($FF00-$FFFF).
# Three additional probes type the documented ROM entry command one character
# at a time and assert the bytes emitted through the injected $D012 display
# sink: SD CARD OS, CFFA1 and Krusader must reach their real prompts.
#
# This is the one test that exercises applyHeadlessConfig for all 13 presets:
# the RAM profile, the jumpers, the plug cascade (TMS9918 -> CodeTank, ACI ->
# Extended ACI, microSD -> IEC), the mutex evictions (Parmigiani's rule), every
# card's reset path and its MMIO registration on PeripheralBus. A card whose
# constructor, reset or bus window breaks now fails here instead of on a user.
#
#   python3 tools/test_headless_presets.py [--pom1 path] [--cycles N] [--preset N]...
# Exits 77 (skip) if POM1 is missing. Run from the repo root (ROMs are relative).

import argparse
import os
import re
import subprocess
import sys

DEFAULT_CYCLES = 2_000_000          # ~2 s of emulated time: every ROM is at its prompt
MONITOR_LO, MONITOR_HI = 0xFF00, 0xFFFF
KRUSADER_PRESET = 6
KRUSADER_LO, KRUSADER_HI = 0xE000, 0xFFFF
FIRST_CYCLE_FIXTURE = "tests/fixtures/first_cycle.hex"
CARD_KEYS = (
    "aci", "tms9918", "sid", "sid-se", "microsd", "cffa1", "jukebox",
    "codetank", "wifi", "terminal", "a1io", "pr40", "gt6144", "iec",
    "hgr", "xaci",
)
REQUIRES = {"codetank": {"tms9918"}, "iec": {"microsd"}, "xaci": {"aci"}}
STRICT_CONFLICTS = {
    frozenset(pair) for pair in (
        ("hgr", "a1io"), ("sid-se", "tms9918"), ("sid", "sid-se"),
        ("microsd", "cffa1"), ("jukebox", "cffa1"),
        ("jukebox", "microsd"), ("jukebox", "wifi"),
        ("jukebox", "sid"), ("codetank", "jukebox"),
        ("codetank", "microsd"), ("sid", "tms9918"),
    )
}


def list_presets(pom1):
    out = subprocess.run([pom1, "--list-presets"], capture_output=True, text=True,
                         encoding="utf-8", errors="replace", timeout=30).stdout
    presets = []
    for m in re.finditer(r"^\s+(\d+):\s+(.*?)\s+\[cards=([^]]*)\]\s*$", out, re.M):
        cards = {c for c in m.group(3).split(",") if c}
        presets.append((int(m.group(1)), m.group(2).strip(), cards))
    return presets


def boot(pom1, idx, cycles, *, first_cycle=False, extra_args=(), expect_rejected=False):
    cmd = [pom1, "--headless", "--preset", str(idx)]
    cmd += list(extra_args)
    if first_cycle:
        cmd += ["--load", f"0x0300:{FIRST_CYCLE_FIXTURE}", "--run", "0x0300"]
    cmd += ["--exit-after-cycles", str(cycles)]
    try:
        # encoding is EXPLICIT: text=True alone decodes with the locale's
        # preferred encoding, which is cp1252 on Windows while POM1 prints
        # UTF-8. The em dash in the completion line then arrived as "â€"" and
        # the match below could never succeed — every preset was reported as
        # "no 'headless run complete' line" even though the line was right
        # there in the output. errors="replace" keeps a mojibake byte from
        # raising instead of failing the assertion it was meant to test.
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=120)
    except subprocess.TimeoutExpired:
        return False, "timeout (deadlock or the run never returned)", ""
    log = r.stdout + r.stderr
    if expect_rejected:
        rejected = (r.returncode == 2 and
                    "card override rejected by topology policy" in log)
        return rejected, ("rejected" if rejected else
                          f"expected topology rejection, got status {r.returncode}"), log
    errors = [ln for ln in log.splitlines() if " ERROR" in ln or ln.startswith("ERROR")]
    # \D+ rather than a literal em dash: the assertion is about the cycle
    # count and the PC, not about which dash the message happens to use, and
    # tying a cross-platform test to one non-ASCII character is what broke it.
    m = re.search(r"headless run complete\D+(\d+) cycles, PC=\$([0-9A-Fa-f]{4})", log)
    if r.returncode != 0:
        return False, f"exit status {r.returncode}", log
    if errors:
        return False, "ERROR lines: " + " | ".join(errors[:3]), log
    if not m:
        return False, "no 'headless run complete' line", log
    pc = int(m.group(2), 16)
    if first_cycle and pc != 0x0300:
        return False, f"first instruction left PC=${pc:04X}, expected loop at $0300", log
    expected_boot_range = ((KRUSADER_LO, KRUSADER_HI) if idx == KRUSADER_PRESET
                           else (MONITOR_LO, MONITOR_HI))
    if not first_cycle and not (expected_boot_range[0] <= pc <= expected_boot_range[1]):
        label = "Krusader ROM" if idx == KRUSADER_PRESET else "Woz Monitor"
        return False, (f"CPU parked at PC=${pc:04X}, not in the {label} "
                       f"(${expected_boot_range[0]:04X}-${expected_boot_range[1]:04X})"), log
    return True, f"PC=${pc:04X}", log


def closes_requirements(cards):
    result = set(cards)
    changed = True
    while changed:
        changed = False
        for card in tuple(result):
            for required in REQUIRES.get(card, ()):
                if required not in result:
                    result.add(required)
                    changed = True
    return result


def strict_rejects(cards, candidate):
    target = closes_requirements(set(cards) | {candidate})
    return any(conflict <= target for conflict in STRICT_CONFLICTS)


def prompt_probe(pom1, preset, command, expected):
    cmd = [pom1, "--headless", "--preset", str(preset)]
    for i, char in enumerate(command + "\r", 1):
        cmd += ["--paste-at-cycle", str(i * 100_000), char]
    cmd += ["--exit-after-cycles", "4000000"]
    try:
        run = subprocess.run(cmd, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=120)
    except subprocess.TimeoutExpired:
        return False, "timeout", ""
    log = run.stdout + run.stderr
    marker = "headless display capture: "
    capture = log.split(marker, 1)[1] if marker in log else ""
    ok = run.returncode == 0 and expected in capture
    return ok, (expected if ok else f"missing {expected!r}"), log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pom1", default=os.environ.get("POM1", "build/POM1"))
    ap.add_argument("--cycles", type=int, default=DEFAULT_CYCLES)
    ap.add_argument("--preset", type=int, action="append", help="restrict to these indices")
    ap.add_argument("-v", "--verbose", action="store_true", help="dump the POM1 log on failure")
    a = ap.parse_args()

    if not os.path.exists(a.pom1):
        print(f"SKIP: {a.pom1} not found (build first)"); return 77

    presets = list_presets(a.pom1)
    if not presets:
        print("FAIL: --list-presets returned nothing"); return 1
    if a.preset:
        presets = [p for p in presets if p[0] in a.preset]

    failed = 0
    combination_count = 0
    rejected_count = 0
    for idx, name, cards in presets:
        ok, detail, log = boot(a.pom1, idx, 1, first_cycle=True)
        print(f"{'ok  ' if ok else 'FAIL'} preset {idx:2d} {name:<62} first-cycle {detail}")
        if not ok:
            failed += 1
            if a.verbose:
                print(log)
            continue
        ok, detail, log = boot(a.pom1, idx, a.cycles)
        print(f"{'ok  ' if ok else 'FAIL'} preset {idx:2d} {name:<62} {detail}")
        if not ok:
            failed += 1
            if a.verbose:
                print(log)
        fantasy = "Fantasy" in name
        for card in CARD_KEYS:
            if card in cards:
                continue
            rejected = not fantasy and strict_rejects(cards, card)
            ok, detail, log = boot(
                a.pom1, idx, 1, first_cycle=not rejected,
                extra_args=("--enable", card), expect_rejected=rejected)
            combination_count += 1
            rejected_count += int(rejected)
            print(f"{'ok  ' if ok else 'FAIL'} preset {idx:2d} + {card:<9} {detail}")
            if not ok:
                failed += 1
                if a.verbose:
                    print(log)
    prompt_cases = (
        (8, "8000R", "*** SD CARD OS 1.3"),
        (7, "9006R", "CFFA1>"),
        (KRUSADER_PRESET, "F000R", "KRUSADER 1.3 BY KEN WESSEN"),
    )
    for preset, command, expected in prompt_cases:
        if a.preset and preset not in a.preset:
            continue
        ok, detail, log = prompt_probe(a.pom1, preset, command, expected)
        print(f"{'ok  ' if ok else 'FAIL'} preset {preset:2d} prompt {command:<5} {detail}")
        if not ok:
            failed += 1
            if a.verbose:
                print(log)
    prompt_count = sum(not a.preset or preset in a.preset
                       for preset, _, _ in prompt_cases)
    print(f"{len(presets)} presets booted; {combination_count} absent-card combinations "
          f"checked ({rejected_count} strict rejections); "
          f"{prompt_count} ROM prompts captured")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
