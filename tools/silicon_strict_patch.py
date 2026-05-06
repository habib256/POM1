#!/usr/bin/env python3
"""
silicon_strict_patch.py — insert JSR-based padding into a 6502 .asm so back-
to-back TMS9918 VDP stores respect the silicon-strict access window of 40
cycles (hardened paranoid mode, cf. dev/SILICONBUGS.md Bug N°1 §2).

Idempotent: re-running on an already-patched file leaves it unchanged. Old
v1 markers (NOPs), v2-pad16 markers (JSR pad12), v2-pad24 markers and
v2-pad40 markers are all stripped before re-insertion.

Rules applied (per dev/SILICONBUGS.md §17 Annexe E):

    A — ST? VDP_*                 / ST? VDP_*    → 1 JSR tms9918_pad40 between
                                                   gap = 4 + 40 + 4 = 48c
    B — ST? VDP_* / LDA #imm      / ST? VDP_*    → 1 JSR tms9918_pad40 before
                                                   the LDA #imm.
                                                   gap = 4 + 40 + 2 + 4 = 50c
    C — ST? VDP_* / LDA <addr>    / ST? VDP_*    → 1 JSR tms9918_pad40 before
                                                   the LDA addr (zp/abs/zp,X).
                                                   gap = 4 + 40 + 3 + 4 = 51c

`ST?` covers STA / STX / STY (a few games use STX VDP_CTRL for fast
two-byte address writes). The TMS9918 access window is shared between
the data port ($CC00) and the control port ($CC01), so DATA→DATA,
CTRL→CTRL, and DATA↔CTRL pairs are all gated identically.

Why JSR instead of NOPs?
    NOP        = 2 cycles in 1 byte (ratio 2 c/B)
    JSR pad40  = 40 cycles in 3 bytes at the call site, helper itself = 7 B
                 (ratio 13.3 c/B at the call site — 6.67× denser than NOP)

The helpers `tms9918_pad12` (legacy 12c), `tms9918_pad24` (legacy 24c) and
`tms9918_pad40` (hardened 40c, current default) are defined in
`dev/lib/tms9918/tms9918_pad.asm` and must be linked into every project.
Each project's Makefile / build_codetank_rom.py auto-links the helper when
its symbol is referenced.

Usage:
    silicon_strict_patch.py <file.asm> [--dry-run]
    silicon_strict_patch.py <file.asm> [--unpatch]   # strip v1+v2(16/24/40) patches

**Strict means strict — no SKIP escape hatch.** Earlier versions honoured
a `; SILICON_STRICT_SKIP` comment to exempt a routine from pad injection
(typically used for register-init loops that run with display blanked).
That escape hatch was removed in May 2026 because (a) it created subtle
footguns when stale comments triggered substring matches and (b) the
"strict mode" contract is meaningless with per-routine exemptions.
Routines that genuinely need different padding (e.g. cross-JSR cushions,
VBlank-sync entry pads) must inline an explicit `JSR tms9918_pad{12,40}`
themselves — their hand-coded pads are recognised by `is_existing_pad`
and the patcher will not double-inject.
"""
from __future__ import annotations
import argparse
import pathlib
import re
import sys

# v1 marker (NOP padding, 8c paranoid). Stripped on every run for backward
# compat — a fresh patch re-inserts the latest v2 form.
V1_MARKER = "silicon-strict gap"

# v2-pad16 marker (legacy JSR tms9918_pad12, 16c paranoid). Stripped so a
# rerun of the patcher re-injects the hardened pad form.
V2_PAD16_MARKER = "silicon-strict pad16"

# v2-pad24 marker (legacy JSR tms9918_pad24, 24c paranoid). Stripped so a
# rerun re-injects the current pad40 form.
V2_PAD24_MARKER = "silicon-strict pad24"

# v2-pad40 marker (current JSR tms9918_pad40, 40c hardened paranoid).
V2_MARKER = "silicon-strict pad40"

# v2 also injects `.import tms9918_pad40` once near the top of any file we
# patch (so cc65 resolves the symbol against tms9918_pad.asm). The marker
# tags the line so --unpatch removes it, and re-patch is idempotent.
V2_IMPORT = "        .import tms9918_pad40  ; silicon-strict pad40 (helper from tms9918_pad.asm)\n"

JSR_BTB  = "        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)\n"
JSR_LDAI = "        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)\n"
JSR_LDAZ = "        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)\n"

RE_VDP_STORE = re.compile(
    r"^\s+(?:"
    r"ST[AXY]\s+VDP_(?:DATA|CTRL)"      # raw STA/STX/STY VDP_*
    r"|WRT_DATA_REG\b"                  # tms9918.inc macro: STA + pad40
    r"|WRT_DATA_VAL\b"                  # tms9918.inc macro: LDA # + STA + pad40
    r")"
    r"(?:\s+[^;]*?)?(?:\s*;.*)?$"
)
# Macros from tms9918.inc that embed their own postlude pad40. The patcher
# treats them as VDP stores when looking for a STA2 target (so callers get
# padded before them) but skips the outer-loop forward analysis from them
# (because the macro itself already provides the gap to the *next* VDP
# write). Without this distinction, a chain like
#   WRT_DATA_REG / LDA / WRT_DATA_REG
# would get a redundant patcher-injected pad40 inserted between the macros.
RE_BUILTIN_PADDED_STORE = re.compile(r"^\s+WRT_DATA_(?:REG|VAL)\b")


def is_builtin_padded_store(line: str) -> bool:
    return bool(RE_BUILTIN_PADDED_STORE.match(strip_inline_label(line)))
RE_LABEL     = re.compile(r"^[ \t]*(@?[a-zA-Z_][a-zA-Z0-9_]*):\s*$")
# Top-level labels only (no leading @) — used by find_skip_ranges (now a
# no-op stub) and find_stale_skip_markers to delimit routines for the
# stale-SKIP warning.
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

# Control-flow instructions that terminate the local cycle accounting:
# JMP (unconditional), Bcc (8 conditional branches), JSR (subroutine call —
# the called body's cycle count is opaque to the local walk), RTS / RTI
# (return — what comes next is up to the caller).
#
# When the patcher's forward bridge walk encounters one of these, it falls
# into "branch lookahead" mode: scan further in source for a STA VDP_*, and
# if found, pad before the branch instruction (so the pad fires once per
# iteration regardless of which way the branch goes).
RE_BRANCH = re.compile(
    r"^\s+(JMP|JSR|RTS|RTI|"
    r"BCC|BCS|BNE|BEQ|BPL|BMI|BVC|BVS"
    r")\b"
)


def is_branch_or_jump(line: str) -> bool:
    return bool(RE_BRANCH.match(strip_inline_label(line)))


def branch_target_label(line: str) -> str | None:
    """If this line is a JMP/JSR/Bcc to a label, return the label name (or
    None for RTS/RTI/computed branches)."""
    m = re.match(
        r'^\s*(?:JMP|JSR|BCC|BCS|BNE|BEQ|BPL|BMI|BVC|BVS)\s+(@?[a-zA-Z_]\w*)',
        strip_inline_label(line).strip()
    )
    return m.group(1) if m else None


# Detect a JSR call that's already an idle-pad helper (12c, 24c or 40c).
# When the patcher walks forward from STA1 and the very next instruction
# is one of these, STA1 is already protected — skip its analysis. This
# keeps the patcher idempotent when called on a file that contains hand-
# coded pad helpers (lib files like tms9918m1.asm, or hand-tuned project
# sites that pre-date the v2 marker convention).
RE_EXISTING_PAD = re.compile(r'^\s+JSR\s+tms9918_pad(?:12|24|40)\b')


def is_existing_pad(line: str) -> bool:
    return bool(RE_EXISTING_PAD.match(strip_inline_label(line)))


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
    """Remove every line we previously inserted (v1 NOP, v2 pad16 JSR,
    v2 pad24 JSR or v2 pad40 JSR).

    Strict regex match — a line is stripped ONLY if it has the exact shape
    of an auto-generated line (`<indent> JSR tms9918_pad{12,24,40} ; ... <key>`
    or `<indent> .import tms9918_pad{12,24,40} ; ... <key>`). Hand-written
    comments that merely *mention* the marker phrase (e.g. a `JSR pad40`
    prologue whose docstring explains "this avoids the silicon-strict
    pad40 strip key") are preserved.

    Without this strict check the patcher's hide_slot_4 prologue in
    TMS_Galaga.asm got eaten on every rerun (May 2026 incident)."""
    auto_re = re.compile(
        r"^\s+(?:JSR\s+tms9918_pad(?:12|24|40)|\.import\s+tms9918_pad(?:12|24|40))\b"
        r".*?(?:" + re.escape(V1_MARKER)
        + "|" + re.escape(V2_PAD16_MARKER)
        + "|" + re.escape(V2_PAD24_MARKER)
        + "|" + re.escape(V2_MARKER) + ")"
    )
    return [l for l in src if not auto_re.match(l)]


def find_skip_ranges(src: list[str]) -> list[tuple[int, int]]:
    """Strict mode means strict — no per-routine exemption. The legacy
    `; SILICON_STRICT_SKIP` annotation was removed (May 2026) after it
    created two recurring footguns:
        1. Stale comments mentioning the directive name silently
           disabled pad injection across an entire routine.
        2. "Skip" exemptions defeated the whole point of strict mode —
           a passing strict-mode build no longer guaranteed the silicon
           contract because the auditor couldn't tell which routines
           had been audited vs. exempted.

    Always returns []. If the marker is still present in source, the
    patcher does NOT honour it (and emits a warning so the user knows
    to clean it up)."""
    return []


def find_stale_skip_markers(src: list[str]) -> list[int]:
    """Return line indices of any leftover `; SILICON_STRICT_SKIP` comments.
    These are no longer honoured — surface them so the user can remove."""
    skip_re = re.compile(r"^\s*;\s*SILICON_STRICT_SKIP\b")
    return [i for i, line in enumerate(src) if skip_re.match(line)]


# JSR target name extractor (for cross-routine analysis).
RE_JSR_TARGET = re.compile(r"^\s+JSR\s+(@?[a-zA-Z_][a-zA-Z0-9_]*)\b")


def jsr_target(line: str) -> str | None:
    m = RE_JSR_TARGET.match(strip_inline_label(line))
    return m.group(1) if m else None


# RTS detector (case-insensitive, no operand).
RE_RTS_LINE = re.compile(r"^\s+RTS\s*(;.*)?$", re.IGNORECASE)


def find_vdp_tail_routines(src: list[str]) -> set[str]:
    """Return the set of TOP-level routine names whose body ends — at any
    RTS exit point — with a STA VDP_* close enough to leak the silicon-
    strict gap into the caller. Cross-JSR boundaries are the major gap
    the patcher used to miss: caller's STA → JSR routineB → routineB's
    last STA + RTS (12c) → caller's next STA = ~10-13c gap, well under
    the 16c VBlank threshold. Treating these JSR call sites as
    *synthetic* VDP stores in the main case detector lets the patcher
    inject a pad on the caller side automatically.

    Heuristic: walk each TOP-level routine body. For every RTS, look
    backward for the most recent STA VDP_* within ≤ 4 code lines (no
    intervening VDP store between them). If found, the routine is
    VDP-tail. This matches the typical shape `... / STA VDP_DATA / RTS`
    or `... / STA VDP_DATA / [bridge] / RTS` used by helpers like
    emit_3digit_vdp, draw_str_tms, plot_star, hide_slot_4, etc."""
    tail_routines: set[str] = set()
    label_lines: list[tuple[int, str]] = []
    for i, l in enumerate(src):
        m = RE_TOP_LABEL.match(l)
        if m:
            label_lines.append((i, m.group(1)))
    if not label_lines:
        return tail_routines
    boundaries = [i for i, _ in label_lines] + [len(src)]

    def is_code_line(line: str) -> bool:
        s = line.strip()
        return bool(s) and not s.startswith(";") and not RE_LABEL.match(line)

    for k, (start, name) in enumerate(label_lines):
        end = boundaries[k + 1] - 1
        last_vdp_idx = -1
        for i in range(start, end + 1):
            line = src[i]
            if is_vdp_store(line):
                last_vdp_idx = i
                continue
            if not is_code_line(line):
                continue
            if RE_RTS_LINE.match(strip_inline_label(line)):
                if last_vdp_idx >= 0:
                    code_between = sum(
                        1 for j in range(last_vdp_idx + 1, i) if is_code_line(src[j])
                    )
                    if code_between <= 4:
                        tail_routines.add(name)
                last_vdp_idx = -1  # reset for next exit point
    return tail_routines


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


def warn_stale_skip(src: list[str], path: pathlib.Path) -> None:
    """Print a warning if the source still mentions the obsolete SKIP
    annotation. Strict mode means strict — these markers are no longer
    honoured."""
    stale = find_stale_skip_markers(src)
    if stale:
        print(f"{path}: WARNING — {len(stale)} stale SILICON_STRICT_SKIP "
              f"marker(s) found at line(s) {[i+1 for i in stale]}.",
              file=sys.stderr)
        print("  Strict mode no longer honours these. Either remove the",
              file=sys.stderr)
        print("  comments, or accept that pads will be injected uniformly.",
              file=sys.stderr)


def apply_padding(src: list[str]) -> tuple[list[str], dict[str, int]]:
    """v2-pad40 strategy:
       - Case A: insert JSR tms9918_pad40 between the two ST? VDP_*.
       - Case B/C: insert JSR tms9918_pad40 BEFORE the LDA bridge (so the
         40c idle lands between STA1 and the LDA, giving gap = 4+40+2+4 = 50c
         for B, 4+40+3+4 = 51c for C — well over the 40c hardened threshold).
         Bridge instructions like ORA/CLC/ADC/INX between the LDA and the
         second STA are tolerated (up to 8 in a row) — common in vdp_set_write
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

    # Pre-scan: which routines END (at any RTS exit) with STA VDP_*.
    # A `JSR <vdp_tail>` site is treated as a synthetic VDP store for the
    # purposes of case detection, so callers get padded before their next
    # VDP access. See find_vdp_tail_routines for the heuristic.
    vdp_tail = find_vdp_tail_routines(src)

    def is_vdp_access_line(line: str) -> bool:
        """True for real STA VDP_* and for JSR <vdp_tail_routine> — both
        consume the chip's access window, so the patcher's STA→STA
        analysis must treat them uniformly."""
        if is_vdp_store(line):
            return True
        tgt = jsr_target(line)
        return tgt is not None and tgt in vdp_tail

    out: list[str] = []
    counts = {"A": 0, "B": 0, "C": 0}

    # Track the indices in `src` where we want to insert a pad BEFORE that line
    # (used for case B/C: the pad goes before the LDA, which we discover when
    # we walk forward from STA1).
    pad_before: dict[int, str] = {}

    # Up to this many bridge instructions allowed between two VDP stores.
    # Bumped 4 → 8 (May 2026) because the 40c-hardened contract pushes
    # callers to chain more value-shaping ops between VDP writes (typical
    # offender: `LDA pen_color / ASL × 4 / ORA #$01 / STA VDP_DATA` in
    # tms9918m2.asm:plot_set — 6 bridges, was missed at MAX_BRIDGE=4).
    MAX_BRIDGE = 8
    # Forward-search window for the second VDP store across simple control
    # flow (conditional branches + JMP). Used by the "branch lookahead"
    # extension below: if the second VDP store is reachable within this many
    # source lines via at most one branch, we still pad — the pad runs
    # unconditionally before the branch decision (or before the branch
    # instruction itself for backward loops), so it covers every path to
    # the next VDP write.
    BRANCH_LOOKAHEAD_LINES = 16

    for i, line in enumerate(src):
        if in_skip(i):
            continue
        # Start STA1 analysis from real VDP stores AND from JSR sites that
        # call a VDP-tail routine (the JSR is a synthetic VDP access — its
        # callee ends with STA VDP_* + RTS, so the access-window clock
        # bleeds into the caller).
        if not (is_vdp_store(line) or
                (jsr_target(line) is not None and jsr_target(line) in vdp_tail)):
            continue
        if is_builtin_padded_store(line):
            # WRT_DATA_REG / WRT_DATA_VAL macros embed their own postlude
            # pad40. Don't analyse forward from them — the macro itself
            # supplies the gap to the next VDP store. (They still serve as
            # STA2 targets when an *earlier* STA walks forward to find the
            # next VDP store.)
            continue
        # Walk forward up to (1 LDA + MAX_BRIDGE bridge + 1 STA) = 6 code lines.
        nxt = next_code_lines(src, i, 1 + MAX_BRIDGE + 1)
        if not nxt:
            continue
        n0_idx, n0_text = nxt[0]
        if is_vdp_access_line(n0_text):
            # Case A direct — STA/JSR-vdp-tail back-to-back. Insert pad
            # between. Note the second access can itself be a JSR to a
            # VDP-tail routine (caller chains two VDP-writing helpers
            # back-to-back — pad before the second JSR is correct).
            pad_before.setdefault(n0_idx, JSR_BTB)
            counts["A"] += 1
            continue
        if is_existing_pad(n0_text):
            # STA1 is already followed by a hand-coded pad helper —
            # whatever comes after the pad is ≥ pad-helper cycles away
            # from STA1. Skip this STA's analysis; the next STA in source
            # will be analysed on its own iteration of the outer loop.
            continue
        # Generalized case A: STA / [bridge instructions only] / STA. The
        # patcher inserts pad before the LAST STA (the "second" one), not
        # before the first bridge instruction. This avoids disturbing the
        # caller's data flow and gives gap = 4 + (bridges) + 40 + 4 ≥ 48c.
        bridge_kind = None
        branch_idx: int | None = None  # set when the bridge ends on JMP/Bcc/JSR/RTS
        if is_lda_imm(n0_text):
            bridge_kind = "B"
        elif is_lda_any(n0_text):
            bridge_kind = "C"
        elif RE_BRIDGE_OK.match(strip_inline_label(n0_text)) \
                or RE_NONVDP_STORE.match(strip_inline_label(n0_text)):
            bridge_kind = "A"  # generalized case A — non-LDA bridge
        elif is_branch_or_jump(n0_text):
            # First non-bridge after STA1 is a control-flow instruction.
            # Skip the bridge walk and jump directly to the branch lookahead
            # so we still find any STA VDP within the lookahead window
            # (forward path), inside the branch target's body (JMP/Bcc-follow),
            # or in a backward-loop body (Bcc back to a label upstream).
            bridge_kind = "branch"
            branch_idx = n0_idx
        else:
            continue
        # Walk through up to MAX_BRIDGE-1 bridge instructions; stop at first STA VDP.
        sta2_idx = None
        bridge_ended_in_branch = (bridge_kind == "branch")
        if not bridge_ended_in_branch:
            for k in range(1, len(nxt)):
                kk_idx, kk_text = nxt[k]
                if is_vdp_access_line(kk_text):
                    # STA2 found — could be a real STA or a JSR <vdp_tail>.
                    sta2_idx = kk_idx
                    break
                kk_stripped = strip_inline_label(kk_text)
                if RE_BRIDGE_OK.match(kk_stripped) or RE_NONVDP_STORE.match(kk_stripped):
                    continue
                # Stopped on something that's neither a bridge nor a VDP access.
                # Treat as branch terminator and fall through to the lookahead.
                bridge_ended_in_branch = True
                branch_idx = kk_idx
                break
        # Backward-branch tight-loop detection (May 2026 fix):
        # Independently of the forward scan, check if the branch is a
        # conditional Bcc whose target is BEFORE branch_idx. If yes AND
        # the loop body (between target label and branch) contains a STA
        # VDP_*, this is a tight loop — pad before the branch so every
        # iteration's STA→STA gap gets the cushion. Without this check,
        # a forward-found exit STA (in the next sibling routine) would
        # take precedence, leaving the back-edge unpadded (Chess
        # upload_chess_patterns @z1/@l1/@dpat tight-fill loops, May 2026).
        is_backward_loop = False
        if bridge_ended_in_branch and branch_idx is not None:
            label_name = branch_target_label(src[branch_idx])
            if label_name and label_name in labels_map:
                tgt = labels_map[label_name]
                if tgt < branch_idx:
                    # Branch targets earlier label. Is there a STA VDP in
                    # the loop body? Scan from target up to (but excluding)
                    # the branch line.
                    for kk in range(tgt, branch_idx):
                        if is_vdp_access_line(src[kk]):
                            is_backward_loop = True
                            break
        if is_backward_loop:
            # Pad before the branch — covers both back-edge AND loop-exit
            # paths with a single pad (since the pad runs unconditionally
            # before the Bcc decision).
            pad_before.setdefault(branch_idx, JSR_BTB)
            counts["A"] += 1
            continue
        if sta2_idx is None:
            if bridge_ended_in_branch:
                # Look ahead in raw source lines for a STA VDP. Bound the scan
                # to keep false positives manageable: stop at the next
                # TOP-LEVEL label (function boundary) and at BRANCH_LOOKAHEAD
                # lines. RTS/JMP-out also ends the scan via the top-level
                # label check.
                # +1 so the target STA at distance LOOKAHEAD lines is included.
                start_idx = branch_idx if branch_idx is not None else (n0_idx + 1)
                limit = min(len(src), i + BRANCH_LOOKAHEAD_LINES + 1)
                # Scan forward physically.
                for k in range(start_idx + 1, limit):
                    if RE_TOP_LABEL.match(src[k]):
                        break
                    if is_vdp_access_line(src[k]):
                        sta2_idx = k
                        break
                # Follow the branch target label if forward scan didn't find
                # anything. Catches:
                #   - JMP @after_N convergence (slot rendering with hidden alts)
                #   - Bcc @loop backward branches (tight loops where the next
                #     VDP STA is the loop body's first store)
                if sta2_idx is None and branch_idx is not None:
                    label_name = branch_target_label(src[branch_idx])
                    if label_name and label_name in labels_map:
                        tgt = labels_map[label_name]
                        tgt_limit = min(len(src), tgt + BRANCH_LOOKAHEAD_LINES + 1)
                        for kk in range(tgt, tgt_limit):
                            if kk != tgt and RE_TOP_LABEL.match(src[kk]):
                                break
                            if is_vdp_access_line(src[kk]):
                                sta2_idx = kk
                                break
            if sta2_idx is None:
                continue
        if bridge_kind == "B":
            pad_before.setdefault(n0_idx, JSR_LDAI)
            counts["B"] += 1
        elif bridge_kind == "C":
            pad_before.setdefault(n0_idx, JSR_LDAZ)
            counts["C"] += 1
        else:  # generalized case A or branch-terminated case
            # Pad placement:
            #  - sta2 found AFTER the branch (forward / JMP-target convergence):
            #    pad before sta2_idx (the next STA's instruction).
            #  - sta2 found BEFORE the branch (backward Bcc into loop body):
            #    pad before the branch instruction itself, so it fires on
            #    every iter just before looping back. Pad-before-sta2 in this
            #    case would land at the loop body's top, which only fires
            #    once per overall entry, not per iteration.
            if bridge_ended_in_branch and branch_idx is not None \
                    and sta2_idx < branch_idx:
                pad_before.setdefault(branch_idx, JSR_BTB)
            else:
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
    warn_stale_skip(src, args.file)
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
