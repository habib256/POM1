#!/usr/bin/env python3
"""lint_tms_lib.py — static SILICON LINT for POM1's TMS9918 6502 sources.

Enforces the repo's verified real-silicon contracts on every ca65 source in
the TMS9918 scope (dev/lib/tms9918, dev/lib/basicrt/basicrt_tms.s,
sketchs/tms9918, dev/codetank):

P1 — VDP DATA-port pacing.
    Real TMS9918A silicon (validated on Claudio Parmigiani's Replica-1) DROPS
    CPU writes to the DATA port ($CC00) spaced under ~16 cycles during active
    Mode I/II display. The repo convention is `JSR tms9918_pad18` between
    back-to-back accesses = 22c store-to-store (4c STA + 18c pad).
    The lint computes the MINIMUM cycle distance between consecutive $CC00
    accesses along every linear path (basic-block fall-through) AND around
    every loop back-edge (store-to-store across the branch-taken wrap).
      gap <  16c  -> ERROR  (at/below the silicon drop floor: data loss)
      gap <  22c  -> WARN   (clears the ~16c floor but below the pad18
                             contract margin — audit / migrate)
    Control-port ($CC01) pairs are never dropped by silicon (internal latch)
    and are NOT flagged.
    Exemptions (each grant is recorded; audit with --list):
      * display blanked — a preceding R1 write with bit 6 clear via a CTRL
        pair (LDA #v1 / STA VDP_CTRL ... LDA #$81 / STA VDP_CTRL, v1&$40==0)
        or `JSR vdp_display_off`, before the access, not yet re-enabled.
        Blanked display flips the chip to the dense ScreenOff access slots
        (~2c drain) where bursts always fit.
      * VBlank gate — a WAIT_VBLANK macro or a manual `BIT/LDA VDP_CTRL /
        BPL` poll immediately before the burst, provided the burst stays
        under the ~4000c VBlank budget (loops are exempted only when the
        iteration count can be statically bounded).
      * documented free zone — the routine's header/inline comments carry
        the repo's audited free-zone vocabulary ("display blanked",
        "display off", "ScreenOff", "free zone", "VBlank"). This mirrors
        the tree's existing audit annotations.
      * pragma — `; lint: free-zone <why>` or `; lint: allow-fast <why>`
        on the access line or the loop label line. The justification text
        is REQUIRED (a bare pragma is itself a finding).

P2 — SAT immediate hygiene (conservative, comment-driven; zero-FP bias).
    In sprite/SAT-annotated `LDA #imm / STA VDP_DATA` sequences:
      * a byte the comment calls a colour must be <= $8F (EC bit + colour
        nibble; anything above sets undefined bits),
      * a byte the comment calls a 16x16 pattern name must be % 4 == 0.

P3 — cross-file conventions.
      * duplicate hardcoded ZEROPAGE labels (`.segment "ZEROPAGE"` /
        `.zeropage` + `name: .res N`) declared in two scoped files with
        DIFFERENT sizes (the basicrt_gen2-vs-card-lib ln_* hazard class),
      * `.export`/`.exportzp` symbols defined by more than one scoped file
        that the Symbol-collision registry in dev/lib/README.md does not
        list as a deliberate link-exclusive shadow.

Exit codes: 0 clean (warnings allowed unless --strict), 1 violations,
2 parse/setup error.  `--list` prints per-file stats + every exemption
granted so a human can audit the auto-detection.

Stdlib-only, single pass per file, < 2 s on the whole scope.

Known limits (static analysis): computed Y=$D0 SAT terminators, cross-
procedure store-to-store gaps (a JSR boundary resets tracking except for
the modelled helpers), data-driven register writes (TAX/indexed R1 loads),
and code reached only via computed jumps are not checked.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Contract constants
# ---------------------------------------------------------------------------
SILICON_FLOOR = 16      # < 16c store-to-store: real silicon drops the access
CONTRACT_GAP = 22       # repo pad18 convention: 22c store-to-store
VBLANK_BUDGET = 4000    # cycles a VBlank-gated burst may use (NTSC ~4554c)

VDP_DATA_ADDR = 0xCC00
VDP_CTRL_ADDR = 0xCC01

PAD_CYCLES = {          # JSR-inclusive total cost of the pad helpers
    "tms9918_pad12": 18,   # legacy alias -> pad18
    "tms9918_pad18": 18,
    "tms9918_pad24": 30,
    "tms9918_pad40": 48,
}

# Comment vocabulary the tree's silicon audit already uses to mark free zones.
FREEZONE_COMMENT_RE = re.compile(
    r"display\s+(?:is\s+|still\s+)?(?:blanked|off)|screen\s*off|"
    r"free[\s-]zone|v-?blank|blanked", re.IGNORECASE)

PRAGMA_RE = re.compile(r";\s*lint:\s*(free-zone|allow-fast)\b[ \t]*(.*)",
                       re.IGNORECASE)

# ---------------------------------------------------------------------------
# 6502 cycle table — MINIMUM cycles (no page-cross penalties, branch
# not-taken = 2). Minimum is the sound direction for a *minimum-gap* check.
# ---------------------------------------------------------------------------
_RW = {"imm": 2, "zp": 3, "zpx": 4, "zpy": 4, "abs": 4, "absx": 4,
       "absy": 4, "indx": 6, "indy": 5}
CYCLES = {}
for _m in ("ADC", "AND", "CMP", "EOR", "LDA", "ORA", "SBC"):
    CYCLES[_m] = dict(_RW)
CYCLES["LDX"] = {"imm": 2, "zp": 3, "zpy": 4, "abs": 4, "absy": 4}
CYCLES["LDY"] = {"imm": 2, "zp": 3, "zpx": 4, "abs": 4, "absx": 4}
CYCLES["BIT"] = {"zp": 3, "abs": 4}
CYCLES["CPX"] = CYCLES["CPY"] = {"imm": 2, "zp": 3, "abs": 4}
CYCLES["STA"] = {"zp": 3, "zpx": 4, "abs": 4, "absx": 5, "absy": 5,
                 "indx": 6, "indy": 6}
CYCLES["STX"] = {"zp": 3, "zpy": 4, "abs": 4}
CYCLES["STY"] = {"zp": 3, "zpx": 4, "abs": 4}
for _m in ("ASL", "LSR", "ROL", "ROR"):
    CYCLES[_m] = {"acc": 2, "zp": 5, "zpx": 6, "abs": 6, "absx": 7}
for _m in ("INC", "DEC"):
    CYCLES[_m] = {"zp": 5, "zpx": 6, "abs": 6, "absx": 7}
CYCLES["JMP"] = {"abs": 3, "ind": 5}
CYCLES["JSR"] = {"abs": 6}
IMPLIED = {"CLC": 2, "SEC": 2, "CLI": 2, "SEI": 2, "CLV": 2, "CLD": 2,
           "SED": 2, "DEX": 2, "DEY": 2, "INX": 2, "INY": 2, "TAX": 2,
           "TXA": 2, "TAY": 2, "TYA": 2, "TSX": 2, "TXS": 2, "NOP": 2,
           "PHA": 3, "PHP": 3, "PLA": 4, "PLP": 4, "RTS": 6, "RTI": 6,
           "BRK": 7}
BRANCHES = {"BCC", "BCS", "BEQ", "BNE", "BMI", "BPL", "BVC", "BVS"}
A_CLOBBER = {"LDA", "ADC", "SBC", "AND", "ORA", "EOR", "ASL", "LSR", "ROL",
             "ROR", "TXA", "TYA", "PLA", "JSR"}  # JSR: callee may clobber A

# Baseline ZP names from dev/lib/apple1/zp.inc (fixed $00-$07) — used only to
# pick zp vs abs timing for symbols we cannot otherwise resolve (1c effect).
KNOWN_ZP = {"tmp", "tmp2", "print_ptr_lo", "print_ptr_hi", "mul_tmp",
            "mul_res0", "prng_lo", "prng_hi"}

# Helpers modelled across the JSR boundary (everything else is a barrier —
# the gap becomes "unknown", i.e. assumed safe; see Known limits above).
#   inline: pseudo-instruction stream executed at the call site
#           (cost, is_data_access) — JSR cost itself is prepended.
HELPER_INLINE = {
    # vdp_write_a (tms9918_helpers.asm): STA VDP_DATA / NOP / NOP / RTS
    "vdp_write_a": [(4, True), (2, False), (2, False), (6, False)],
    # print_at_rc (tms9918m1.asm) *ends* STA VDP_DATA / RTS: model the exit
    # hazard (a DATA store 10c before control returns). Entry side is safe
    # (PHA + JSR name_at_rc ~50c before its store).
    "print_at_rc": [(999, False), (4, True), (6, False)],
}

MNEMONIC_RE = re.compile(r"^[A-Za-z]{3}$")
LABEL_RE = re.compile(r"^(@?[A-Za-z_][A-Za-z0-9_]*):")
EQUATE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:?=\s*(.+)$")


def parse_number(text: str):
    t = text.strip()
    try:
        if t.startswith("$"):
            return int(t[1:], 16)
        if t.startswith("%"):
            return int(t[1:], 2)
        if re.fullmatch(r"\d+", t):
            return int(t, 10)
    except ValueError:
        return None
    return None


# ---------------------------------------------------------------------------
# Source model
# ---------------------------------------------------------------------------
@dataclass
class Ev:
    """One analysable event in the linearised instruction stream."""
    kind: str                 # 'insn' | 'label' | 'barrier' | 'gate'
    line: int = 0
    label: str = ""           # for kind=='label'
    mnem: str = ""
    operand: str = ""
    comment: str = ""
    cost: int = 0
    is_data: bool = False     # accesses $CC00
    is_ctrl: bool = False     # accesses $CC01
    is_branch: bool = False
    target: str = ""          # branch/jmp target
    note: str = ""            # why a barrier / provenance of expansion


@dataclass
class FileStats:
    lines: int = 0
    insns: int = 0
    data_accesses: int = 0
    ctrl_accesses: int = 0
    loops_checked: int = 0
    gaps_checked: int = 0


@dataclass
class Finding:
    severity: str             # 'ERROR' | 'WARN'
    check: str                # 'P1' | 'P2' | 'P3'
    path: str
    line: int
    message: str

    def render(self) -> str:
        return (f"[{self.check}-{self.severity}] {self.path}:{self.line}: "
                f"{self.message}")


@dataclass
class Exemption:
    path: str
    line: int
    kind: str
    detail: str

    def render(self) -> str:
        return f"  {self.path}:{self.line}: [{self.kind}] {self.detail}"


BUILTIN_MACROS = {
    # tms9918.inc — treated as their documented expansion.
    "WRT_DATA_REG": (0, ["STA VDP_DATA", "JSR tms9918_pad18"]),
    "WRT_DATA_VAL": (1, ["LDA #{0}", "STA VDP_DATA", "JSR tms9918_pad18"]),
    # WAIT_VBLANK / WAIT_5S handled specially (gate / spin events).
}


class FileLinter:
    def __init__(self, path: pathlib.Path, rel: str, ctx: "Linter"):
        self.path = path
        self.rel = rel
        self.ctx = ctx
        self.equates: dict[str, int] = {"VDP_DATA": VDP_DATA_ADDR,
                                        "VDP_CTRL": VDP_CTRL_ADDR}
        self.zp_syms: set[str] = set(KNOWN_ZP)
        self.macros: dict[str, tuple[list[str], list[str]]] = {}
        self.stats = FileStats()
        self.stream: list[Ev] = []
        self.raw_lines: list[str] = []
        # side tables filled by the linear pass, used by the loop pass
        self.display_off_at: list[bool] = []
        self.gate_cycles_at: list = []      # None or cycles-since-gate
        self.routine_at: list[int] = []     # stream idx of enclosing routine label
        self.zp_res: dict[str, tuple[int, int]] = {}   # name -> (size, line)
        self.exports: dict[str, int] = {}              # name -> line

    # -- helpers ------------------------------------------------------------
    @staticmethod
    def split_comment(line: str) -> tuple[str, str]:
        in_str = None
        for i, c in enumerate(line):
            if in_str:
                if c == in_str:
                    in_str = None
            elif c in "\"'":
                in_str = c
            elif c == ";":
                return line[:i], line[i:]
        return line, ""

    def resolve(self, sym: str):
        return self.equates.get(sym)

    def addr_mode(self, mnem: str, operand: str):
        """Return (mode, base_symbol_or_value)."""
        op = operand.strip()
        if not op or op.upper() == "A":
            return ("acc" if mnem in ("ASL", "LSR", "ROL", "ROR") else "imp",
                    None)
        if op.startswith("#"):
            return "imm", None
        if op.startswith("("):
            inner = op[1:]
            if re.search(r",\s*[Xx]\s*\)\s*$", op):
                return "indx", None
            if re.search(r"\)\s*,\s*[Yy]\s*$", op):
                return "indy", None
            return "ind", None
        m = re.search(r",\s*([XxYy])\s*$", op)
        idx = None
        if m:
            idx = m.group(1).upper()
            op = op[:m.start()].strip()
        base = op
        val = parse_number(base)
        if val is None:
            val = self.resolve(base)
        is_zp = ((val is not None and val < 0x100) or
                 (val is None and base in self.zp_syms))
        if idx == "X":
            return ("zpx" if is_zp else "absx"), (base, val)
        if idx == "Y":
            return ("zpy" if is_zp else "absy"), (base, val)
        return ("zp" if is_zp else "abs"), (base, val)

    def insn_cost(self, mnem: str, operand: str) -> tuple[int, str, object]:
        mode, base = self.addr_mode(mnem, operand)
        if mnem in IMPLIED and mode in ("imp", "acc"):
            return IMPLIED[mnem], mode, base
        if mnem in BRANCHES:
            return 2, "rel", base       # fall-through minimum
        tbl = CYCLES.get(mnem)
        if tbl is None:
            return 0, "?", base
        if mode in tbl:
            return tbl[mode], mode, base
        # mode/mnemonic mismatch (e.g. zpy on STA) — fall back to any
        for alt in ("abs", "zp", "imm", "acc", "imp"):
            if alt in tbl:
                return tbl[alt], alt, base
        return 2, mode, base

    def target_addr(self, operand: str):
        mode, base = self.addr_mode("LDA", operand)
        if isinstance(base, tuple):
            name, val = base
            if val is not None:
                return val
            if name == "VDP_DATA":
                return VDP_DATA_ADDR
            if name == "VDP_CTRL":
                return VDP_CTRL_ADDR
        return None

    # -- pass 0: prescan equates / macros / zp / exports ---------------------
    def prescan(self):
        seg = "CODE"
        in_macro = None
        macro_params: list[str] = []
        macro_body: list[str] = []
        depth_repeat = 0
        for n, raw in enumerate(self.raw_lines, 1):
            code, _ = self.split_comment(raw)
            s = code.strip()
            if not s:
                continue
            low = s.lower()
            if in_macro is not None:
                if low.startswith(".endmacro"):
                    self.macros[in_macro] = (macro_params, macro_body)
                    in_macro = None
                else:
                    macro_body.append(s)
                continue
            if low.startswith(".macro"):
                parts = s.split(None, 2)
                if len(parts) >= 2:
                    in_macro = parts[1]
                    macro_params = ([p.strip() for p in parts[2].split(",")]
                                    if len(parts) > 2 else [])
                    macro_body = []
                continue
            if low.startswith(".segment"):
                m = re.search(r'"([^"]+)"', s)
                seg = m.group(1).upper() if m else "CODE"
                continue
            if low.startswith(".zeropage"):
                seg = "ZEROPAGE"
                continue
            if low in (".code", ".bss", ".data", ".rodata"):
                seg = low[1:].upper()
                continue
            if low.startswith((".export", ".exportzp")):
                body = s.split(None, 1)[1] if len(s.split(None, 1)) > 1 else ""
                for tok in body.split(","):
                    name = tok.split(":")[0].split("=")[0].strip()
                    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name or ""):
                        self.exports.setdefault(name, n)
                if low.startswith(".exportzp"):
                    for tok in body.split(","):
                        name = tok.split(":")[0].split("=")[0].strip()
                        if name:
                            self.zp_syms.add(name)
                continue
            if low.startswith(".importzp"):
                body = s.split(None, 1)[1] if len(s.split(None, 1)) > 1 else ""
                for tok in body.split(","):
                    name = tok.split(":")[0].strip()
                    if name:
                        self.zp_syms.add(name)
                continue
            # labels + equates (strip a leading label first)
            mlab = LABEL_RE.match(s)
            rest = s[mlab.end():].strip() if mlab else s
            if mlab and seg == "ZEROPAGE":
                mres = re.match(r"\.res\s+(\S+)", rest, re.IGNORECASE)
                if mres:
                    size = parse_number(mres.group(1))
                    name = mlab.group(1)
                    if size is not None and not name.startswith("@"):
                        self.zp_res[name] = (size, n)
                    self.zp_syms.add(name)
            if mlab and seg == "ZEROPAGE":
                self.zp_syms.add(mlab.group(1))
            meq = EQUATE_RE.match(rest if not mlab else "")
            if meq and not rest.lower().startswith("."):
                val = parse_number(meq.group(2))
                if val is not None:
                    self.equates[meq.group(1)] = val
                    if val < 0x100:
                        self.zp_syms.add(meq.group(1))
            _ = depth_repeat

    # -- pass 1: build the linear event stream -------------------------------
    def build_stream(self):
        seg = "CODE"
        in_macro = False
        i = 0
        lines = self.raw_lines
        gate_serial = [0]

        def emit_barrier(n, note):
            self.stream.append(Ev("barrier", line=n, note=note))

        def emit_source_line(s: str, comment: str, n: int, depth=0):
            """Recursively expand one code statement into events."""
            if depth > 6:
                emit_barrier(n, "macro depth")
                return
            s = s.strip()
            if not s:
                return
            mlab = LABEL_RE.match(s)
            if mlab:
                self.stream.append(Ev("label", line=n, label=mlab.group(1),
                                      comment=comment))
                emit_source_line(s[mlab.end():], comment, n, depth)
                return
            if s.startswith("."):
                low = s.lower()
                if low.startswith((".byte", ".word", ".addr", ".res",
                                   ".dbyt", ".lobytes", ".hibytes", ".asciiz",
                                   ".align", ".org", ".include", ".incbin",
                                   ".segment", ".zeropage", ".code", ".bss",
                                   ".data", ".rodata", ".if", ".else",
                                   ".elseif", ".endif", ".proc", ".endproc",
                                   ".scope", ".endscope")):
                    emit_barrier(n, s.split()[0])
                return
            if EQUATE_RE.match(s) and "=" in s:
                return
            parts = s.split(None, 1)
            word = parts[0]
            operand = parts[1].strip() if len(parts) > 1 else ""
            upper = word.upper()
            # macro invocations ------------------------------------------------
            if upper == "WAIT_VBLANK":
                gate_serial[0] += 1
                # drain read + spin (min 1 iteration): 4 + 4 + 2
                self.stream.append(Ev("insn", line=n, mnem="BIT",
                                      operand="VDP_CTRL", cost=4,
                                      is_ctrl=True, comment=comment))
                self.stream.append(Ev("insn", line=n, mnem="BIT",
                                      operand="VDP_CTRL", cost=4,
                                      is_ctrl=True))
                self.stream.append(Ev("gate", line=n,
                                      note="WAIT_VBLANK macro"))
                return
            if upper == "WAIT_5S":
                self.stream.append(Ev("insn", line=n, mnem="BIT",
                                      operand="VDP_CTRL", cost=4,
                                      is_ctrl=True, comment=comment))
                emit_barrier(n, "WAIT_5S spin (mid-frame trap)")
                return
            if upper in BUILTIN_MACROS:
                nargs, body = BUILTIN_MACROS[upper]
                args = ([a.strip() for a in operand.split(",")]
                        if operand else [])
                for tmpl in body:
                    emit_source_line(tmpl.format(*args[:nargs]), comment, n,
                                     depth + 1)
                return
            if word in self.macros:
                params, body = self.macros[word]
                args = ([a.strip() for a in operand.split(",")]
                        if operand else [])
                for bl in body:
                    expanded = bl
                    for p, a in zip(params, args):
                        expanded = re.sub(rf"\b{re.escape(p)}\b", a, expanded)
                    emit_source_line(expanded, comment, n, depth + 1)
                return
            if not MNEMONIC_RE.match(word) or \
               (upper not in CYCLES and upper not in IMPLIED and
                    upper not in BRANCHES):
                emit_barrier(n, f"unrecognised: {word}")
                return
            # plain instruction ------------------------------------------------
            mnem = upper
            cost, mode, base = self.insn_cost(mnem, operand)
            ev = Ev("insn", line=n, mnem=mnem, operand=operand, cost=cost,
                    comment=comment)
            addr = self.target_addr(operand) if mode not in (
                "imm", "imp", "acc", "rel", "ind") else None
            ev.is_data = addr == VDP_DATA_ADDR
            ev.is_ctrl = addr == VDP_CTRL_ADDR
            if mnem in BRANCHES:
                ev.is_branch = True
                ev.target = operand.strip()
            if mnem == "JMP":
                ev.target = operand.strip()
            self.stream.append(ev)

        # ---- main scan, with .repeat / .macro skipping -----------------------
        n = 0
        repeat_stack: list[tuple[int, list[tuple[str, str, int]]]] = []
        while i < len(lines):
            n = i + 1
            raw = lines[i]
            i += 1
            code, comment = self.split_comment(raw)
            s = code.strip()
            low = s.lower()
            if in_macro:
                if low.startswith(".endmacro"):
                    in_macro = False
                continue
            if low.startswith(".macro"):
                in_macro = True
                emit_barrier(n, ".macro definition")
                continue
            if low.startswith(".repeat"):
                m = re.match(r"\.repeat\s+([^,\s]+)", s, re.IGNORECASE)
                count = parse_number(m.group(1)) if m else None
                repeat_stack.append((count if count is not None else -1, []))
                continue
            if low.startswith((".endrepeat", ".endrep")):
                if repeat_stack:
                    count, body = repeat_stack.pop()
                    if repeat_stack:
                        repeat_stack[-1][1].extend(body * max(count, 0))
                    elif count < 0:
                        emit_barrier(n, ".repeat with non-constant count")
                    else:
                        for (bs, bc, bn) in body * count:
                            emit_source_line(bs, bc, bn)
                continue
            if repeat_stack:
                if s or comment:
                    repeat_stack[-1][1].append((s, comment, n))
                continue
            if not s:
                if comment:
                    self.stream.append(Ev("barrier", line=n, comment=comment,
                                          note="comment"))
                continue
            emit_source_line(s, comment, n)
            _ = seg

    # -- shared comment-context helpers ---------------------------------------
    def routine_comment_block(self, stream_idx: int) -> list[tuple[int, str]]:
        """Comments from the routine header block + inline up to stream_idx."""
        start = self.routine_at[stream_idx]
        out: list[tuple[int, str]] = []
        # contiguous comment barriers immediately above the routine label
        j = start - 1
        while j >= 0 and self.stream[j].kind == "barrier" and \
                self.stream[j].note == "comment":
            out.append((self.stream[j].line, self.stream[j].comment))
            j -= 1
        for k in range(start, stream_idx + 1):
            ev = self.stream[k]
            if ev.comment:
                out.append((ev.line, ev.comment))
        return out

    def find_pragma(self, *stream_idxs) -> tuple[str, str, int] | None:
        for si in stream_idxs:
            if si is None:
                continue
            ev = self.stream[si]
            candidates = [(ev.line, ev.comment)]
            if si + 1 < len(self.stream):  # trailing comment-only line
                nx = self.stream[si + 1]
                if nx.kind == "barrier" and nx.note == "comment":
                    candidates.append((nx.line, nx.comment))
            for ln, com in candidates:
                m = PRAGMA_RE.search(com or "")
                if m:
                    return m.group(1).lower(), m.group(2).strip(), ln
        return None

    # -- pass 2: linear pacing scan --------------------------------------------
    def lint_pacing(self, findings: list[Finding], exemptions: list[Exemption]):
        st = self.stream
        nev = len(st)
        self.display_off_at = [False] * nev
        self.gate_cycles_at = [None] * nev
        self.routine_at = [0] * nev

        pending = None          # (stream_idx, cycles_since_access_start)
        display_off = False
        gate = None             # cycles since VBlank gate
        last_imm_a = None       # value of last LDA #imm still in A
        prev_ctrl_imm = None    # immediate written by the previous CTRL store
        routine = 0

        def exempt_reason(si_access, si_prev):
            """Return (kind, detail) if the access at si_access is exempt."""
            prag = self.find_pragma(si_access, si_prev)
            if prag:
                kind, why, ln = prag
                if not why:
                    findings.append(Finding(
                        "ERROR", "P1", self.rel, ln,
                        f"lint pragma '{kind}' without a justification — "
                        f"add a reason after the keyword"))
                    return None
                return (f"pragma {kind}", f"line {ln}: {why}")
            if display_off:
                return ("display-blanked (auto)",
                        "R1 bit6 cleared earlier in this routine")
            if gate is not None and gate <= VBLANK_BUDGET:
                return ("vblank-gate (auto)",
                        f"{gate}c after WAIT_VBLANK/status poll "
                        f"(budget {VBLANK_BUDGET}c)")
            for ln, com in self.routine_comment_block(si_access):
                m = FREEZONE_COMMENT_RE.search(com)
                if m:
                    return ("documented free-zone",
                            f"comment line {ln}: “{m.group(0)}”")
            return None

        for si, ev in enumerate(st):
            self.display_off_at[si] = display_off
            self.gate_cycles_at[si] = gate
            self.routine_at[si] = routine
            if ev.kind == "label":
                if not ev.label.startswith("@"):
                    routine = si
                    self.routine_at[si] = si
                pending = None          # unknown predecessors may jump here
                continue
            if ev.kind == "gate":
                gate = 0
                pending = None
                continue
            if ev.kind == "barrier":
                if ev.note != "comment":
                    pending = None
                    last_imm_a = None
                continue
            # kind == 'insn'
            self.stats.insns += 1
            mnem = ev.mnem
            if ev.is_data:
                self.stats.data_accesses += 1
            if ev.is_ctrl:
                self.stats.ctrl_accesses += 1

            # ---- JSR handling ----
            if mnem == "JSR":
                tgt = ev.operand.strip()
                if tgt in PAD_CYCLES:
                    if pending:
                        pending = (pending[0], pending[1] + PAD_CYCLES[tgt])
                    if gate is not None:
                        gate += PAD_CYCLES[tgt]
                    last_imm_a = last_imm_a  # pads are register-transparent
                    continue
                if tgt == "vdp_display_off":
                    display_off = True
                    pending = None
                    last_imm_a = None
                    continue
                if tgt == "vdp_display_on":
                    display_off = False
                    pending = None
                    last_imm_a = None
                    continue
                if tgt in HELPER_INLINE:
                    cyc = 6
                    for cost, is_data in HELPER_INLINE[tgt]:
                        if is_data:
                            self.stats.gaps_checked += 1
                            if pending is not None:
                                gap = pending[1] + cyc
                                if gap < CONTRACT_GAP:
                                    self._report_gap(findings, exemptions,
                                                     ev, si, pending[0], gap,
                                                     exempt_reason(si,
                                                                   pending[0]),
                                                     f"via JSR {tgt}")
                            pending = (si, cost)
                            cyc = 0
                        else:
                            cyc = min(cyc + cost, 999)
                            if pending is not None:
                                pending = (pending[0], pending[1] + cost)
                    if gate is not None:
                        gate = min(gate + 40, VBLANK_BUDGET + 1)
                    last_imm_a = None
                    continue
                # unknown callee: gap unknown -> safe; gate expires
                pending = None
                gate = None
                last_imm_a = None
                continue

            # ---- control flow ----
            if mnem in ("RTS", "RTI"):
                pending = None
                display_off = False
                gate = None
                last_imm_a = None
                continue
            if mnem == "JMP" or mnem == "BRK":
                pending = None
                if not ev.target.startswith("@"):
                    display_off = False
                    gate = None
                last_imm_a = None
                continue

            # ---- track A immediates + display state via CTRL pairs ----
            if mnem == "LDA" and ev.operand.startswith("#"):
                last_imm_a = parse_number(ev.operand[1:].strip())
                if last_imm_a is None:
                    v = self.resolve(ev.operand[1:].strip())
                    last_imm_a = v
            elif mnem in A_CLOBBER or mnem in ("TAX", "TAY"):
                if mnem in A_CLOBBER:
                    last_imm_a = None

            if ev.is_ctrl and mnem in ("STA", "STX", "STY"):
                cur = last_imm_a if mnem == "STA" else None
                if cur is not None and cur == 0x81 and prev_ctrl_imm is not None:
                    display_off = (prev_ctrl_imm & 0x40) == 0
                prev_ctrl_imm = cur

            # ---- manual VBlank poll: BIT/LDA VDP_CTRL then BPL backwards ----
            if ev.is_branch and ev.mnem == "BPL":
                j = si - 1
                polled = False
                while j >= 0 and st[j].kind == "insn" and \
                        st[j].mnem in ("BIT", "LDA", "ORA") and st[j].is_ctrl:
                    polled = True
                    j -= 1
                if polled and st[j].kind == "label" and \
                        st[j].label == ev.target:
                    gate = 0
                    pending = None
                    continue

            # ---- the pacing check itself ----
            if ev.is_data:
                self.stats.gaps_checked += 1
                if pending is not None:
                    gap = pending[1]
                    if gap < CONTRACT_GAP:
                        self._report_gap(findings, exemptions, ev, si,
                                         pending[0], gap,
                                         exempt_reason(si, pending[0]), "")
                pending = (si, ev.cost)
            elif pending is not None:
                pending = (pending[0], pending[1] + ev.cost)
            if gate is not None:
                gate += ev.cost

    def _report_gap(self, findings, exemptions, ev, si, prev_si, gap,
                    exempt, suffix):
        prev_ev = self.stream[prev_si]
        where = (f"$CC00 access {gap}c after the one at line {prev_ev.line}"
                 f"{' ' + suffix if suffix else ''}")
        if exempt:
            exemptions.append(Exemption(self.rel, ev.line, exempt[0],
                                        f"{where} — {exempt[1]}"))
            return
        if gap < SILICON_FLOOR:
            findings.append(Finding(
                "ERROR", "P1", self.rel, ev.line,
                f"{where}: below the ~{SILICON_FLOOR}c real-silicon drop "
                f"floor — the chip WILL lose this access during active "
                f"display (pad to >= {CONTRACT_GAP}c with JSR tms9918_pad18)"))
        else:
            findings.append(Finding(
                "WARN", "P1", self.rel, ev.line,
                f"{where}: clears the ~{SILICON_FLOOR}c silicon floor but "
                f"is below the repo's {CONTRACT_GAP}c pad18 contract "
                f"(margin {gap - SILICON_FLOOR}c)"))

    # -- pass 3: loop back-edge (wrap-around store-to-store) -------------------
    def lint_loops(self, findings: list[Finding],
                   exemptions: list[Exemption]):
        st = self.stream
        # map label -> stream idx (locals scoped to enclosing routine)
        label_at: dict[tuple[int, str], int] = {}
        routine = 0
        for si, ev in enumerate(st):
            if ev.kind == "label":
                if not ev.label.startswith("@"):
                    routine = si
                label_at[(routine if ev.label.startswith("@") else 0,
                          ev.label)] = si
        for si, ev in enumerate(st):
            if ev.kind != "insn" or not ev.is_branch:
                continue
            routine = self.routine_at[si]
            tgt = ev.target
            key = (routine, tgt) if tgt.startswith("@") else (0, tgt)
            tsi = label_at.get(key)
            if tsi is None or tsi >= si:
                continue    # forward branch or unknown target
            body = st[tsi + 1:si + 1]
            accesses = [k for k, b in enumerate(body)
                        if b.kind == "insn" and b.is_data]
            if not accesses:
                continue
            self.stats.loops_checked += 1
            # a barrier / unknown JSR / RTS / JMP inside the slice means the
            # linear wrap path is not provable — skip (assumed safe).
            broken = False
            costs = []
            for k, b in enumerate(body):
                if b.kind == "gate":
                    broken = True
                    break
                if b.kind == "label":
                    costs.append(0)
                    continue
                if b.kind == "barrier":
                    if b.note == "comment":
                        costs.append(0)
                        continue
                    broken = True
                    break
                if b.mnem == "JSR":
                    t = b.operand.strip()
                    if t in PAD_CYCLES:
                        costs.append(PAD_CYCLES[t])
                        continue
                    broken = True
                    break
                if b.mnem in ("RTS", "RTI", "JMP", "BRK"):
                    broken = True
                    break
                if b.kind == "insn" and b.is_branch and k != len(body) - 1:
                    costs.append(2)     # interior branch: fall-through
                    continue
                costs.append(b.cost)
            if broken:
                continue
            # wrap gap: last access -> (branch taken) -> first access
            last, first = accesses[-1], accesses[0]
            wrap = sum(costs[last:-1]) + 3 + sum(costs[:first])
            self.stats.gaps_checked += 1
            if wrap >= CONTRACT_GAP:
                continue
            acc_ev = body[first]
            # exemptions at loop scope --------------------------------------
            prag = self.find_pragma(tsi, tsi + 1 + first, si)
            if prag:
                kind, why, ln = prag
                if why:
                    exemptions.append(Exemption(
                        self.rel, acc_ev.line, f"pragma {kind}",
                        f"loop wrap gap {wrap}c — line {ln}: {why}"))
                    continue
                findings.append(Finding(
                    "ERROR", "P1", self.rel, ln,
                    f"lint pragma '{kind}' without a justification"))
                continue
            if self.display_off_at[tsi]:
                exemptions.append(Exemption(
                    self.rel, acc_ev.line, "display-blanked (auto)",
                    f"loop wrap gap {wrap}c — R1 bit6 cleared before the "
                    f"loop (ScreenOff slot density, ~2c drain)"))
                continue
            gate = self.gate_cycles_at[tsi]
            if gate is not None:
                iters = self._bound_iterations(tsi, si, body)
                total = None if iters is None else iters * sum(costs)
                if total is not None and gate + total <= VBLANK_BUDGET:
                    exemptions.append(Exemption(
                        self.rel, acc_ev.line, "vblank-gate (auto)",
                        f"loop wrap gap {wrap}c — VBlank-gated burst, "
                        f"{iters} iters x {sum(costs)}c = {total}c "
                        f"(+{gate}c lead-in) <= {VBLANK_BUDGET}c budget"))
                    continue
                findings.append(Finding(
                    "ERROR", "P1", self.rel, acc_ev.line,
                    f"loop wrap gap {wrap}c < {CONTRACT_GAP}c: VBlank-gated "
                    f"but the burst is unbounded or exceeds the "
                    f"{VBLANK_BUDGET}c budget"
                    + ("" if iters is None else f" ({iters} iters)")))
                continue
            doc = None
            for ln, com in self.routine_comment_block(tsi):
                m = FREEZONE_COMMENT_RE.search(com)
                if m:
                    doc = (ln, m.group(0))
                    break
            if doc:
                exemptions.append(Exemption(
                    self.rel, acc_ev.line, "documented free-zone",
                    f"loop wrap gap {wrap}c — comment line {doc[0]}: "
                    f"“{doc[1]}”"))
                continue
            if wrap < SILICON_FLOOR:
                findings.append(Finding(
                    "ERROR", "P1", self.rel, acc_ev.line,
                    f"loop wrap gap {wrap}c (label at line "
                    f"{st[tsi].line}, back-edge line {ev.line}): below the "
                    f"~{SILICON_FLOOR}c silicon drop floor in active "
                    f"display — pad the loop (JSR tms9918_pad18)"))
            else:
                findings.append(Finding(
                    "WARN", "P1", self.rel, acc_ev.line,
                    f"loop wrap gap {wrap}c (label at line {st[tsi].line}, "
                    f"back-edge line {ev.line}): clears the "
                    f"~{SILICON_FLOOR}c silicon floor but is below the "
                    f"{CONTRACT_GAP}c pad18 contract "
                    f"(margin {wrap - SILICON_FLOOR}c)"))

    def _bound_iterations(self, tsi, bsi, body):
        """Statically bound a counted loop, or None."""
        br = body[-1]
        uses_x = any(b.kind == "insn" and b.mnem in ("DEX", "INX", "CPX")
                     for b in body)
        uses_y = any(b.kind == "insn" and b.mnem in ("DEY", "INY", "CPY")
                     for b in body)
        reg = "X" if uses_x and not uses_y else ("Y" if uses_y else None)
        if reg is None or br.mnem not in ("BNE", "BPL", "BCC"):
            return None
        init = None
        j = tsi - 1
        seen = 0
        while j >= 0 and seen < 12:
            b = self.stream[j]
            j -= 1
            if b.kind in ("label", "barrier") and \
                    (b.kind != "barrier" or b.note != "comment"):
                break
            if b.kind != "insn":
                continue
            seen += 1
            if b.mnem == ("LDX" if reg == "X" else "LDY") and \
                    b.operand.startswith("#"):
                init = parse_number(b.operand[1:].strip())
                break
            if b.mnem in ("TAX", "TAY", "JSR"):
                break
        if init is None:
            return None
        dec = any(b.kind == "insn" and b.mnem == ("DEX" if reg == "X"
                                                  else "DEY") for b in body)
        inc = any(b.kind == "insn" and b.mnem == ("INX" if reg == "X"
                                                  else "INY") for b in body)
        cmp_imm = None
        for b in body:
            if b.kind == "insn" and b.mnem == ("CPX" if reg == "X"
                                               else "CPY") and \
                    b.operand.startswith("#"):
                cmp_imm = parse_number(b.operand[1:].strip())
        if dec and not inc:
            return init if init else 256
        if inc and not dec:
            if cmp_imm is not None and cmp_imm > init:
                return cmp_imm - init
            return 256 - init
        return None

    # -- P2: SAT immediates -----------------------------------------------------
    def lint_sat_immediates(self, findings: list[Finding]):
        st = self.stream
        for si, ev in enumerate(st):
            if ev.kind != "insn" or not ev.is_data or \
                    ev.mnem not in ("STA", "STX", "STY"):
                continue
            # find the immediate load feeding this store (allow pads between)
            imm = None
            load_ev = None
            j = si - 1
            hops = 0
            while j >= 0 and hops < 3:
                b = st[j]
                j -= 1
                if b.kind == "barrier" and b.note == "comment":
                    continue
                if b.kind != "insn":
                    break
                hops += 1
                if b.mnem == "JSR" and b.operand.strip() in PAD_CYCLES:
                    continue
                if b.mnem == ("LDA" if ev.mnem == "STA" else
                              "LDX" if ev.mnem == "STX" else "LDY") and \
                        b.operand.startswith("#"):
                    imm = parse_number(b.operand[1:].strip())
                    load_ev = b
                break
            if imm is None:
                continue
            ctx = (ev.comment or "") + " " + \
                  (load_ev.comment if load_ev else "")
            routine_ctx = " ".join(
                c for _, c in self.routine_comment_block(si)[-12:])
            sat_scope = re.search(r"\b(SAT|sprite)\b",
                                  ctx + " " + routine_ctx, re.IGNORECASE)
            if not sat_scope:
                continue
            if re.search(r"\bcolou?r\b", ctx, re.IGNORECASE) and \
                    imm > 0x8F and imm not in (0xD0, 0xD1):
                findings.append(Finding(
                    "WARN", "P2", self.rel, ev.line,
                    f"SAT colour byte #${imm:02X} > $8F (bit7=EC + colour "
                    f"nibble; undefined bits set)"))
            if re.search(r"16\s*x\s*16", ctx + " " + routine_ctx,
                         re.IGNORECASE) and \
                    re.search(r"\b(name|pattern)\b", ctx, re.IGNORECASE) and \
                    imm % 4 != 0:
                findings.append(Finding(
                    "WARN", "P2", self.rel, ev.line,
                    f"16x16 sprite pattern name #${imm:02X} is not a "
                    f"multiple of 4 (hardware masks to &$FC)"))

    # -- run ---------------------------------------------------------------------
    def run(self, findings, exemptions):
        try:
            text = self.path.read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            raise RuntimeError(f"cannot read {self.rel}: {e}")
        self.raw_lines = text.splitlines()
        self.stats.lines = len(self.raw_lines)
        self.prescan()
        self.build_stream()
        self.lint_pacing(findings, exemptions)
        self.lint_loops(findings, exemptions)
        self.lint_sat_immediates(findings)


# ---------------------------------------------------------------------------
# P3: cross-file conventions
# ---------------------------------------------------------------------------
def parse_collision_registry(readme: pathlib.Path):
    """Extract the deliberately-shared symbols from dev/lib/README.md.

    Two sections document intentional cross-file name sharing:
      * '### Symbol-collision registry' — link-exclusive shadow exports,
      * '## Cross-library zero-page map' — ZP slot names jointly owned by
        several drivers (e.g. vdp_* shared by tms9918m1/_text/_console).
    Backticked identifiers in either section's table rows are treated as
    intentional; a trailing '*' makes a prefix rule (rt_*, pix_*, ...).
    """
    exact: set[str] = set()
    prefixes: list[str] = []
    if not readme.exists():
        return exact, prefixes
    text = readme.read_text(encoding="utf-8", errors="replace")
    sections = []
    for pat in (r"### Symbol-collision registry(.*?)(?:\n## |\Z)",
                r"## Cross-library zero-page map(.*?)(?:\n### |\n## |\Z)"):
        m = re.search(pat, text, re.DOTALL)
        if m:
            sections.append(m.group(1))
    for line in "\n".join(sections).splitlines():
        if not line.strip().startswith("|"):
            continue
        for tok in re.findall(r"`([^`]+)`", line):
            if "/" in tok and "." in tok:
                continue        # a file path, not a symbol
            for sub in re.split(r",\s*", tok):
                sub = sub.strip()
                variants = []
                mp = re.match(r"([A-Za-z_][\w]*)\(([\w]+)\)$", sub)
                if mp:                       # plot_set(_x16) / line_xy(16)
                    variants = [mp.group(1), mp.group(1) + mp.group(2)]
                elif "/" in sub:             # vdp_set_write/read, rt_hlin/vlin
                    parts = sub.split("/")
                    head = parts[0]
                    variants = [head]
                    stem = head.rsplit("_", 1)[0] + "_" if "_" in head else ""
                    for alt in parts[1:]:
                        variants.append(alt if "_" in alt else stem + alt)
                else:
                    variants = [sub]
                for v in variants:
                    v = v.strip()
                    if not v:
                        continue
                    if v.endswith("*"):
                        prefixes.append(v[:-1])
                    elif re.fullmatch(r"[A-Za-z_][\w]*", v):
                        exact.add(v)
    return exact, prefixes


def lint_cross_file(linters: list[FileLinter], root: pathlib.Path,
                    findings: list[Finding]):
    # ZEROPAGE .res duplicates with different sizes
    zp_owner: dict[str, tuple[str, int, int]] = {}
    for fl in linters:
        for name, (size, line) in fl.zp_res.items():
            if name in zp_owner:
                opath, oline, osize = zp_owner[name]
                if osize != size:
                    findings.append(Finding(
                        "ERROR", "P3", fl.rel, line,
                        f"ZEROPAGE label '{name}' (.res {size}) also "
                        f"declared as .res {osize} in {opath}:{oline} — "
                        f"same name, different sizes silently corrupts "
                        f"the other claimant"))
            else:
                zp_owner[name] = (fl.rel, line, size)
    # Export collisions not in the README registry. Only LIB files
    # (dev/lib/**) are considered: sketchs/ and dev/codetank/ sources are
    # standalone link roots — mutually link-exclusive by construction — and
    # their exports (tmp, plot_mode, ...) exist to satisfy lib .importzp's.
    exact, prefixes = parse_collision_registry(root / "dev/lib/README.md")
    owners: dict[str, list[tuple[str, int]]] = {}
    for fl in linters:
        if not fl.rel.replace("\\", "/").startswith("dev/lib/"):
            continue
        for name, line in fl.exports.items():
            owners.setdefault(name, []).append((fl.rel, line))
    for name, defs in sorted(owners.items()):
        if len(defs) < 2:
            continue
        if name in exact or any(name.startswith(p) for p in prefixes):
            continue
        sites = ", ".join(f"{p}:{ln}" for p, ln in defs)
        findings.append(Finding(
            "ERROR", "P3", defs[0][0], defs[0][1],
            f"symbol '{name}' exported by multiple scoped files ({sites}) "
            f"and not listed in dev/lib/README.md's symbol-collision "
            f"registry"))


# ---------------------------------------------------------------------------
# Scope + driver
# ---------------------------------------------------------------------------
SCOPE_GLOBS = [
    "dev/lib/tms9918/*.asm",
    "dev/lib/basicrt/basicrt_tms.s",
    "sketchs/tms9918/**/*.asm",
    "dev/codetank/**/*.asm",
]


def collect_scope(root: pathlib.Path) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for g in SCOPE_GLOBS:
        files.extend(sorted(root.glob(g)))
    return sorted(set(files))


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Static silicon lint for POM1 TMS9918 6502 sources.")
    ap.add_argument("--root", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parent.parent,
                    help="repo root (default: parent of tools/)")
    ap.add_argument("--list", action="store_true", dest="list_mode",
                    help="print per-file stats and every exemption granted")
    ap.add_argument("--strict", action="store_true",
                    help="treat WARN findings as failures too")
    ap.add_argument("files", nargs="*", type=pathlib.Path,
                    help="lint only these files (default: the full scope)")
    args = ap.parse_args()

    root = args.root.resolve()
    files = ([f.resolve() for f in args.files] if args.files
             else collect_scope(root))
    if not files:
        print(f"lint_tms_lib: no source files found under {root}",
              file=sys.stderr)
        return 2

    findings: list[Finding] = []
    exemptions: list[Exemption] = []
    linters: list[FileLinter] = []
    for path in files:
        try:
            rel = str(path.relative_to(root))
        except ValueError:
            rel = str(path)
        fl = FileLinter(path, rel, None)
        try:
            fl.run(findings, exemptions)
        except RuntimeError as e:
            print(f"lint_tms_lib: parse error: {e}", file=sys.stderr)
            return 2
        linters.append(fl)
    lint_cross_file(linters, root, findings)

    if args.list_mode:
        print(f"{'file':56s} {'lines':>6s} {'insns':>6s} {'$CC00':>6s} "
              f"{'$CC01':>6s} {'loops':>6s} {'gaps':>6s}")
        for fl in linters:
            s = fl.stats
            print(f"{fl.rel:56s} {s.lines:6d} {s.insns:6d} "
                  f"{s.data_accesses:6d} {s.ctrl_accesses:6d} "
                  f"{s.loops_checked:6d} {s.gaps_checked:6d}")
        print(f"\nExemptions granted ({len(exemptions)}) — audit these:")
        for ex in exemptions:
            print(ex.render())
        print()

    errors = [f for f in findings if f.severity == "ERROR"]
    warns = [f for f in findings if f.severity == "WARN"]
    for f in findings:
        print(f.render())
    print(f"lint_tms_lib: {len(files)} files, "
          f"{sum(fl.stats.data_accesses for fl in linters)} $CC00 accesses, "
          f"{sum(fl.stats.gaps_checked for fl in linters)} gaps checked, "
          f"{len(exemptions)} exemptions granted, "
          f"{len(errors)} errors, {len(warns)} warnings")
    if errors or (args.strict and warns):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
