#!/usr/bin/env python3
"""Fail when the two assembly video backends stop offering the same API.

WHAT THIS IS ABOUT
    `dev/lib/` has one program that runs on TWO cards from ONE source. LOGO is
    5 426 lines under sketchs/tms9918/tool_logo/; sketchs/gen2/tool_logo_gen2/
    is THIRTY-ONE lines that define LOGO_GEN2 and `.include` it. What makes that
    work is a link-time backend, exactly the arrangement dev/lib/gfx/ uses for
    the C runtimes and for the same reason — Parmigiani's one-board rule means a
    binary ever talks to one video card, so the choice can be made by ld65 with
    no runtime dispatch:

        dev/lib/tms9918/tms9918m2.asm   → the TMS9918 Mode-2 implementation
        dev/lib/gen2/gen2_logom2.asm    → the same symbols, on GEN2 HGR

    So the apparent duplicates between the two directories — `bubble.asm` vs
    `gen2_bubble.asm`, `text_bitmap.asm` vs `gen2_text_bitmap.asm` — are NOT two
    copies of one routine. They are the two sides of one interface, and each
    says so in its own header ("Drop-in replacement … same public symbol").
    What genuinely cannot be shared is documented in place: HGR is 280 px wide,
    so its X needs nine bits (`line_xy16`, `plot_set_x16`) where the TMS's 256
    fits in eight.

WHY IT NEEDS A GUARD
    The contract is held by nothing but the link. Add a routine to one backend
    and forget the other, and the failure is an `ld65: Unresolved external` in
    a build most people never run — the GEN2 LOGO. This asserts, without
    building anything, that every backend symbol the shared source imports is
    exported by BOTH sides.

Run from the repo root: python3 tools/check_asm_backends.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "dev" / "lib"

SHARED_SOURCE = ROOT / "sketchs" / "tms9918" / "tool_logo" / "TMS_Logo_16k.asm"
GEN2_BACKEND = [LIB / "gen2" / "gen2_logom2.asm",
                LIB / "gen2" / "gen2_text_bitmap.asm",
                LIB / "gen2" / "gen2_bubble.asm"]
TMS_BACKEND = [LIB / "tms9918" / "tms9918m2.asm",
               LIB / "tms9918" / "tms9918m1.asm",
               LIB / "tms9918" / "tms9918_helpers.asm",
               LIB / "tms9918" / "tms9918_text.asm",
               LIB / "tms9918" / "tms9918_pad.asm",
               LIB / "tms9918" / "text_bitmap.asm",
               LIB / "tms9918" / "bubble.asm"]

# `[^;\n]` and NOT `[^;]`: a character class that omits the newline still
# matches one, so the greedy form swallowed the following lines up to the next
# comment and the LAST symbol of every directive was silently lost. The `zp`
# forms come first in the alternation for the same reason — ordered alternation
# would otherwise match `export` inside `.exportzp` and leave "zp" glued to the
# first symbol.
DIRECTIVE = re.compile(r"^\s*\.(exportzp|importzp|export|import)\s+([^;\n]*)", re.M)


def symbols(path: Path, kind: str) -> set[str]:
    """Every symbol a file exports or imports. Handles `\\` continuations and
    the `.export a, b` / `.exportzp c` forms the tree actually uses."""
    if not path.is_file():
        return set()
    text = re.sub(r"\\\s*\n", " ", path.read_text(errors="replace"))
    out: set[str] = set()
    for m in DIRECTIVE.finditer(text):
        if not m.group(1).startswith(kind):
            continue
        for s in m.group(2).split(","):
            s = s.strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", s):
                out.add(s)
    return out


def union(paths, kind: str) -> set[str]:
    out: set[str] = set()
    for p in paths:
        out |= symbols(p, kind)
    return out


def main() -> int:
    if not SHARED_SOURCE.is_file():
        print(f"skip: {SHARED_SOURCE.relative_to(ROOT)} not found")
        return 0

    text = SHARED_SOURCE.read_text(errors="replace")
    # The `.ifdef LOGO_GEN2` blocks are the DOCUMENTED seam — the 9-bit-X
    # routines HGR needs and the TMS cannot have. They are deliberately
    # one-sided, so they are excluded from the shared contract and checked
    # against the GEN2 backend alone.
    shared_text = re.sub(r"\.ifdef\s+LOGO_GEN2.*?\.endif", "", text, flags=re.S)
    seam_text = "\n".join(re.findall(r"\.ifdef\s+LOGO_GEN2(.*?)\.endif", text, re.S))

    gen2_exports = union(GEN2_BACKEND, "export")
    tms_exports = union(TMS_BACKEND, "export")
    backend_symbols = gen2_exports | tms_exports

    imported_shared = set()
    for m in DIRECTIVE.finditer(re.sub(r"\\\s*\n", " ", shared_text)):
        if not m.group(1).startswith("import"):
            continue
        for s in m.group(2).split(","):
            s = s.strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", s):
                imported_shared.add(s)
    # Only what the BACKENDS provide — the same source also imports sprite
    # patterns, m6502 math and text helpers, which are card-neutral already.
    contract = imported_shared & backend_symbols

    imported_seam = set()
    for m in DIRECTIVE.finditer(re.sub(r"\\\s*\n", " ", seam_text)):
        if not m.group(1).startswith("import"):
            continue
        for s in m.group(2).split(","):
            s = s.strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", s):
                imported_seam.add(s)

    problems: list[str] = []
    if not contract:
        problems.append("derived an EMPTY backend contract — the extraction is "
                        "broken, not the tree")
    for sym in sorted(contract):
        if sym not in gen2_exports:
            problems.append(f"'{sym}' is used by the shared LOGO source and "
                            f"exported by the TMS backend, but NOT by the GEN2 "
                            f"one — the GEN2 build will not link")
        if sym not in tms_exports:
            problems.append(f"'{sym}' is used by the shared LOGO source and "
                            f"exported by the GEN2 backend, but NOT by the TMS "
                            f"one — the TMS build will not link")
    for sym in sorted(imported_seam & backend_symbols):
        if sym not in gen2_exports:
            problems.append(f"'{sym}' is in the LOGO_GEN2 seam but the GEN2 "
                            f"backend does not export it")

    if problems:
        print("asm_backends_sync: FAIL")
        for p in problems:
            print(f"  {p}")
        print("\nThe two backends are one interface with two implementations "
              "(dev/lib/README.md). Adding a routine to one and not the other "
              "breaks a build most people never run.")
        return 1

    print(f"OK: {len(contract)} backend symbols shared by both video backends, "
          f"{len(imported_seam & backend_symbols)} in the documented "
          f"GEN2-only 9-bit-X seam.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
