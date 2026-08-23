#!/usr/bin/env python3
"""check_window_registry.py — keep the panel registry the single window list.

POM1's MainWindow spans 17 000 lines across 16 translation units and owns 68
windows. Those 68 used to be recited by hand in four places: the registry
(persistence), 51 `if (showX) renderX();` dispatch lines, the toggles scattered
across eight menus, and kDockLayout[]. Four lists over one set is how a window
ends up openable but never saved, saved but missing from the menu, or added to
the menu and never docked — each failure silent, and none of it reachable by a
test, because the UI is deliberately excluded from every test binary.

The registry (MainWindow_ImGui::windowRegistry, MainWindow_Presets.cpp) is now
the one list. This guard is what keeps it that way. It checks, statically:

  1. every `bool show*` member declared in MainWindow_ImGui.h has a registry row
     — a window that is not in the table gets no persistence and no menu entry;
  2. every registry row either carries a `render` member or is named by a
     bespoke `if (show…)` block in MainWindow_ImGui.cpp — a row with neither is
     a window that can be toggled and will never draw;
  3. no `if (showX) renderX();` dispatch line has come back into
     MainWindow_ImGui.cpp — that line is the fifth list re-forming, and it is
     the only one of the four failures that is invisible in review.

Exit 0 = the registry is the single source. Exit 1 = drift, with the drift named.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HDR = REPO / "src" / "MainWindow_ImGui.h"
REG = REPO / "src" / "MainWindow_Presets.cpp"
DISPATCH = REPO / "src" / "MainWindow_ImGui.cpp"


def die(problems):
    print("check_window_registry: the window list has DRIFTED\n", file=sys.stderr)
    for p in problems:
        print(f"  - {p}", file=sys.stderr)
    print(
        "\nwindowRegistry() (src/MainWindow_Presets.cpp) is the single list of\n"
        "POM1's windows: persistence, the dispatch loop and the menus all read it.\n"
        "Adding a window means adding ONE row there.",
        file=sys.stderr,
    )
    sys.exit(1)


def registry_body(text):
    start = text.index("static const std::vector<WindowDescriptor> kReg = {")
    return text[start : text.index("\n    };", start)]


def main():
    for f in (HDR, REG, DISPATCH):
        if not f.is_file():
            die([f"missing source file: {f.relative_to(REPO)}"])

    hdr = HDR.read_text(encoding="utf-8")
    reg = registry_body(REG.read_text(encoding="utf-8"))
    disp = DISPATCH.read_text(encoding="utf-8")

    problems = []

    # -- 1. every show* flag has a row ---------------------------------------
    flags = set(re.findall(r"^\s+bool\s+(show[A-Za-z0-9_]+)\s*(?:=|;)", hdr, re.M))
    rows = re.findall(
        r'\{\s*"([^"]+)"\s*,\s*"[^"]*"\s*,\s*&MW::(show[A-Za-z0-9_]+)\s*,', reg
    )
    in_reg = {f for _, f in rows}
    if not flags:
        die(["parsed MainWindow_ImGui.h but found no `bool show*` members"])
    if not rows:
        die(["parsed the registry but found no rows"])

    for f in sorted(flags - in_reg):
        problems.append(
            f"`{f}` is declared in MainWindow_ImGui.h but has NO registry row "
            f"(it gets no persistence and no menu entry)"
        )
    for f in sorted(in_reg - flags):
        problems.append(f"registry names `{f}`, which is not a member of MainWindow_ImGui")

    # -- 2. every row either renders from the table or has a bespoke block ----
    #    A bespoke block is any `if (show… )` mention of the flag in render()'s
    #    TU that is not the (now-banned) one-line dispatch form.
    with_render = set(
        re.findall(
            r"&MW::(show[A-Za-z0-9_]+)\s*,\s*K::\w+\s*,\s*(?:true|false)\s*,\s*&MW::render",
            reg,
        )
    )
    bespoke = set(re.findall(r"\bif\s*\([^)]*\b(show[A-Za-z0-9_]+)\b", disp))
    # The eight photo windows are drawn by one table-driven helper, not by an
    # `if` of their own; treat that helper as their bespoke block.
    if "renderSimplePhotoWindows()" in disp:
        bespoke |= {f for _, f in rows if "Photo" in f}

    for key, f in rows:
        if f in with_render or f in bespoke:
            continue
        problems.append(
            f'row "{key}" (`{f}`) has no `render` member and no bespoke block in '
            f"MainWindow_ImGui.cpp — that window can be toggled and will never draw"
        )

    # -- 3. the one-line dispatch form must not come back ---------------------
    revived = re.findall(
        r"^\s*if \((?:[a-z][A-Za-z0-9_]*Enabled && )?(show[A-Za-z0-9_]+)\) "
        r"(render[A-Za-z0-9_]+)\(\);",
        disp,
        re.M,
    )
    for f, fn in revived:
        problems.append(
            f"`if ({f}) {fn}();` is back in MainWindow_ImGui.cpp — put the "
            f"function in the registry row's `render` field instead, or the "
            f"parallel dispatch list starts over"
        )

    if problems:
        die(problems)

    print(
        f"check_window_registry: OK — {len(rows)} windows, one list "
        f"({len(with_render)} dispatched from the table, "
        f"{len(rows) - len(with_render)} bespoke)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
