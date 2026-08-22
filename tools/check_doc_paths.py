#!/usr/bin/env python3
"""Fail when the documentation cites a source file that does not exist.

POM1 carries 26 000 lines of markdown against 81 000 lines of C++ — an unusually
good ratio, and an unusually large surface for silent drift. A file gets renamed
or split, a dozen documents keep naming the old path, and nothing notices until
somebody reads a paragraph and goes looking. The git history already contains a
manual "20 dérives corrigées" pass; that is the work this script exists to stop
repeating.

It is the third guard of its kind, after tools/check_version_sync.sh
(version_sync) and tools/check_imgui_pin.sh (imgui_pin_sync): pick one fact the
docs must agree with the tree about, and let ctest hold it.

WHAT COUNTS AS A CITATION
    A backtick span that looks like a source path — it carries a directory
    separator and a known code/config extension. `Memory.cpp` on its own is a
    bare basename and is deliberately NOT checked: CLAUDE.md says such names are
    to be found under src/, and enforcing that would flag every prose mention.

HOW A CITATION IS RESOLVED  (any one hit is enough)
    1. straight from the repo root                     src/Memory.cpp
    2. relative to the citing document's directory     ../cc65/apple1.cfg
    3. under src/, the documented convention           bench/CodeBench.cpp
    4. under dev/, as the dev/lib READMEs write it     lib/m6502/math.asm
    5. under dev/lib/, the library-relative form       m6502/math.asm

Placeholders (NN, <name>, globs) are skipped — they name a shape, not a file.
So are paths rooted in another project or toolkit (see EXTERNAL_ROOTS), and
CHANGELOG.md as a whole: a changelog is a record of what the tree WAS, and
naming a file that has since been renamed is what it is supposed to do.

Exit codes: 0 clean · 1 unresolved citations found.
"""

import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CODE_EXT = ("cpp", "h", "hpp", "mm", "s", "asm", "inc", "c",
            "py", "sh", "bat", "cfg", "json", "yml", "ini", "txt", "md")

CITATION = re.compile(r"`([A-Za-z0-9_./-]+\.(?:" + "|".join(CODE_EXT) + r"))`")

# A citation that names a shape rather than a file. `imgui_preset_NN.ini` and
# `preset_NN.size` are real, documented filename TEMPLATES; flagging them would
# push the docs toward being less precise, not more.
PLACEHOLDER = re.compile(r"[*?<>]|\bNN\b|_NN|NN\.|XX|\.\.\.|%s|\{")

# Directories whose markdown is not POM1's to keep in sync.
SKIP_DIRS = ("build", ".git", "src/third_party", "imgui", "fpga")

# A changelog documents the tree as it was at each release; entries naming a
# file that has since moved are correct history, not drift. Excluded whole.
SKIP_FILES = ("CHANGELOG.md",)

# Roots that legitimately name a file OUTSIDE this repository. Each is a real
# citation POM1's docs need to make — they just cannot be checked here.
EXTERNAL_ROOTS = (
    "POM2/",     # the sibling Apple II emulator, its own repository
    "demos/",    # upstream nino-d/tms9918 demos the dev/lib ports came from
    "GLFW/",     # toolkit headers, not repo files
    "GL/", "GLES3/",
    "docs/",     # upstream projects' own doc trees
)


def markdown_files():
    """Tracked .md files, or a filesystem walk when git is unavailable."""
    try:
        out = subprocess.run(["git", "-C", REPO, "ls-files", "*.md"],
                             capture_output=True, text=True, check=True).stdout
        files = out.split()
    except (subprocess.CalledProcessError, FileNotFoundError):
        files = []
        for root, dirs, names in os.walk(REPO):
            dirs[:] = [d for d in dirs if not d.startswith(".")]
            for n in names:
                if n.endswith(".md"):
                    files.append(os.path.relpath(os.path.join(root, n), REPO))
    return [f for f in sorted(files)
            if not any(f.startswith(d) for d in SKIP_DIRS)
            and os.path.basename(f) not in SKIP_FILES]


def resolves(cited, doc_dir):
    """True if `cited` names a file that exists, under any documented convention."""
    for candidate in (cited,
                      os.path.normpath(os.path.join(doc_dir, cited)),
                      os.path.join("src", cited),
                      os.path.join("dev", cited),
                      os.path.join("dev", "lib", cited)):
        if os.path.exists(os.path.join(REPO, candidate)):
            return True
    return False


def main():
    checked = 0
    broken = []

    for doc in markdown_files():
        doc_dir = os.path.dirname(doc)
        try:
            with open(os.path.join(REPO, doc), encoding="utf-8", errors="ignore") as fh:
                text = fh.read()
        except OSError:
            continue

        for cited in sorted(set(CITATION.findall(text))):
            if "/" not in cited or PLACEHOLDER.search(cited):
                continue
            # ".cpp/.h" and friends: prose about extensions, not a path.
            if cited.startswith("."):
                continue
            if cited.startswith(EXTERNAL_ROOTS):
                continue
            checked += 1
            if not resolves(cited, doc_dir):
                broken.append((doc, cited))

    if broken:
        print(f"{len(broken)} documentation citation(s) name a file that does not exist:\n")
        current = None
        for doc, cited in broken:
            if doc != current:
                print(f"  {doc}")
                current = doc
            print(f"      {cited}")
        print(f"\nChecked {checked} cited paths across the markdown tree.")
        print("Fix the document, or the path — whichever moved. If the citation is a\n"
              "filename TEMPLATE rather than a real file, it belongs in PLACEHOLDER\n"
              "in this script.")
        return 1

    print(f"OK: {checked} cited source paths all resolve.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
