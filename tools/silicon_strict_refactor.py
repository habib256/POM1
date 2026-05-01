#!/usr/bin/env python3
"""
silicon_strict_refactor.py — replace inline VDP-store patterns by JSR to the
mutualised helpers in dev/lib/tms9918/tms9918m1.asm. Companion to
silicon_strict_patch.py (which adds NOP padding *inline*); this tool turns
the most common patterns into helper calls instead, so the byte cost of the
silicon-strict timing pads once-per-binary-link in the lib instead of
hundreds-of-times inline in each game.

Patterns refactored:

  P1  LDA #imm
      STA VDP_CTRL
      [optional NOP marker lines]
      LDA #imm
      STA VDP_CTRL
      ↓
      LDX #imm1
      LDY #imm2
      JSR vdp_set_write_xy

  P2  LDA #imm
      STA VDP_DATA
      [optional NOP marker lines]
      ↓
      LDA #imm
      JSR vdp_write_a

Idempotency: the tool refuses to refactor a region that already contains a
`JSR vdp_set_write_xy` or `JSR vdp_write_a` (already done). Run the
silicon_strict_patch.py *--unpatch* first to strip residual inline NOPs from
a partial state if needed.

The caller .asm must arrange for the linker to pull in tms9918m1.o (already
the case for every project that uses init_vdp_g1, clear_name_table, etc.).
Add `.import vdp_write_a, vdp_set_write_xy` to the project header if
missing — the script appends those imports automatically when it inserts
its first JSR.

Usage:
    silicon_strict_refactor.py <file.asm> [--dry-run]
"""
from __future__ import annotations
import argparse
import pathlib
import re
import sys

NOP_MARKER = "silicon-strict gap"

RE_LABEL  = re.compile(r"^([a-zA-Z_][a-zA-Z0-9_]*):\s*$")
RE_NOPMARK = re.compile(r"^\s*NOP\b.*" + re.escape(NOP_MARKER), re.M)

# Match a 2-imm address-write pair, optionally separated by silicon-strict NOPs
RE_ADDR_PAIR = re.compile(
    r"(?P<lead>(?:^[ \t]*)?)"
    r"LDA\s+#(?P<lo>\$?[0-9A-Fa-f]+|[A-Za-z_][A-Za-z0-9_]*)\s*\n"
    r"\s+STA\s+VDP_CTRL\s*\n"
    r"(?:\s+NOP[^\n]*" + re.escape(NOP_MARKER) + r"[^\n]*\n)*"
    r"(?P<lead2>[ \t]+)LDA\s+#(?P<hi>\$?[0-9A-Fa-f]+|[A-Za-z_][A-Za-z0-9_]*)\s*\n"
    r"\s+STA\s+VDP_CTRL\b",
    re.M,
)

# Match a single LDA #imm / STA VDP_DATA, optionally followed by silicon-strict NOPs.
RE_LDA_IMM_STA_DATA = re.compile(
    r"(?P<lead>[ \t]+)LDA\s+#(?P<imm>\$?[0-9A-Fa-f]+|[A-Za-z_][A-Za-z0-9_]*)\s*\n"
    r"\s+STA\s+VDP_DATA\s*\n"
    r"(?P<trailing_nops>(?:\s+NOP[^\n]*" + re.escape(NOP_MARKER) + r"[^\n]*\n)*)",
    re.M,
)

IMPORT_LINE = ".import vdp_write_a, vdp_set_write_xy"


def refactor_addr_pairs(src: str) -> tuple[str, int]:
    def repl(m: re.Match) -> str:
        lead = m.group("lead2")  # use indent of second LDA
        lo = m.group("lo")
        hi = m.group("hi")
        # If hi already encodes the $40 write flag (e.g., $58 for $1800), strip it.
        # The helper ORs $40 itself, so caller passes the bare high byte.
        # Detect literals like "$58", "$5B" etc. and remove the $40 if set.
        if hi.startswith("$"):
            try:
                v = int(hi[1:], 16)
                if v & 0x40:
                    hi = f"${v & 0x3F:02X}"
            except ValueError:
                pass
        return (
            f"{lead}LDX     #{lo}\n"
            f"{lead}LDY     #{hi}\n"
            f"{lead}JSR     vdp_set_write_xy"
        )
    new_src, n = RE_ADDR_PAIR.subn(repl, src)
    return new_src, n


def refactor_lda_sta_data(src: str) -> tuple[str, int]:
    def repl(m: re.Match) -> str:
        lead = m.group("lead")
        imm = m.group("imm")
        return (
            f"{lead}LDA     #{imm}\n"
            f"{lead}JSR     vdp_write_a\n"
        )
    new_src, n = RE_LDA_IMM_STA_DATA.subn(repl, src)
    return new_src, n


def ensure_import(src: str) -> str:
    if "vdp_write_a" in src and "vdp_set_write_xy" in src and ".import" in src:
        return src  # already imported (most likely)
    if "vdp_set_write_xy" not in src and "vdp_write_a" not in src:
        return src  # no JSR to those helpers added (no need to import)
    # Insert .import after the first .importzp or near the top exports.
    lines = src.splitlines(keepends=True)
    inserted = False
    out = []
    for i, l in enumerate(lines):
        out.append(l)
        if not inserted and l.lstrip().startswith(".import") and "vdp_write_a" not in l:
            out.append(f"{IMPORT_LINE}\n")
            inserted = True
    if not inserted:
        # Fall back: prepend at start of file
        return f"{IMPORT_LINE}\n\n" + src
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", type=pathlib.Path)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    src = args.file.read_text()
    new_src = src

    new_src, n_addr = refactor_addr_pairs(new_src)
    new_src, n_data = refactor_lda_sta_data(new_src)

    if n_addr or n_data:
        new_src = ensure_import(new_src)

    delta = len(new_src.splitlines()) - len(src.splitlines())
    print(f"{args.file}:")
    print(f"  refactored {n_addr:3d}  addr-pair → JSR vdp_set_write_xy")
    print(f"  refactored {n_data:3d}  LDA #imm/STA VDP_DATA → JSR vdp_write_a")
    print(f"  net delta {delta:+4d} lines")

    if not args.dry_run and (n_addr or n_data):
        args.file.write_text(new_src)
    return 0


if __name__ == "__main__":
    sys.exit(main())
