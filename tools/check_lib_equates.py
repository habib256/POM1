#!/usr/bin/env python3
"""check_lib_equates.py — pin the constants shared by the asm and C library
tracks so the two cannot silently drift apart.

Companion to tools/build_shared_font.py (Axis 2, fonts): same "one truth, a
--check gate" idea, applied to the OTHER things that are hand-written twice — the
MMIO / ROM-entry equates AND the telemetry side-channel protocol constants.
dev/lib carries two toolchain tracks (asm `.include`-style libs and cc65 C
runtimes); the same magic numbers are declared independently in an asm `.inc`
AND a C header. They rarely change — but when one is edited nothing today flags
the other track left behind. This tool is that flag.

SOURCE OF TRUTH. The asm `.inc` is canonical (lowest level, closest to the bus).
The C side mirrors it for cc65 convenience. EDIT THE `.inc` FIRST, then this
check tells you which C `#define` / pointer constant to follow. The per-track
symbol names sometimes differ (`GEN2_TEXTOFF` vs `GEN2_SS[0]`, `TELE_DATA` vs
`TELE_DATA_REG`), so the correspondence below is an explicit spec — it encodes
WHICH symbols pair up, never their VALUES (those are always read from the files).

Radix: asm writes `$C440` (hex) or a bare decimal (`1`); C writes `0xC440U` or a
bare decimal. Each captured literal is parsed with its own radix (prefix-driven)
and compared as an integer, so `$01` == `0x1` == `1`.

Usage:
    python3 tools/check_lib_equates.py            # report drift, exit 1 if any
    python3 tools/check_lib_equates.py --check    # same (CI alias, mirrors font tool)
    python3 tools/check_lib_equates.py -v         # also print every matched value
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
LIB = REPO / "dev" / "lib"

# A captured literal is one of: $hh (asm hex), 0xhh (C hex), or bare decimal.
LITERAL = r"(\$[0-9A-Fa-f]+|0[xX][0-9A-Fa-f]+|\d+)"


def parse_lit(lit: str) -> int:
    """Prefix-driven radix: $.. and 0x.. are hex, bare digits decimal."""
    if lit.startswith("$"):
        return int(lit[1:], 16)
    if lit[:2].lower() == "0x":
        return int(lit, 16)
    return int(lit, 10)


# Each entry: a human concept, the canonical asm source (file, regex capturing a
# literal in group 1), and one-or-more C mirrors (file, regex). Regexes are
# anchored on the symbol so an unrelated value in the same file can't match.
CHECKS: list = [
    # --- Apple-1 base (apple1.inc  <->  apple1c/apple1io.h) ------------------
    ("Apple1 ECHO (Wozmon char out)",
     ("apple1/apple1.inc",        rf"^\s*ECHO\s*=\s*{LITERAL}"),
     [("apple1c/apple1io.h",      rf"#define\s+ECHO\s+{LITERAL}")]),
    ("Apple1 KBD data",
     ("apple1/apple1.inc",        rf"^\s*KBD\s*=\s*{LITERAL}"),
     [("apple1c/apple1io.h",      rf"#define\s+KBD_DATA\s+{LITERAL}")]),
    ("Apple1 KBDCR (keyboard control)",
     ("apple1/apple1.inc",        rf"^\s*KBDCR\s*=\s*{LITERAL}"),
     [("apple1c/apple1io.h",      rf"#define\s+KBD_CTRL\s+{LITERAL}")]),

    # --- GEN2 HGR card (gen2.inc  <->  gen2c/gen2.h) ------------------------
    ("GEN2 soft-switch base ($C250)",
     ("gen2/gen2.inc",            rf"^\s*GEN2_TEXTOFF\s*=\s*{LITERAL}"),
     [("gen2c/gen2.h",            rf"#define\s+GEN2_SS\b.*?{LITERAL}")]),
    ("GEN2 HIRES page-1 framebuffer",
     ("gen2/gen2.inc",            rf"^\s*GEN2_HGR1\s*=\s*{LITERAL}"),
     [("gen2c/gen2.h",            rf"#define\s+GEN2_HGR1\b.*?{LITERAL}")]),
    ("GEN2 HIRES page-2 framebuffer",
     ("gen2/gen2.inc",            rf"^\s*GEN2_HGR2\s*=\s*{LITERAL}"),
     [("gen2c/gen2.h",            rf"#define\s+GEN2_HGR2\b.*?{LITERAL}")]),

    # --- P-LAB TMS9918 card (tms9918.inc  <->  tms9918c/tms9918.c) ----------
    ("TMS9918 VDP data port",
     ("tms9918/tms9918.inc",      rf"^\s*VDP_DATA\s*=\s*{LITERAL}"),
     [("tms9918c/tms9918.c",      rf"VDP_DATA\s*=.*?{LITERAL}")]),
    ("TMS9918 VDP control/register port",
     ("tms9918/tms9918.inc",      rf"^\s*VDP_CTRL\s*=\s*{LITERAL}"),
     [("tms9918c/tms9918.c",      rf"VDP_REG\s*=.*?{LITERAL}")]),
]

# --- Telemetry side channel (telemetry.inc  <->  telemetry.h) ---------------
# The header promises it "Mirrors telemetry.inc byte-for-byte" — the wire
# protocol (register window, control opcodes, status bits, schema type codes) is
# hand-declared on both sides. Generated here rather than spelled out, since the
# naming is regular: registers gain a `_REG` suffix in C, everything else keeps
# its name. (TELE_ACK is asm-only — the harness's release token, never read by
# the game — so it has no C mirror to pin.)
_TELE_INC = "telemetry/telemetry.inc"
_TELE_H = "telemetry/telemetry.h"


def _asm_eq(name: str):
    return (_TELE_INC, rf"^\s*{name}\s*=\s*{LITERAL}")


def _c_scalar(name: str):
    return (_TELE_H, rf"#define\s+{name}\s+{LITERAL}")


def _c_reg(name: str):
    # C registers wrap the address in a cast expression: grab the first 0x literal.
    return (_TELE_H, rf"#define\s+{name}\b.*?(0[xX][0-9A-Fa-f]+)")


# asm name -> C name is name + "_REG"
_TELE_REGISTERS = ["TELE_DATA", "TELE_CTRL", "TELE_STAT", "TELE_IN", "TELE_INLEN"]
# same name on both tracks
_TELE_SCALARS = [
    "TELE_END", "TELE_LOCKSTEP", "TELE_FREERUN", "TELE_SCHEMA",     # control opcodes
    "TELE_CONNECTED", "TELE_INAVAIL",                              # status bits
    "TELE_T_U8", "TELE_T_S8", "TELE_T_U16",                        # schema type codes
    "TELE_T_S16", "TELE_T_BOOL", "TELE_T_CHAR",
]
for _n in _TELE_REGISTERS:
    CHECKS.append((f"Telemetry {_n} register", _asm_eq(_n), [_c_reg(_n + "_REG")]))
for _n in _TELE_SCALARS:
    CHECKS.append((f"Telemetry {_n}", _asm_eq(_n), [_c_scalar(_n)]))


def extract(rel: str, pattern: str) -> tuple[int | None, str]:
    """Return (integer value, raw match) for the first regex hit, or (None, why)."""
    path = LIB / rel
    if not path.exists():
        return None, f"missing file {rel}"
    text = path.read_text()
    m = re.search(pattern, text, re.MULTILINE | re.DOTALL)
    if not m:
        return None, f"no match for /{pattern}/ in {rel}"
    return parse_lit(m.group(1)), m.group(0).strip()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="CI alias (default behaviour: exit 1 on drift)")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every matched value, not just failures")
    args = ap.parse_args()

    failures: list[str] = []
    for concept, (asm_file, asm_re), mirrors in CHECKS:
        gold, gold_raw = extract(asm_file, asm_re)
        if gold is None:
            failures.append(f"[{concept}] canonical: {gold_raw}")
            continue
        if args.verbose:
            print(f"  0x{gold:<4X} {concept}  ({asm_file})")
        for c_file, c_re in mirrors:
            got, got_raw = extract(c_file, c_re)
            if got is None:
                failures.append(f"[{concept}] {got_raw}")
            elif got != gold:
                failures.append(
                    f"[{concept}] DRIFT: {asm_file} = 0x{gold:X} "
                    f"but {c_file} = 0x{got:X}\n"
                    f"        asm: {gold_raw}\n        C:   {got_raw}")

    if failures:
        print("check_lib_equates: FAIL — asm/C shared constants disagree:\n",
              file=sys.stderr)
        for f in failures:
            print("  " + f, file=sys.stderr)
        print("\nFix: edit the asm .inc (canonical), then sync the C mirror.",
              file=sys.stderr)
        return 1

    print(f"check_lib_equates: ok — {len(CHECKS)} shared constants agree across "
          f"asm/C tracks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
