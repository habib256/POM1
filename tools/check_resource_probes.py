#!/usr/bin/env python3
"""Fail when a source file hand-rolls its own "walk up from the cwd" probe.

POM1's data — roms/, software/, cassettes/, sdcard/, fonts/, pic/, dev/,
ini_defaults/ — is found by ONE search order, `pom1::ResourceLocator`
(src/ResourceLocator.h). Before that class every consumer re-implemented "try
`x`, then `../x`, then `../../x`", and they did not agree on how far to climb:
`Memory::loadROM()` went up one level, the sdcard/disks/cfcard probes two, the
CodeTank probe three. Run POM1 from `build/tests/` and it found the disk images
but not the ROMs, then quietly substituted its built-in Woz Monitor and carried
on. Several of them also carried an exe-relative half spelled out under
`#if defined(_WIN32)` and nowhere else, so a Linux or macOS build launched from
outside the tree simply showed no photo and no icon font.

The class ended that. This script is what keeps it ended: the drift never comes
back all at once, it comes back one call site at a time, each of them locally
reasonable. Fourth guard of the version_sync / imgui_pin_sync / doc_paths_sync
family — pick one fact the tree must keep, and let ctest hold it.

WHAT IS FLAGGED
    A `"../` string literal in POM1's own C++ (src/, excluding third_party).
    That is the whole signature of a hand-rolled walk, and it is not otherwise
    a thing POM1 writes: a path RELATIVE to something already resolved is
    composed with std::filesystem, not spelled with dots.

WHAT IS NOT
    Comment text (the rule is about what the code does, not how it is
    described) and the ALLOWED sites below, each of which is a `../` that is
    not a probe at all.

Exit codes: 0 clean · 1 a new hand-rolled probe appeared.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "src")

# `../` occurrences that are not resource probes. Keyed by repo-relative path;
# the value is the substring the line must contain, so an allowlisted FILE
# cannot quietly grow a real probe on some other line.
ALLOWED = {
    # Guest-side path traversal, not a host lookup: the SD CARD OS refuses a
    # CPU-supplied name that escapes the sdcard root.
    "src/MicroSD.cpp": 'normalized.compare(0, 3, "../")',
    # The "parent directory" row of each portable editor's built-in file
    # browser — a label the user clicks, not a place to look for POM1's data.
    "src/hgrpaint/HgrPaintEditor.cpp": 'ImGui::Selectable("../"',
    "src/tmspaint/TmsPaintEditor.cpp": 'ImGui::Selectable("../"',
    "src/hgrsprite/HgrSpriteEditor.cpp": 'ImGui::Selectable("../"',
    "src/tmssprite/TmsSpriteEditor.cpp": 'ImGui::Selectable("../"',
}

SUFFIXES = (".cpp", ".h", ".hpp", ".mm", ".c")
PROBE = re.compile(r'"\.\./')


def strip_comments(line: str) -> str:
    """Drop // comments. Block comments are handled by the caller's state."""
    cut = line.find("//")
    return line if cut < 0 else line[:cut]


def scan(path: str, rel: str) -> list:
    hits = []
    in_block = False
    with open(path, encoding="utf-8", errors="replace") as fh:
        for number, raw in enumerate(fh, start=1):
            line = raw
            if in_block:
                end = line.find("*/")
                if end < 0:
                    continue
                line, in_block = line[end + 2:], False
            # A /* opened and not closed on this line hides the rest of it.
            start = line.find("/*")
            while start >= 0:
                end = line.find("*/", start + 2)
                if end < 0:
                    line, in_block = line[:start], True
                    break
                line = line[:start] + " " + line[end + 2:]
                start = line.find("/*")
            line = strip_comments(line)
            if not PROBE.search(line):
                continue
            allowed = ALLOWED.get(rel)
            if allowed and allowed in raw:
                continue
            hits.append((number, raw.rstrip()))
    return hits


def main() -> int:
    findings = []
    scanned = 0
    for root, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != "third_party"]
        for name in sorted(files):
            if not name.endswith(SUFFIXES):
                continue
            path = os.path.join(root, name)
            # Forward slashes ALWAYS: os.path.relpath hands back `src\\MicroSD.cpp`
            # on Windows, so an ALLOWED key written `src/MicroSD.cpp` matched
            # nothing there and the five legitimate sites were reported as
            # findings — green on Linux and macOS, red on Windows only.
            rel = os.path.relpath(path, REPO).replace(os.sep, "/")
            scanned += 1
            findings.extend((rel, n, text) for n, text in scan(path, rel))

    if findings:
        print(f"{len(findings)} hand-rolled resource probe(s) found:\n")
        current = None
        for rel, number, text in findings:
            if rel != current:
                print(f"  {rel}")
                current = rel
            print(f"      {number}: {text.strip()}")
        print("\nPOM1 has ONE search order for its data: pom1::ResourceLocator"
              " (src/ResourceLocator.h).\n"
              "Use defaultLocator().find(\"roms/x.rom\") or .findDirectory(\"software\")"
              " instead of\n"
              "spelling out cwd ancestors — the locator also covers the"
              " executable-relative\n"
              "packaged layouts (macOS Resources/, AppImage share/POM1/), which a"
              " hand-written\n"
              "list never does. If the `../` genuinely is not a probe, add the site"
              " to ALLOWED\n"
              "in this script with the reason.")
        return 1

    print(f"OK: no hand-rolled resource probes in {scanned} source files "
          f"({len(ALLOWED)} allowlisted non-probe sites).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
