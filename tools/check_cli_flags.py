#!/usr/bin/env python3
"""Fail when POM1's CLI flags, its --help text and doc/CLI.md disagree.

WHY THIS EXISTS
    Before --help existed, the unknown-flag error read "Run with --help for the
    supported list" — pointing at a flag the parser did not accept. The person
    who most needed the list was handed a dead end, and nothing in the tree
    could notice, because a help text is only ever checked by someone reading
    it. Adding the flag without adding this guard would just move the rot one
    step: a new flag lands in the parser, the table keeps quiet, and the help
    is wrong again.

    Fourth guard of the family — after tools/check_version_sync.sh
    (version_sync), tools/check_imgui_pin.sh (imgui_pin_sync) and
    tools/check_doc_paths.py (doc_paths_sync): pick one fact several places
    must agree on, and let ctest hold it.

THE THREE SETS
    parser  every `arg == "--x"` / `arg == "-x"` in src/CliDispatcher.cpp —
            what POM1 actually accepts, and therefore the authority.
    help    every flag spelled in kCliFlagHelp[] in the same file — what
            `POM1 --help` prints.
    doc     every flag in a table row of doc/CLI.md — the full reference.

    All three must name the same flags. A flag the parser accepts but neither
    other set mentions is undiscoverable; a flag they promise but the parser
    rejects is the dead end above.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI_CPP = ROOT / "src" / "CliDispatcher.cpp"
CLI_DOC = ROOT / "doc" / "CLI.md"

# A flag never starts mid-word: the leading dash must not follow an alphanumeric.
# Without that guard the "YYYY-MM-DD" in --rtc-freeze's own argument reads as a
# flag named -MM-DD.
FLAG_RE = re.compile(r"(?<![A-Za-z0-9])(-{1,2}[A-Za-z][A-Za-z0-9-]*)")


def strip_comments(src: str) -> str:
    """Drop // and /* */ comments — the prose around the parser mentions
    `arg == "--x"` as an example, and an example is not a flag."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def parser_flags(src: str) -> set:
    """Flags the parse loop compares argv against."""
    return set(re.findall(r'arg == "(-{1,2}[A-Za-z][A-Za-z0-9-]*)"', strip_comments(src)))


def help_flags(src: str) -> set:
    """Flags spelled in the kCliFlagHelp[] rows."""
    start = src.index("constexpr CliFlagHelp kCliFlagHelp[]")
    end = src.index("};", start)
    flags = set()
    for row in re.finditer(r"\{'[ABC]',\s*\"((?:[^\"\\]|\\.)*)\"", src[start:end]):
        flags.update(FLAG_RE.findall(row.group(1)))
    return flags


def doc_flags(doc: str) -> set:
    """Flags named in the first column of a doc/CLI.md table row."""
    flags = set()
    for line in doc.splitlines():
        if not line.startswith("| `-"):
            continue
        # Split on UNESCAPED pipes only: the table writes `--preset <N\\|name>`,
        # and a naive split cuts that row in half.
        first_col = re.split(r"(?<!\\)\|", line)[1]
        for span in re.findall(r"`([^`]+)`", first_col):
            flags.update(FLAG_RE.findall(span))
    return flags


def report(title: str, missing: set) -> None:
    print(f"  {title}:")
    for f in sorted(missing):
        print(f"    {f}")


def main() -> int:
    src = CLI_CPP.read_text(encoding="utf-8")
    doc = CLI_DOC.read_text(encoding="utf-8")

    parser, helped, documented = parser_flags(src), help_flags(src), doc_flags(doc)
    if not parser or not helped or not documented:
        print("check_cli_flags: one of the three sets came back EMPTY — the "
              "extraction, not the tree, is what broke.", file=sys.stderr)
        return 2

    problems = 0
    for name, other in (("--help (kCliFlagHelp[])", helped), ("doc/CLI.md", documented)):
        missing = parser - other
        if missing:
            problems += len(missing)
            print(f"FAIL: accepted by the parser but absent from {name}")
            report("flags", missing)
        extra = other - parser
        if extra:
            problems += len(extra)
            print(f"FAIL: promised by {name} but REJECTED by the parser")
            report("flags", extra)

    if problems:
        print(f"\n{problems} disagreement(s). src/CliDispatcher.cpp is the authority: "
              f"a flag it accepts must appear in kCliFlagHelp[] and in doc/CLI.md.")
        return 1

    print(f"OK: {len(parser)} CLI flags — parser, --help and doc/CLI.md agree.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
