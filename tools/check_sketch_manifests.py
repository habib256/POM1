#!/usr/bin/env python3
"""Fail when a sketch has no manifest, or when its two manifests disagree.

WHY THIS EXISTS
    A sketch under `sketchs/` could be described in `.sketch.json`, in a
    `Makefile`, in both, or in neither — and which regime it was in could not be
    told without opening it. Measured before this guard: 36 sketches carried the
    sidecar alone, 5 the Makefile alone, 10 both, and 6 neither.

    That is not a tidiness problem. `Pom1BenchCc65::probeAsmProject` reads BOTH,
    and reads the Makefile LITERALLY — `LOAD_CFG` and `EXTRA_ASM` with no
    `$(VAR)` expansion, a rule whose violation once "silently assembled nothing
    and the link died". So a sketch whose Makefile spells its linker config as
    `CFG :=` (which the probe does not read) built one way under `make` and
    another way in the DevBench, from the same source. `tools/tool_diapo` and
    `tool_tmsload` were doing exactly that: `apple1_tmsutil.cfg` for `make`,
    the TMS target's `codetank.cfg` in the Bench.

WHAT IS ASSERTED
    1. Every sketch directory holding a buildable source has a `.sketch.json`.
    2. It declares a `profile` and a `language`, both from the known sets.
    3. Every path it names — `cfg`, `extraAsm`, `incDirs` — exists.
    4. Where a `Makefile` ALSO exists, the two agree: the config it links with
       and the modules it assembles resolve to the same files. Disagreement is
       the defect above, and this is the assertion that makes it impossible.

    A sidecar may omit `cfg`: that means "the DevBench target's default
    applies", which is what a sketch with no manifest at all used to get
    implicitly. Omission is a statement; a WRONG cfg is not.

Run from the repo root: python3 tools/check_sketch_manifests.py
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKETCHES = ROOT / "sketchs"

PROFILES = {"apple1", "gen2", "tms9918", "portable",
            "basic_applesoft", "basic_integer", "logo"}
LANGUAGES = {"asm", "c", "basic", "logo", "hex"}
SOURCE_SUFFIXES = {".asm", ".s", ".c"}

# The Makefile variables the DevBench actually reads (Pom1BenchCc65.cpp).
MAKE_VARS = ("LOAD_CFG", "EXTRA_ASM", "CFG", "INC", "LIB")



# ── The DevBench auto-link contract ──────────────────────────────────────────
#
# A sketch that names no `cfg` builds through the Bench's auto-link: it scans
# the source for symbols it knows and pulls the matching dev/lib/tms9918 module
# in (Pom1BenchHost.cpp, kTmsLibSyms). That table is hand-maintained, so a
# sketch can import a symbol nothing in it provides and fail at ld65 — which is
# exactly what TMS_RogueDiag.asm did: `init_vdp_g1`, `vdp_set_write`, `vdp_hi`
# and `vdp_lo` all unresolved, because the table lacked three of them and the
# fourth was skipped by a "defined locally" probe that matched a COMMENT.
#
# This asserts the contract: for a sketch on the auto-link path, every symbol it
# imports must be reachable from the modules that table would select.

ASM_TABLE_RE = re.compile(r'\{\s*"([a-z_0-9]+)",\s*"(tms9918[a-z_0-9.]*\.asm)"')
BENCH_HOST = ROOT / "src" / "Pom1BenchHost.cpp"
TMS_LIB = ROOT / "dev" / "lib" / "tms9918"


def strip_asm_comments(text: str) -> str:
    """ca65 comments start at ';'. Reading them as code is the bug this guard
    exists for, so every scan below runs on the stripped text."""
    return "\n".join(line.split(";", 1)[0] for line in text.splitlines())


def asm_symbols(text: str, kind: str) -> set[str]:
    out: set[str] = set()
    for m in re.finditer(rf"(?m)^\s*\.{kind}(?:zp)?\s+([^;\n]*)", text):
        for sym in m.group(1).split(","):
            sym = sym.strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", sym):
                out.add(sym)
    return out


def autolink_table() -> dict[str, str]:
    if not BENCH_HOST.is_file():
        return {}
    return dict(ASM_TABLE_RE.findall(BENCH_HOST.read_text(errors="replace")))


def module_exports(name: str) -> set[str]:
    f = TMS_LIB / name
    return asm_symbols(f.read_text(errors="replace"), "export") if f.is_file() else set()


def make_vars(path: Path) -> dict[str, str]:
    """Read the declarative half of a Makefile the way the DevBench does:
    literally, first assignment wins, backslash continuations folded, and NO
    `$(VAR)` expansion — because the C++ side cannot expand one either."""
    text = re.sub(r"\\\n\s*", " ", path.read_text(errors="replace"))
    out: dict[str, str] = {}
    for line in text.splitlines():
        m = re.match(r"\s*(" + "|".join(MAKE_VARS) + r")\s*[:?]?=\s*(.*)", line)
        if m:
            out.setdefault(m.group(1), m.group(2).strip())
    return out


def resolve(base: Path, spec: str) -> Path | None:
    """A sidecar path is repo-relative; a Makefile path is sketch-relative.
    Accept either and report where it landed."""
    for candidate in (ROOT / spec, base / spec):
        try:
            p = candidate.resolve()
        except OSError:
            continue
        if p.exists():
            return p
    return None


def main() -> int:
    problems: list[str] = []
    checked = 0
    with_cfg = 0
    cross_checked = 0
    scripts = 0

    for d in sorted(p for p in SKETCHES.glob("*/*") if p.is_dir()):
        sources = [f for f in d.iterdir()
                   if f.is_file() and f.suffix in SOURCE_SUFFIXES]
        if not sources:
            continue
        checked += 1
        rel = d.relative_to(ROOT)
        sidecar = d / ".sketch.json"

        if not sidecar.is_file():
            problems.append(f"{rel}: no .sketch.json (sources: "
                            f"{', '.join(s.name for s in sources[:3])})")
            continue

        try:
            spec = json.loads(sidecar.read_text())
        except json.JSONDecodeError as e:
            problems.append(f"{rel}/.sketch.json: not valid JSON — {e}")
            continue

        profile, language = spec.get("profile"), spec.get("language")
        if profile not in PROFILES:
            problems.append(f"{rel}: profile {profile!r} is not one of "
                            f"{sorted(PROFILES)}")
        if language not in LANGUAGES:
            problems.append(f"{rel}: language {language!r} is not one of "
                            f"{sorted(LANGUAGES)}")

        # Every path it names must exist.
        cfg_path = None
        if spec.get("cfg"):
            with_cfg += 1
            cfg_path = resolve(d, spec["cfg"])
            if cfg_path is None:
                problems.append(f"{rel}: cfg {spec['cfg']!r} does not exist")
        for key in ("extraAsm", "incDirs"):
            for entry in spec.get(key, []):
                if resolve(d, entry) is None:
                    problems.append(f"{rel}: {key} {entry!r} does not exist")

        # Where both manifests exist, they must agree.
        mk = d / "Makefile"
        if not mk.is_file():
            continue
        mv = make_vars(mk)
        # A Makefile that declares NONE of the build variables is not a second
        # manifest — it is a script. sketchs/gen2/a2port_buzzard_bait's only
        # rule disassembles and re-assembles its source to prove the port is
        # faithful; it describes no build to disagree about. Counting it as a
        # rival description would make this guard demand a declaration that has
        # nothing to declare.
        if not any(mv.get(v) for v in ("LOAD_CFG", "EXTRA_ASM", "CFG")):
            scripts += 1
            continue
        cross_checked += 1

        mk_cfg_spec = mv.get("LOAD_CFG", "")
        if mk_cfg_spec == "$(CFG)":
            mk_cfg_spec = mv.get("CFG", "")
        if not mk_cfg_spec:
            mk_cfg_spec = mv.get("CFG", "")
        if mk_cfg_spec:
            mk_cfg = resolve(d, mk_cfg_spec)
            if mk_cfg is None:
                problems.append(f"{rel}/Makefile: config {mk_cfg_spec!r} does not exist")
            elif cfg_path is None:
                problems.append(
                    f"{rel}: the Makefile links with {mk_cfg.relative_to(ROOT)} "
                    f"but .sketch.json names no cfg — the DevBench would use the "
                    f"target default and build a different binary")
            elif mk_cfg != cfg_path:
                problems.append(
                    f"{rel}: the two manifests disagree on the linker config — "
                    f"Makefile {mk_cfg.relative_to(ROOT)} vs .sketch.json "
                    f"{cfg_path.relative_to(ROOT)}")

        mk_asm = {resolve(d, t) for t in mv.get("EXTRA_ASM", "").split() if t}
        mk_asm.discard(None)
        json_asm = {resolve(d, t) for t in spec.get("extraAsm", [])}
        json_asm.discard(None)
        if mk_asm != json_asm:
            only_mk = sorted(str(p.relative_to(ROOT)) for p in mk_asm - json_asm)
            only_js = sorted(str(p.relative_to(ROOT)) for p in json_asm - mk_asm)
            detail = []
            if only_mk:
                detail.append(f"only in the Makefile: {', '.join(only_mk)}")
            if only_js:
                detail.append(f"only in .sketch.json: {', '.join(only_js)}")
            problems.append(f"{rel}: the two manifests disagree on the assembled "
                            f"modules — {'; '.join(detail)}")

    # ── auto-link reachability ───────────────────────────────────────────
    table = autolink_table()
    autolinked = 0
    for d in sorted(p for p in SKETCHES.glob("*/*") if p.is_dir()):
        sidecar = d / ".sketch.json"
        if not sidecar.is_file():
            continue
        try:
            spec = json.loads(sidecar.read_text())
        except json.JSONDecodeError:
            continue
        # Only the auto-link path: a sketch that names a cfg (or a Makefile that
        # does) states its own modules and is checked above.
        if spec.get("cfg") or spec.get("extraAsm") or (d / "Makefile").is_file():
            continue
        if spec.get("language") != "asm" or spec.get("profile") != "tms9918":
            continue

        imports: set[str] = set()
        own: set[str] = set()
        for f in d.iterdir():
            if f.suffix not in (".asm", ".s"):
                continue
            code = strip_asm_comments(f.read_text(errors="replace"))
            imports |= asm_symbols(code, "import")
            own |= asm_symbols(code, "export")
        if not imports:
            continue
        autolinked += 1

        mods = {table[s] for s in imports if s in table}
        if mods:
            mods.add("tms9918_pad.asm")     # every module JSRs the pad itself
        available: set[str] = set()
        for m in mods:
            available |= module_exports(m)
        unresolved = sorted(imports - available - own)
        if unresolved:
            problems.append(
                f"{d.relative_to(ROOT)}: builds through the DevBench auto-link, "
                f"but {', '.join(unresolved)} " +
                ("is" if len(unresolved) == 1 else "are") +
                " reachable from no module its table would select — ld65 will "
                f"report {'it' if len(unresolved) == 1 else 'them'} unresolved. "
                f"Add the symbol to kTmsLibSyms in src/Pom1BenchHost.cpp, or give "
                f"the sketch a cfg + extraAsm.")

    if problems:
        print("sketch_manifests_sync: FAIL")
        for p in problems:
            print(f"  {p}")
        print(f"\n{len(problems)} problem(s) across {checked} sketches.")
        print("Every sketch needs a .sketch.json, and where a Makefile also "
              "describes the build the two must name the same files — the "
              "DevBench and `make` read different ones.")
        return 1

    print(f"OK: {checked} sketches, all with a .sketch.json "
          f"({with_cfg} naming a linker config, {cross_checked} cross-checked "
          f"against a sibling Makefile, {scripts} whose Makefile is a script "
          f"rather than a second manifest, {autolinked} resolved through the "
          f"DevBench auto-link table).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
