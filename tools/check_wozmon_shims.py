#!/usr/bin/env python3
"""check_wozmon_shims.py -- drift gate for the two Wozmon I/O asm shims.

dev/lib/apple1c/apple1io_asm.s and dev/lib/tms9918c/apple1_asm.s carry the
same four Wozmon routines (_woz_putc / _woz_print_hex / _woz_mon /
_apple1_getkey) on purpose: the DevBench copies build sources into a scratch
directory BY BASENAME, so a relative `.include` across lib directories breaks
in-Bench builds, and the tms9918c path is pinned by dev/bench/tms9918c.json,
src/Pom1BenchHost.cpp and tools/build_codetank_rom.py -- a physical merge
costs more than it saves. What almost bit for real was DRIFT: the two shims
historically jumped different Wozmon entries ($FF1F vs $FF1A, unified June
2026). This gate keeps the duplication safe by asserting the shared routine
bodies and equates stay instruction-identical.

Checked:
  - equates ECHO / PRBYTE / WOZMON / KEYCR / KEYDATA have the same values;
  - the normalised instruction stream of each shared routine is identical
    (labels/comments/whitespace ignored; tms9918c's extra _woz_mon_silent
    is allowed -- it is the documented superset).

Run by `make -C dev/lib check`. Exit 0 = in sync.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
A = ROOT / "dev/lib/apple1c/apple1io_asm.s"
B = ROOT / "dev/lib/tms9918c/apple1_asm.s"

SHARED_ROUTINES = ["_woz_putc", "_woz_print_hex", "_woz_mon", "_apple1_getkey"]
SHARED_EQUATES = ["ECHO", "PRBYTE", "WOZMON", "KEYCR", "KEYDATA"]


def parse(path):
    equates = {}
    routines = {}
    cur = None
    for raw in path.read_text().splitlines():
        line = raw.split(";", 1)[0].rstrip()
        if not line.strip():
            continue
        m = re.match(r"(\w+)\s*=\s*(\$[0-9A-Fa-f]+)", line.strip())
        if m:
            equates[m.group(1)] = m.group(2).upper()
            continue
        m = re.match(r"(_\w+):", line)
        if m:
            cur = m.group(1)
            routines[cur] = []
            line = line[m.end():]
        if cur is None:
            continue
        # strip local labels, keep the instruction text
        instr = re.sub(r"^\s*\w+:", "", line).strip()
        if instr and not instr.startswith("."):
            routines[cur].append(re.sub(r"\s+", " ", instr).lower())
    return equates, routines


def main():
    ok = True
    ea, ra = parse(A)
    eb, rb = parse(B)
    for eq in SHARED_EQUATES:
        va, vb = ea.get(eq), eb.get(eq)
        if va != vb:
            print(f"DRIFT equate {eq}: {A.name}={va} vs {B.name}={vb}")
            ok = False
    for rt in SHARED_ROUTINES:
        ba, bb = ra.get(rt), rb.get(rt)
        if ba is None or bb is None:
            print(f"DRIFT routine {rt}: missing in "
                  f"{A.name if ba is None else B.name}")
            ok = False
        elif ba != bb:
            print(f"DRIFT routine {rt}:")
            print(f"  {A.name}: {ba}")
            print(f"  {B.name}: {bb}")
            ok = False
    if ok:
        print(f"wozmon shims in sync ({len(SHARED_ROUTINES)} routines, "
              f"{len(SHARED_EQUATES)} equates)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
