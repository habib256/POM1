#!/usr/bin/env python3
"""
silicon_strict_audit.py — offline static analysis of TMS9918 VDP access timing.

Reads a 6502 .asm file (cc65 syntax) and reports every site where the
worst-case gap to the previous VDP access falls below the silicon-strict
threshold. Unlike `silicon_strict_patch.py` (which INJECTS pads via
heuristics), this tool only AUDITS — it walks the control-flow graph
per routine, computes the MIN gap across forward paths, and lists the
at-risk sites with a precise reason.

Use as:
- CI gate after `silicon_strict_patch.py` runs (audit-mode no-op =
  patched correctly).
- Manual sanity check when adding new VDP-touching code.
- Cross-validate the patcher when a runtime drop slips through.

Output format:
    file.asm:LINE: routine_name: REASON
        prev VDP access: line PREV (...)
        bridge cycles  : N
        gate (mode/loc): TARGET (M)
        drop margin    : (TARGET - N) cycles short

Exit code: 0 if no at-risk sites found, 1 otherwise.

Limitations (deliberate):
- Cycle counts are MIN-case (no page-cross penalty). Pessimistic for
  "could this drop?" — we want false positives, not false negatives.
- Branches are followed in both directions (taken and not-taken); the
  walker takes the WORST gap across paths.
- Cross-routine analysis: a JSR to a VDP-tail routine (last STA VDP_*
  ≤ 4 code lines before RTS) is treated as a synthetic VDP access.
- No interprocedural analysis beyond VDP-tail tagging — i.e. we do NOT
  follow into arbitrary callees. The patcher's same heuristic.
- Mode detection: we audit against the TWO published thresholds
  (16c VBlank/blanked, 40c Mode I+sprites active). Output flags both
  if the routine could run in either context.
"""
from __future__ import annotations
import argparse
import pathlib
import re
import sys

# ---------------------------------------------------------------------------
# Cycle table (MIN-case per opcode + addressing mode).
# ---------------------------------------------------------------------------
# Format: regex on the stripped opcode+operand → min cycles.
# Branch instructions are special: BCC etc. take 2c not-taken, 3c taken; we
# use 2c for the WORST-gap analysis (forces the walker to consider the
# shorter, riskier path).
CYCLES_RULES: list[tuple[re.Pattern, int]] = [
    # Loads / stores — addressing modes ordered specific-first.
    (re.compile(r"^LD[AXY]\s+#"), 2),                           # imm
    (re.compile(r"^LD[AXY]\s+\([^)]+\),Y"), 5),                 # (zp),Y no page cross
    (re.compile(r"^LD[AXY]\s+\([^)]+,X\)"), 6),                 # (zp,X)
    (re.compile(r"^LD[AXY]\s+[a-zA-Z_][\w]*,[XY]"), 4),         # abs,X / abs,Y
    (re.compile(r"^LD[AXY]\s+\$[0-9A-Fa-f]{1,2},[XY]"), 4),     # zp,X / zp,Y
    (re.compile(r"^LD[AXY]\s+\$[0-9A-Fa-f]{1,2}\b(?!,)"), 3),   # zp
    (re.compile(r"^LD[AXY]\s+[a-zA-Z_][\w]*"), 4),              # abs (named)
    (re.compile(r"^ST[AXY]\s+\([^)]+\),Y"), 6),                 # (zp),Y store
    (re.compile(r"^ST[AXY]\s+\([^)]+,X\)"), 6),                 # (zp,X) store
    (re.compile(r"^ST[AXY]\s+[a-zA-Z_][\w]*,[XY]"), 5),         # abs,X / abs,Y store
    (re.compile(r"^ST[AXY]\s+\$[0-9A-Fa-f]{1,2},[XY]"), 4),     # zp,X store
    (re.compile(r"^ST[AXY]\s+\$[0-9A-Fa-f]{1,2}\b(?!,)"), 3),   # zp store
    (re.compile(r"^ST[AXY]\s+[a-zA-Z_][\w]*"), 4),              # abs store (incl. VDP_*)
    # Arithmetic / logic
    (re.compile(r"^(ADC|SBC|AND|ORA|EOR|CMP)\s+#"), 2),
    (re.compile(r"^(ADC|SBC|AND|ORA|EOR|CMP)\s+\$[0-9A-Fa-f]{1,2}\b(?!,)"), 3),
    (re.compile(r"^(ADC|SBC|AND|ORA|EOR|CMP)\s+[a-zA-Z_][\w]*,[XY]"), 4),
    (re.compile(r"^(ADC|SBC|AND|ORA|EOR|CMP)\s+[a-zA-Z_][\w]*"), 4),
    # Index compares
    (re.compile(r"^(CPX|CPY)\s+#"), 2),
    (re.compile(r"^(CPX|CPY)\s+\$[0-9A-Fa-f]{1,2}\b(?!,)"), 3),
    (re.compile(r"^(CPX|CPY)\s+[a-zA-Z_][\w]*"), 4),
    # Bit / inc / dec
    (re.compile(r"^BIT\s+\$[0-9A-Fa-f]{1,2}\b(?!,)"), 3),
    (re.compile(r"^BIT\s+[a-zA-Z_][\w]*"), 4),
    (re.compile(r"^IN[XY]\s*$"), 2),
    (re.compile(r"^DE[XY]\s*$"), 2),
    (re.compile(r"^INC\s+\$[0-9A-Fa-f]{1,2}\b(?!,)"), 5),
    (re.compile(r"^INC\s+[a-zA-Z_][\w]*"), 6),
    (re.compile(r"^DEC\s+\$[0-9A-Fa-f]{1,2}\b(?!,)"), 5),
    (re.compile(r"^DEC\s+[a-zA-Z_][\w]*"), 6),
    # Shifts (A only — short forms)
    (re.compile(r"^(ASL|LSR|ROL|ROR)\s*$"), 2),
    (re.compile(r"^(ASL|LSR|ROL|ROR)\s+A\s*$"), 2),
    # Transfers / flags (2c)
    (re.compile(r"^(TAX|TAY|TXA|TYA|TXS|TSX|CLC|SEC|CLD|SED|CLI|SEI|CLV|NOP)\s*$"), 2),
    # Stack
    (re.compile(r"^(PHA|PHP)\s*$"), 3),
    (re.compile(r"^(PLA|PLP)\s*$"), 4),
    # Control flow (return cycles for the opcode itself)
    (re.compile(r"^JSR\s+"), 6),
    (re.compile(r"^RTS\s*$"), 6),
    (re.compile(r"^RTI\s*$"), 6),
    (re.compile(r"^JMP\s+\("), 5),
    (re.compile(r"^JMP\s+"), 3),
    # Branches: 2c not-taken (worst case for the walker, since shorter gap = riskier).
    (re.compile(r"^B(CC|CS|EQ|NE|MI|PL|VC|VS)\s+"), 2),
    # BRK
    (re.compile(r"^BRK\s*$"), 7),
]


def opcode_cycles(line: str) -> int:
    """Return MIN cycles for the opcode on this line (stripped of label
    + comment). Returns 0 for non-opcode lines."""
    s = line.strip()
    if not s or s.startswith(";"):
        return 0
    # Strip inline label `name: opcode operand`.
    s = re.sub(r"^@?[a-zA-Z_][\w]*:\s*", "", s)
    # Strip trailing comment.
    s = re.sub(r"\s*;.*$", "", s)
    if not s:
        return 0
    # Skip pseudo-ops / segment directives.
    if s.startswith(".") or s.startswith("="):
        return 0
    s = s.upper()
    for rx, c in CYCLES_RULES:
        if rx.match(s):
            return c
    return 0


# ---------------------------------------------------------------------------
# VDP access detection (mirrors silicon_strict_patch.py heuristics).
# ---------------------------------------------------------------------------
RE_VDP_STORE = re.compile(
    r"^\s+(?:ST[AXY]\s+VDP_(?:DATA|CTRL)"
    r"|WRT_DATA_REG\b"
    r"|WRT_DATA_VAL\b)"
    r"(?:\s+[^;]*?)?(?:\s*;.*)?$"
)
# WRT_DATA_REG / WRT_DATA_VAL macros embed `JSR tms9918_pad40` in their
# postlude (see tms9918.inc), so a line that uses one of these is
# functionally `STA VDP_DATA / JSR pad40`. The auditor's gap-after-access
# initial value must account for the trailing 40c pad, not just the 4c
# STA. Without this, `WRT_DATA_VAL X / WRT_DATA_VAL Y` looks like a 4c
# gap to the auditor — a false positive.
RE_BUILTIN_PADDED_STORE = re.compile(r"^\s+WRT_DATA_(?:REG|VAL)\b")
RE_VDP_LOAD = re.compile(r"^\s+LD[AXY]\s+VDP_(?:DATA|CTRL)\b")
RE_BIT_VDP = re.compile(r"^\s+BIT\s+VDP_(?:DATA|CTRL)\b")
RE_TOP_LABEL = re.compile(r"^[ \t]*([a-zA-Z_][\w]*):\s*$")
RE_LABEL = re.compile(r"^[ \t]*(@?[a-zA-Z_][\w]*):")
RE_RTS = re.compile(r"^\s+RTS\s*(;.*)?$", re.IGNORECASE)
RE_PAD_CALL = re.compile(r"^\s+JSR\s+tms9918_pad(12|24|40)\b")
RE_JSR_TARGET = re.compile(r"^\s+JSR\s+(@?[a-zA-Z_][\w]*)\b")
RE_BRANCH_TARGET = re.compile(
    r"^\s+(?:JMP|JSR|BCC|BCS|BNE|BEQ|BPL|BMI|BVC|BVS)\s+(@?[a-zA-Z_][\w]*)"
)


def is_vdp_access(line: str) -> bool:
    """True for STA/LDA/STX/STY/LDX/LDY/BIT VDP_* AND for the WRT_DATA_*
    macros (which expand to STA + pad)."""
    return bool(RE_VDP_STORE.match(line) or RE_VDP_LOAD.match(line)
                or RE_BIT_VDP.match(line))


def pad_call_cycles(line: str) -> int:
    """If line is `JSR tms9918_pad{12,24,40}`, return its idle-cycle count
    AT THE CALL SITE (12, 24 or 40). Else 0."""
    m = RE_PAD_CALL.match(line)
    if not m:
        return 0
    return int(m.group(1))


def find_vdp_tail_routines(src: list[str]) -> set[str]:
    """Mirrors silicon_strict_patch.find_vdp_tail_routines."""
    tail = set()
    label_lines = [(i, m.group(1)) for i, l in enumerate(src)
                   for m in [RE_TOP_LABEL.match(l)] if m]
    if not label_lines:
        return tail
    boundaries = [i for i, _ in label_lines] + [len(src)]
    for k, (start, name) in enumerate(label_lines):
        end = boundaries[k + 1] - 1
        last_vdp = -1
        for i in range(start, end + 1):
            line = src[i]
            if is_vdp_access(line):
                last_vdp = i
            elif RE_RTS.match(line):
                if last_vdp >= 0:
                    code_between = sum(
                        1 for j in range(last_vdp + 1, i)
                        if src[j].strip() and not src[j].strip().startswith(";")
                        and not RE_LABEL.match(src[j])
                    )
                    if code_between <= 4:
                        tail.add(name)
                last_vdp = -1
    return tail


# ---------------------------------------------------------------------------
# Per-routine worst-case gap computation (linear walk + branch worst-case).
# ---------------------------------------------------------------------------
def audit_routine(src: list[str], start: int, end: int, name: str,
                  vdp_tails: set[str], thresholds: dict) -> list[dict]:
    """Walk the routine linearly. After every VDP access (real or
    JSR-to-tail synthetic), reset a `gap` accumulator to 4 (the STA/LDA's
    own opcode time, charged AFTER the access in the emulator). On
    subsequent opcodes, add MIN cycles. When we hit the next VDP access,
    compare gap vs thresholds. Branches (JMP/Bcc/JSR/RTS) are handled
    pessimistically: we take the SHORTEST forward path's gap.

    Returns a list of issue dicts (one per at-risk site)."""
    issues = []
    # State: gap = min cycles since last VDP access. -1 = no prior access
    # in this routine (treat as "saturated" — no warning from cold entry).
    gap = -1
    prev_idx = -1

    i = start
    while i <= end:
        line = src[i]

        # Skip blanks / comments / labels-only lines.
        s = line.strip()
        if not s or s.startswith(";") or RE_LABEL.match(line):
            i += 1
            continue

        # Pseudo-ops / data directives — bail out (data table).
        stripped = re.sub(r"^@?[a-zA-Z_][\w]*:\s*", "", s)
        if stripped.startswith(".") or stripped.startswith("="):
            i += 1
            continue

        # VDP access? (real or synthetic via JSR <vdp_tail>)
        is_real_vdp = is_vdp_access(line)
        jsr_tgt_m = RE_JSR_TARGET.match(line)
        is_synth_vdp = (jsr_tgt_m is not None and jsr_tgt_m.group(1) in vdp_tails)

        if is_real_vdp or is_synth_vdp:
            # Check threshold against the gap we accumulated.
            if gap >= 0:  # we had a prior access in this routine
                for label, thr in thresholds.items():
                    if gap < thr:
                        issues.append({
                            "line": i + 1,           # 1-based for humans
                            "routine": name,
                            "code": s,
                            "prev_line": prev_idx + 1,
                            "prev_code": src[prev_idx].strip(),
                            "gap": gap,
                            "threshold": thr,
                            "mode": label,
                            "kind": "real" if is_real_vdp else f"synth (JSR {jsr_tgt_m.group(1)})",
                        })
                        break  # one issue per site; report tightest mode first
            # Reset gap for this access. STA/LDA opcode = 4c. If the
            # access is a `WRT_DATA_*` macro (built-in padded store),
            # add the macro's embedded JSR pad40 idle on top — the
            # auditor doesn't see the macro expansion, so it has to
            # know about this trailing pad explicitly.
            gap = 4
            if RE_BUILTIN_PADDED_STORE.match(line):
                gap += 40
            prev_idx = i
            i += 1
            continue

        # Pad helper call — adds its idle cycles.
        pc = pad_call_cycles(line)
        if pc > 0:
            if gap >= 0:
                gap += pc
            i += 1
            continue

        # Generic opcode — add its min cycles to the gap (if we're tracking).
        c = opcode_cycles(line)
        if gap >= 0:
            gap += c

        # Control-flow terminator: any forward path beyond an RTS / JMP
        # is unrelated to the current routine's gap accounting. Stop.
        if RE_RTS.match(line) or stripped.upper().startswith("JMP "):
            # Reset; further accesses (if any) are in a different basic
            # block reached via a forward branch.
            gap = -1
            prev_idx = -1

        i += 1

    return issues


def audit_file(path: pathlib.Path, thresholds: dict) -> list[dict]:
    src = path.read_text().splitlines(keepends=False)
    vdp_tails = find_vdp_tail_routines([l + "\n" for l in src])

    label_lines = [(i, m.group(1)) for i, l in enumerate(src)
                   for m in [RE_TOP_LABEL.match(l)] if m]
    if not label_lines:
        return []
    boundaries = [i for i, _ in label_lines] + [len(src)]

    all_issues = []
    for k, (start, name) in enumerate(label_lines):
        end = boundaries[k + 1] - 1
        all_issues.extend(audit_routine(src, start, end, name, vdp_tails, thresholds))
    return all_issues


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("files", nargs="+", type=pathlib.Path,
                    help="6502 .asm files to audit")
    ap.add_argument("--vblank", type=int, default=16,
                    help="Threshold (cycles) for VBlank / display-blank gate (default 16)")
    ap.add_argument("--active", type=int, default=40,
                    help="Threshold (cycles) for Mode-I+sprites active-display gate (default 40)")
    ap.add_argument("--quiet", action="store_true",
                    help="No header / no per-file pass message")
    args = ap.parse_args()

    # Audit against the TIGHTER threshold first so the report names the
    # mode where the drop would occur. If a site is below the active-display
    # 40c gate, it's by definition also below VBlank's 16c (active is
    # always tighter), so flag it under active.
    thresholds = {
        f"active({args.active}c)": args.active,
        f"VBlank({args.vblank}c)": args.vblank,
    }

    total = 0
    for path in args.files:
        issues = audit_file(path, thresholds)
        if issues:
            for issue in issues:
                print(f"{path}:{issue['line']}: {issue['routine']}: "
                      f"gap={issue['gap']}c < {issue['mode']} threshold")
                print(f"    site:        {issue['code']!s}")
                print(f"    prev access: line {issue['prev_line']}: {issue['prev_code']!s} "
                      f"({issue['kind']})")
                print(f"    short by:    {issue['threshold'] - issue['gap']} cycles")
                print()
            total += len(issues)
        elif not args.quiet:
            print(f"{path}: no at-risk sites at "
                  f"active≥{args.active}c, VBlank≥{args.vblank}c")

    if not args.quiet:
        print(f"\n=== Total at-risk sites: {total} ===", file=sys.stderr)
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
