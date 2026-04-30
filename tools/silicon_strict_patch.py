#!/usr/bin/env python3
"""
silicon_strict_patch.py — insert NOP padding into a 6502 .asm so back-to-back
TMS9918 VDP stores respect the silicon-strict access window (8 cycles in
Mode I + sprites). Idempotent: re-running on an already-patched file leaves
it unchanged (we strip our own marker NOPs first, then re-insert).

Rules applied (per dev/SILICONBUGS.md §17 Annexe E):

    A — ST? VDP_*           / ST? VDP_*           → 2 NOP between
    B — ST? VDP_* / LDA #imm / ST? VDP_*           → 1 NOP between
    C — ST? VDP_* / LDA <zp/abs/zp,X> / ST? VDP_*  → 1 NOP between

`ST?` covers STA / STX / STY (a few games use STX VDP_CTRL for fast
two-byte address writes). The TMS9918 access window is shared between
the data port ($CC00) and the control port ($CC01), so DATA→DATA,
CTRL→CTRL, and DATA↔CTRL pairs are all gated identically.

Usage:
    silicon_strict_patch.py <file.asm> [--dry-run]
    silicon_strict_patch.py <file.asm> [--unpatch]   # remove our NOPs only

Skip annotations: any subroutine that runs with display blanked (R1 bit 6 = 0)
or has its own carefully-tuned padding can be skipped by adding a comment
`; SILICON_STRICT_SKIP` on a line *before* the routine label, anywhere in
the routine. The skip applies to the entire labelled function until the
next top-level label.
"""
from __future__ import annotations
import argparse
import pathlib
import re
import sys

NOP_MARKER = "silicon-strict gap"

NOP_BTB  = "        NOP                     ; +2c silicon-strict gap (back-to-back VDP store)\n"
NOP_LDAI = "        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)\n"
NOP_LDAZ = "        NOP                     ; +2c silicon-strict gap (LDA zp/abs bridge)\n"

RE_VDP_STORE = re.compile(r"^\s+ST[AXY]\s+VDP_(DATA|CTRL)\s*$")
RE_LABEL     = re.compile(r"^([a-zA-Z_][a-zA-Z0-9_]*):\s*$")
RE_LDA_IMM   = re.compile(r"^\s*LDA\s+#")
RE_LDA_ANY   = re.compile(r"^\s*LDA\s+[^#]")  # LDA non-immediate (zp / abs / (zp),Y / zp,X / abs,X)


def is_vdp_store(line: str) -> bool:
    return bool(RE_VDP_STORE.match(line.rstrip()))


def is_lda_imm(line: str) -> bool:
    return bool(RE_LDA_IMM.match(line))


def is_lda_any(line: str) -> bool:
    return bool(RE_LDA_ANY.match(line))


def strip_marker_nops(src: list[str]) -> list[str]:
    """Remove every line that we previously inserted (carries our marker)."""
    return [l for l in src if NOP_MARKER not in l]


def find_skip_ranges(src: list[str]) -> list[tuple[int, int]]:
    """Return [(start, end)] line index ranges of routines marked SILICON_STRICT_SKIP.
    A routine is the lines from a top-level `name:` label up to (exclusive)
    the next top-level label. The skip is requested by a comment line
    containing 'SILICON_STRICT_SKIP' anywhere inside that routine."""
    label_lines = [i for i, l in enumerate(src) if RE_LABEL.match(l)]
    if not label_lines:
        return []
    boundaries = label_lines + [len(src)]
    ranges = []
    for k, start in enumerate(label_lines):
        end = boundaries[k + 1] - 1
        body = "".join(src[start:end + 1])
        if "SILICON_STRICT_SKIP" in body:
            ranges.append((start, end))
    return ranges


def next_code_lines(src: list[str], i: int, n: int):
    """Return the next n non-empty / non-comment / non-label lines after i."""
    out = []
    j = i + 1
    while j < len(src) and len(out) < n:
        s = src[j].strip()
        if s and not s.startswith(";") and not RE_LABEL.match(src[j]):
            out.append((j, src[j]))
        j += 1
    return out


def apply_padding(src: list[str]) -> tuple[list[str], dict[str, int]]:
    skip = find_skip_ranges(src)
    def in_skip(idx: int) -> bool:
        return any(s <= idx <= e for s, e in skip)

    out: list[str] = []
    counts = {"A": 0, "B": 0, "C": 0}
    for i, line in enumerate(src):
        out.append(line)
        if in_skip(i) or not is_vdp_store(line):
            continue
        nxt = next_code_lines(src, i, 2)
        if not nxt:
            continue
        n0_text = nxt[0][1]
        n0_kind = is_vdp_store(n0_text)
        if n0_kind:
            out += [NOP_BTB, NOP_BTB]
            counts["A"] += 2
            continue
        if len(nxt) >= 2 and is_vdp_store(nxt[1][1]):
            if is_lda_imm(n0_text):
                out.append(NOP_LDAI)
                counts["B"] += 1
            elif is_lda_any(n0_text):
                out.append(NOP_LDAZ)
                counts["C"] += 1
    return out, counts


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", type=pathlib.Path, help="6502 .asm to patch in-place")
    ap.add_argument("--dry-run", action="store_true",
                    help="print what would change, don't write")
    ap.add_argument("--unpatch", action="store_true",
                    help="strip silicon-strict NOPs and exit (no re-insertion)")
    args = ap.parse_args()

    src = args.file.read_text().splitlines(keepends=True)
    stripped = strip_marker_nops(src)
    removed = len(src) - len(stripped)

    if args.unpatch:
        if not args.dry_run:
            args.file.write_text("".join(stripped))
        print(f"{args.file}: removed {removed} marker NOPs")
        return 0

    patched, counts = apply_padding(stripped)
    inserted = sum(counts.values())
    delta = len(patched) - len(src)

    print(f"{args.file}:")
    print(f"  reverted  {removed:4d} previous marker NOPs")
    print(f"  inserted  {counts['A']:4d}  case A (back-to-back ST? VDP)")
    print(f"  inserted  {counts['B']:4d}  case B (LDA #imm bridge)")
    print(f"  inserted  {counts['C']:4d}  case C (LDA <sym> bridge)")
    print(f"  net delta {delta:+4d} lines  ({inserted} new NOPs)")

    if not args.dry_run:
        args.file.write_text("".join(patched))
    return 0


if __name__ == "__main__":
    sys.exit(main())
