#!/usr/bin/env python3
"""
silicon_strict_patch.py — insert JSR-based padding into a 6502 .asm so back-
to-back TMS9918 VDP stores respect the silicon-strict access window of 16
cycles (paranoid mode, cf. dev/SILICONBUGS.md Bug N°1 §2).

Idempotent: re-running on an already-patched file leaves it unchanged. Both
v1 markers (NOPs) and v2 markers (JSR) are stripped before re-insertion.

Rules applied (per dev/SILICONBUGS.md §17 Annexe E):

    A — ST? VDP_*                 / ST? VDP_*    → 1 JSR tms9918_pad12 between
                                                   gap = 4 + 12 + 4 = 16c
    B — ST? VDP_* / LDA #imm      / ST? VDP_*    → 1 JSR tms9918_pad12 before
                                                   the LDA #imm.
                                                   gap = 4 + 12 + 2 + 4 = 22c
    C — ST? VDP_* / LDA <addr>    / ST? VDP_*    → 1 JSR tms9918_pad12 before
                                                   the LDA addr (zp/abs/zp,X).
                                                   gap = 4 + 12 + 3 + 4 = 23c

`ST?` covers STA / STX / STY (a few games use STX VDP_CTRL for fast
two-byte address writes). The TMS9918 access window is shared between
the data port ($CC00) and the control port ($CC01), so DATA→DATA,
CTRL→CTRL, and DATA↔CTRL pairs are all gated identically.

Why JSR instead of NOPs?
    NOP        = 2 cycles in 1 byte (ratio 2 c/B)
    JSR + RTS  = 12 cycles in 3 bytes at the call site, helper itself = 1 byte
                 (ratio 4 c/B at the call site — half the ROM cost)

The helper `tms9918_pad12` (and `tms9918_pad24` for chained pads) is defined
in `dev/lib/tms9918/tms9918_pad.asm` and must be linked into every project.
Each project's Makefile adds it to EXTRA_ASM.

Usage:
    silicon_strict_patch.py <file.asm> [--dry-run]
    silicon_strict_patch.py <file.asm> [--unpatch]   # strip v1+v2 patches

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

# v1 marker (NOP padding, 8c paranoid). Stripped on every run for backward
# compat — a fresh patch re-inserts the v2 form.
V1_MARKER = "silicon-strict gap"

# v2 marker (JSR tms9918_pad12, 16c paranoid). Distinct enough from v1 that
# strip_marker_lines() removes both safely.
V2_MARKER = "silicon-strict pad16"

# v2 also injects `.import tms9918_pad12` once near the top of any file we
# patch (so cc65 resolves the symbol against tms9918_pad.asm). The marker
# tags the line so --unpatch removes it, and re-patch is idempotent.
V2_IMPORT = "        .import tms9918_pad12  ; silicon-strict pad16 (helper from tms9918_pad.asm)\n"

JSR_BTB  = "        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)\n"
JSR_LDAI = "        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (before LDA #imm bridge)\n"
JSR_LDAZ = "        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (before LDA zp/abs bridge)\n"

RE_VDP_STORE = re.compile(r"^\s+ST[AXY]\s+VDP_(DATA|CTRL)\s*(;.*)?$")
RE_LABEL     = re.compile(r"^[ \t]*(@?[a-zA-Z_][a-zA-Z0-9_]*):\s*$")
# Top-level labels only (no leading @) — used by the SKIP-range walker so a
# `; SILICON_STRICT_SKIP` comment in init_vdp_g2 covers ALL its @local
# labels (@rg, @rg_store, @th, @nm, @cl) until the next TOP-level label.
RE_TOP_LABEL = re.compile(r"^[ \t]*([a-zA-Z_][a-zA-Z0-9_]*):\s*$")
RE_LDA_IMM   = re.compile(r"^\s*LDA\s+#")
RE_LDA_ANY   = re.compile(r"^\s*LDA\s+[^#]")  # LDA non-immediate (zp / abs / (zp),Y / zp,X / abs,X)
# NOPs are treated as transparent fillers when scanning between VDP stores —
# legacy hand-written 8c-strict padding (e.g. hide_slot_4) puts a single NOP
# between two STA VDP_DATA, which gives only 10c gap (< 16c paranoid). The
# next_code_lines() walker skips them so the case detector sees the second
# store and inserts a JSR pad12 to bring the gap up to ≥ 16c.
RE_NOP       = re.compile(r"^\s+NOP\s*$")

# "Bridge" instructions allowed between LDA and the second STA in a case-B/C
# pattern (1-3 cycles each, no side effect on the bus access window). The
# patcher inserts JSR pad12 BEFORE the LDA so the helper's 12c idle clears
# the strict window regardless of how many bridge instructions the
# accumulator munging needs (typical: ORA #$40 to set the write bit before
# committing the address-high byte).
#
# Anything not matching this list breaks the bridge — we don't pad across
# branches, JSR/RTS, JMP, or VDP stores themselves (those are their own case
# detection target).
RE_BRIDGE_OK = re.compile(
    r"^\s+("
    r"ORA|AND|EOR|"            # imm/zp/abs bitwise (1-4c)
    r"CLC|SEC|CLD|SED|CLI|SEI|CLV|"  # flag set/clear (2c)
    r"INX|INY|DEX|DEY|"        # index reg ±1 (2c)
    r"TXA|TYA|TAX|TAY|TXS|TSX|"  # transfers (2c)
    r"PHA|PHP|PLA|PLP|"        # stack (3-4c)
    r"ASL|LSR|ROL|ROR|"        # accumulator shifts (imm form 2c)
    r"ADC|SBC|"                # arithmetic (imm/zp/abs)
    r"CMP|CPX|CPY|"            # compares (2-4c)
    r"BIT|"                    # bit test
    r"LDA|LDX|LDY|"            # extra load between the bridge LDA and the
                                # final STA — common in `LDA / LDY idx / LDA
                                # tbl,Y / STA VDP_DATA` value-builder loops
    r"NOP"                     # explicit
    r")(\s+[^;]*?)?(\s*;.*)?$"
)
# Non-VDP STA/STX/STY also tolerated as bridge (e.g. `LDA / STA tmp / LDA
# tbl,X / STA VDP_DATA`). Distinguish from VDP store via its own regex.
RE_NONVDP_STORE = re.compile(r"^\s+ST[AXY]\s+(?!VDP_)\S")


# Strip an optional inline label prefix `@local: ` or `name: ` from the start
# of the line so the regexes for VDP_STORE / LDA / BRIDGE see the opcode even
# when the label is on the same line as the instruction (cc65 idiom: `@cppat:
# LDA (zp),Y`). Without this, the case detector treats label-prefixed lines
# as opaque "unknown" instructions and bails or triggers the branch-lookahead
# false-positive.
RE_INLINE_LABEL = re.compile(r"^[ \t]*@?[a-zA-Z_][a-zA-Z0-9_]*:[ \t]+")


def strip_inline_label(line: str) -> str:
    return RE_INLINE_LABEL.sub("        ", line, count=1)


def is_vdp_store(line: str) -> bool:
    return bool(RE_VDP_STORE.match(strip_inline_label(line).rstrip()))


def is_lda_imm(line: str) -> bool:
    return bool(RE_LDA_IMM.match(strip_inline_label(line)))


def is_lda_any(line: str) -> bool:
    return bool(RE_LDA_ANY.match(strip_inline_label(line)))


def strip_marker_lines(src: list[str]) -> list[str]:
    """Remove every line that we previously inserted (carries v1 or v2 marker)."""
    return [l for l in src if V1_MARKER not in l and V2_MARKER not in l]


def find_skip_ranges(src: list[str]) -> list[tuple[int, int]]:
    """Return [(start, end)] line index ranges of routines marked SILICON_STRICT_SKIP.
    A routine is the lines from a TOP-level `name:` label up to (exclusive)
    the next top-level label — `@local:` labels do NOT end the range, so a
    skip annotation in a routine covers all its local subroutines.
    The skip is requested by a comment line containing 'SILICON_STRICT_SKIP'
    anywhere inside that routine."""
    label_lines = [i for i, l in enumerate(src) if RE_TOP_LABEL.match(l)]
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
    """Return the next n non-empty / non-comment / non-label / non-NOP lines after i.

    NOPs are skipped (treated as transparent) so case detectors see across
    legacy hand-written padding. This means STA / NOP / STA matches case A
    (the JSR pad12 lands before the second STA, giving 4 + 2 + 12 + 4 = 22c
    gap — over the 16c threshold)."""
    out = []
    j = i + 1
    while j < len(src) and len(out) < n:
        s = src[j].strip()
        if s and not s.startswith(";") and not RE_LABEL.match(src[j]) \
                and not RE_NOP.match(src[j].rstrip()):
            out.append((j, src[j]))
        j += 1
    return out


def apply_padding(src: list[str]) -> tuple[list[str], dict[str, int]]:
    """v2 strategy:
       - Case A: insert JSR tms9918_pad12 between the two ST? VDP_*.
       - Case B/C: insert JSR tms9918_pad12 BEFORE the LDA bridge (so the
         12c idle lands between STA1 and the LDA, giving gap = 4+12+2+4 = 22c
         for B, 4+12+3+4 = 23c for C — well over the 16c paranoid threshold).
         Bridge instructions like ORA/CLC/ADC/INX between the LDA and the
         second STA are tolerated (up to 3 in a row) — common in vdp_set_write
         (`LDA hi / ORA #$40 / STA VDP_CTRL`) and tile-loop address builders.

       Inserting before the LDA (rather than between LDA and STA2) keeps the
       LDA→STA2 distance constant, which matters for code that relies on
       deterministic LDA-to-write latency (none in our codebase, but a safer
       default for portability)."""
    skip = find_skip_ranges(src)
    def in_skip(idx: int) -> bool:
        return any(s <= idx <= e for s, e in skip)

    # Build a map of every label (including @local) to its line index, so the
    # branch-lookahead can follow `JMP @after_N` to the convergence point.
    labels_map: dict[str, int] = {}
    for i_lbl, line_lbl in enumerate(src):
        m_lbl = re.match(r"^[ \t]*(@?[a-zA-Z_][a-zA-Z0-9_]*):", line_lbl)
        if m_lbl:
            labels_map.setdefault(m_lbl.group(1), i_lbl)

    out: list[str] = []
    counts = {"A": 0, "B": 0, "C": 0}

    # Track the indices in `src` where we want to insert a pad BEFORE that line
    # (used for case B/C: the pad goes before the LDA, which we discover when
    # we walk forward from STA1).
    pad_before: dict[int, str] = {}

    # Up to this many bridge instructions allowed between two VDP stores.
    MAX_BRIDGE = 4
    # Forward-search window for the second VDP store across simple control
    # flow (conditional branches + JMP). Used by the "branch lookahead"
    # extension below: if the second VDP store is reachable within this many
    # source lines via at most one branch, we still pad before the LDA — the
    # pad runs unconditionally before the branch decision, so it covers every
    # path to the next VDP write.
    BRANCH_LOOKAHEAD_LINES = 14

    for i, line in enumerate(src):
        if in_skip(i) or not is_vdp_store(line):
            continue
        # Walk forward up to (1 LDA + MAX_BRIDGE bridge + 1 STA) = 6 code lines.
        nxt = next_code_lines(src, i, 1 + MAX_BRIDGE + 1)
        if not nxt:
            continue
        n0_idx, n0_text = nxt[0]
        if is_vdp_store(n0_text):
            # Case A direct — STA / STA back-to-back. Insert pad between.
            pad_before.setdefault(n0_idx, JSR_BTB)
            counts["A"] += 1
            continue
        # Generalized case A: STA / [bridge instructions only] / STA. The
        # patcher inserts pad before the LAST STA (the "second" one), not
        # before the first bridge instruction. This avoids disturbing the
        # caller's data flow and gives gap = 4 + (bridges) + 12 + 4 ≥ 18c.
        bridge_kind = None
        if is_lda_imm(n0_text):
            bridge_kind = "B"
        elif is_lda_any(n0_text):
            bridge_kind = "C"
        elif RE_BRIDGE_OK.match(strip_inline_label(n0_text)) \
                or RE_NONVDP_STORE.match(strip_inline_label(n0_text)):
            bridge_kind = "A"  # generalized case A — non-LDA bridge
        else:
            continue
        # Walk through up to MAX_BRIDGE-1 bridge instructions; stop at first STA VDP.
        sta2_idx = None
        bridge_ended_in_branch = False
        for k in range(1, len(nxt)):
            kk_idx, kk_text = nxt[k]
            if is_vdp_store(kk_text):
                sta2_idx = kk_idx
                break
            kk_stripped = strip_inline_label(kk_text)
            if RE_BRIDGE_OK.match(kk_stripped) or RE_NONVDP_STORE.match(kk_stripped):
                continue
            # Stopped on something that's neither a bridge nor a VDP store.
            # If it's a conditional branch / JMP, scan ahead linearly through
            # source lines for a STA VDP within BRANCH_LOOKAHEAD_LINES — the
            # pad goes BEFORE the LDA at n0_idx, so it runs unconditionally
            # before the branch. This covers patterns like:
            #     STA VDP  / LDA state / BEQ alt / LDA reg,X / JMP cont
            #     alt:                            / LDA #imm
            #     cont: STA VDP
            bridge_ended_in_branch = True
            break
        if sta2_idx is None:
            if bridge_ended_in_branch:
                # Look ahead in raw source lines for a STA VDP. Bound the scan
                # to keep false positives manageable: stop at the next
                # TOP-LEVEL label (function boundary) and at BRANCH_LOOKAHEAD
                # lines. RTS/JMP-out also ends the scan via the top-level
                # label check.
                # +1 so the target STA at distance LOOKAHEAD lines is included.
                #
                # If the branch is an unconditional `JMP @local`, follow it
                # to the label and scan from there too. This catches the
                # slot-convergence pattern `STA / JMP @after_N / @hidden:
                # JSR hide_slot_4 / @after_N: LDA active / BEQ hide / LDA /
                # STA` where the next VDP write is after the JMP target.
                limit = min(len(src), i + BRANCH_LOOKAHEAD_LINES + 1)
                # Scan forward physically.
                for k in range(n0_idx + 1, limit):
                    if RE_TOP_LABEL.match(src[k]):
                        break
                    if is_vdp_store(src[k]):
                        sta2_idx = k
                        break
                # If the branch was an unconditional JMP @local, also scan
                # from the JMP target — the convergence STA may live there.
                if sta2_idx is None:
                    for k in range(n0_idx, min(len(src), i + BRANCH_LOOKAHEAD_LINES + 1)):
                        s = src[k].strip()
                        m = re.match(r'JMP\s+(@?\w+)', strip_inline_label(src[k]).strip())
                        if m and m.group(1) in labels_map:
                            tgt = labels_map[m.group(1)]
                            tgt_limit = min(len(src), tgt + BRANCH_LOOKAHEAD_LINES + 1)
                            for kk in range(tgt, tgt_limit):
                                if kk != tgt and RE_TOP_LABEL.match(src[kk]):
                                    break
                                if is_vdp_store(src[kk]):
                                    sta2_idx = kk
                                    break
                            if sta2_idx is not None:
                                break
            if sta2_idx is None:
                continue
        if bridge_kind == "B":
            pad_before.setdefault(n0_idx, JSR_LDAI)
            counts["B"] += 1
        elif bridge_kind == "C":
            pad_before.setdefault(n0_idx, JSR_LDAZ)
            counts["C"] += 1
        else:  # generalized case A — pad lands right before the second STA.
            pad_before.setdefault(sta2_idx, JSR_BTB)
            counts["A"] += 1

    for i, line in enumerate(src):
        if i in pad_before:
            out.append(pad_before[i])
        out.append(line)

    # Inject `.import tms9918_pad12` near the top once per file (only if we
    # actually inserted any JSR — files with zero pad sites get no import).
    # The .inc may already declare the import; that's fine, ca65 dedupes.
    if sum(counts.values()) > 0 and not any(V2_IMPORT in l for l in out):
        # Find a sensible insertion point: after any leading comment block,
        # before any segment / code line. Land it on the first line that
        # isn't a comment, blank, or a `.` directive other than .segment.
        insert_at = 0
        for i, l in enumerate(out):
            s = l.strip()
            if s and not s.startswith(";"):
                insert_at = i
                break
        out.insert(insert_at, V2_IMPORT)

    return out, counts


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", type=pathlib.Path, help="6502 .asm to patch in-place")
    ap.add_argument("--dry-run", action="store_true",
                    help="print what would change, don't write")
    ap.add_argument("--unpatch", action="store_true",
                    help="strip silicon-strict markers (v1 NOP + v2 JSR) and exit")
    args = ap.parse_args()

    src = args.file.read_text().splitlines(keepends=True)
    stripped = strip_marker_lines(src)
    removed = len(src) - len(stripped)

    if args.unpatch:
        if not args.dry_run:
            args.file.write_text("".join(stripped))
        print(f"{args.file}: removed {removed} marker lines (v1+v2)")
        return 0

    patched, counts = apply_padding(stripped)
    inserted = sum(counts.values())
    delta = len(patched) - len(src)

    print(f"{args.file}:")
    print(f"  reverted  {removed:4d} previous marker lines (v1 NOP + v2 JSR)")
    print(f"  inserted  {counts['A']:4d}  case A (back-to-back ST? VDP)")
    print(f"  inserted  {counts['B']:4d}  case B (LDA #imm bridge)")
    print(f"  inserted  {counts['C']:4d}  case C (LDA <sym> bridge)")
    print(f"  net delta {delta:+4d} lines  ({inserted} new pad sites)")

    if not args.dry_run:
        args.file.write_text("".join(patched))
    return 0


if __name__ == "__main__":
    sys.exit(main())
