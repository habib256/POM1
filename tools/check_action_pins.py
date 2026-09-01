#!/usr/bin/env python3
"""Fail when a GitHub Action is referenced by tag instead of by commit SHA.

`uses: actions/checkout@v5` looks like a version. It is not: `v5` is a mutable
pointer in someone else's repository, and whoever controls that repository can
move it at any time. POM1's workflows run with the repo's secrets — release.yml
builds, signs and publishes the binaries users download, and pages.yml deploys
the site — so a moved tag is arbitrary code in the job that ships the product.
A 40-hex SHA is the only reference that means "this exact code".

The pins are kept current by .github/dependabot.yml, which rewrites the SHA and
the trailing `# vX.Y.Z` comment together. That comment is required here, not
decorative: a bare SHA tells a reviewer nothing about which version a diff is
moving to or away from, and a pin nobody can read is a pin nobody updates.

Fifth guard of the version_sync / imgui_pin_sync / doc_paths_sync /
resource_probes_sync family. It is deliberately OFFLINE — it never asks GitHub
whether a SHA exists, because a test that needs the network is a test that goes
red on a train. What it holds is the shape: SHA, plus the version it came from.
Whether the SHA is the RIGHT one is Dependabot's job and the reviewer's.

Exit codes: 0 clean · 1 an unpinned or uncommented `uses:` was found.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOWS = os.path.join(REPO, ".github", "workflows")

# `uses: owner/repo@<40 hex>  # v1.2.3`, with an optional sub-path before the @
# (some actions live in a subdirectory of their repo).
PINNED = re.compile(
    r"""^(?P<indent>\s*(?:-\s*)?)uses:\s*
        (?P<action>[\w.\-]+/[\w.\-]+(?:/[\w.\-/]+)?)
        @(?P<sha>[0-9a-f]{40})
        \s*\#\s*(?P<version>\S+)\s*$""",
    re.VERBOSE,
)
USES = re.compile(r"^\s*(?:-\s*)?uses:\s*(?P<value>.+?)\s*$")

# A workflow may call a local composite action or another workflow in this repo
# by path; there is no third party to pin.
LOCAL_PREFIXES = ("./", ".github/")


def main() -> int:
    if not os.path.isdir(WORKFLOWS):
        print(f"no workflows directory at {WORKFLOWS}")
        return 0

    bad = []
    pins = {}
    checked = 0
    for name in sorted(os.listdir(WORKFLOWS)):
        if not name.endswith((".yml", ".yaml")):
            continue
        path = os.path.join(WORKFLOWS, name)
        with open(path, encoding="utf-8") as fh:
            for number, line in enumerate(fh, start=1):
                if line.lstrip().startswith("#"):
                    continue
                m = USES.match(line.rstrip("\n"))
                if not m:
                    continue
                value = m.group("value")
                if value.startswith(LOCAL_PREFIXES):
                    continue
                checked += 1
                pinned = PINNED.match(line.rstrip("\n"))
                if not pinned:
                    bad.append((name, number, value))
                    continue
                pins.setdefault(
                    (pinned.group("action"), pinned.group("sha")),
                    pinned.group("version"),
                )

    # The same action pinned to one SHA under two different version comments
    # means a half-finished bump: the SHA is one version, the comment another.
    disagree = {}
    for (action, sha), version in pins.items():
        disagree.setdefault(sha, set()).add((action, version))
    for sha, entries in disagree.items():
        if len({v for _, v in entries}) > 1:
            bad.append(("(cross-file)", 0,
                        f"{sha} is commented as {sorted(v for _, v in entries)}"))

    if bad:
        print(f"{len(bad)} GitHub Action reference(s) are not pinned to a commit:\n")
        current = None
        for name, number, value in bad:
            if name != current:
                print(f"  .github/workflows/{name}")
                current = name
            where = f"{number}: " if number else ""
            print(f"      {where}{value}")
        print("\nEvery `uses:` must name a 40-hex commit SHA with the version it came\n"
              "from in a trailing comment:\n"
              "\n      uses: actions/checkout@fbc6f39...c09 # v5.1.0\n"
              "\nA tag is a mutable pointer in someone else's repository, and these\n"
              "workflows hold this repo's secrets. Resolve a tag with:\n"
              "\n      gh api repos/<owner>/<repo>/commits/<tag> --jq .sha\n"
              "\n.github/dependabot.yml keeps the pins current; it rewrites the SHA and\n"
              "the comment together.")
        return 1

    print(f"OK: {checked} action reference(s) pinned to a commit SHA "
          f"({len(pins)} distinct pins).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
