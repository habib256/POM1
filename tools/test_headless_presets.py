#!/usr/bin/env python3
# Headless boot matrix: boots EVERY machine preset with no display and proves
# the whole card fan-out survives. For each preset `--list-presets` reports:
#   POM1 --headless --preset N --exit-after-cycles CYCLES
# and asserts (1) exit status 0, (2) no `[TAG] ERROR` line in the output,
# (3) the CPU parked in the Woz Monitor's keyboard wait ($FF00-$FFFF) — i.e. the
# preset's ROMs + cards + RAM profile let the Monitor reach its prompt.
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


def list_presets(pom1):
    out = subprocess.run([pom1, "--list-presets"], capture_output=True, text=True,
                         timeout=30).stdout
    presets = []
    for m in re.finditer(r"^\s+(\d+):\s+(.*)$", out, re.M):
        presets.append((int(m.group(1)), m.group(2).strip()))
    return presets


def boot(pom1, idx, cycles):
    cmd = [pom1, "--headless", "--preset", str(idx), "--exit-after-cycles", str(cycles)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return False, "timeout (deadlock or the run never returned)", ""
    log = r.stdout + r.stderr
    errors = [ln for ln in log.splitlines() if " ERROR" in ln or ln.startswith("ERROR")]
    m = re.search(r"headless run complete — (\d+) cycles, PC=\$([0-9A-Fa-f]{4})", log)
    if r.returncode != 0:
        return False, f"exit status {r.returncode}", log
    if errors:
        return False, "ERROR lines: " + " | ".join(errors[:3]), log
    if not m:
        return False, "no 'headless run complete' line", log
    pc = int(m.group(2), 16)
    if not (MONITOR_LO <= pc <= MONITOR_HI):
        return False, f"CPU parked at PC=${pc:04X}, not in the Woz Monitor ($FF00-$FFFF)", log
    return True, f"PC=${pc:04X}", log


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
    for idx, name in presets:
        ok, detail, log = boot(a.pom1, idx, a.cycles)
        print(f"{'ok  ' if ok else 'FAIL'} preset {idx:2d} {name:<62} {detail}")
        if not ok:
            failed += 1
            if a.verbose:
                print(log)
    print(f"{len(presets) - failed}/{len(presets)} presets booted headless")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
