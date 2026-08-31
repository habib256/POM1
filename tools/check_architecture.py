#!/usr/bin/env python3
"""Guard POM1's dependency direction and publish architecture trend metrics."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
BASELINE = ROOT / "tools" / "architecture_baseline.json"
SOURCE_SUFFIXES = {".cpp", ".h", ".mm"}
CORE_NAMES = {
    "M6502.cpp", "M6502.h", "PeripheralBus.cpp", "PeripheralBus.h",
    "SnapshotIO.cpp", "SnapshotIO.h", "RewindBuffer.cpp", "RewindBuffer.h",
}
UI_INCLUDE = re.compile(
    r"^(?:imgui(?:_internal)?\.h|MainWindow[^/]*|Screen_ImGui\.h|"
    r"MemoryViewer_ImGui\.h|CassetteDeck_ImGui\.h)$"
)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

# The development environment: editors, DevBench and the BASIC compilers — the
# second product sharing this process. TODO.md chantier 1 puts all of it behind
# -DPOM1_DEVTOOLS=OFF; until then this set is the boundary, and the baseline's
# allowed_devtools_dependencies is the ratchet that stops it from spreading.
# One directory or file name per entry, matched against paths relative to src/.
DEVTOOLS_DIRS = {
    "bench", "hgrpaint", "hgrsprite", "tmspaint", "tmssprite", "sfxbeep",
    "sidtrack",
}
DEVTOOLS_STEMS = {
    "Pom1BenchHost", "Pom1BenchHost_Lang", "Pom1BenchCc65", "Pom1BenchTargets",
    "Pom1HgrPaintHost", "Pom1TmsPaintHost", "Pom1SfxHost", "Pom1SidHost",
    "BenchDebugSession", "DbgFile",
    "BasicCompilerApplesoft", "BasicTokeniserApplesoft", "BasicTokeniserInteger",
}
# Classes whose public surface is frozen (TODO.md chantier 2): the ceiling may
# only ever go down. Counted from the header, not from a line total, because
# "how many things can a caller ask of this object?" is the property that
# matters — a facade shrinks by losing methods, not by losing comments.
FROZEN_FACADES = (
    ("memory_public_methods", "Memory.h", "Memory"),
    ("controller_public_methods", "EmulationController.h", "EmulationController"),
)


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def source_files() -> list[Path]:
    return sorted(
        p for p in SRC.rglob("*")
        if p.suffix in SOURCE_SUFFIXES and "third_party" not in p.parts
    )


def is_devtools(path: Path) -> bool:
    relative = path.relative_to(SRC)
    return relative.parts[0] in DEVTOOLS_DIRS or path.stem in DEVTOOLS_STEMS


def test_device_names() -> set[str]:
    cmake = text(ROOT / "tests" / "CMakeLists.txt")
    match = re.search(r"set\(POM1_TEST_CORE_FILES\s+(.*?)\n\)", cmake, re.DOTALL)
    if not match:
        raise RuntimeError("POM1_TEST_CORE_FILES not found in tests/CMakeLists.txt")
    return set(re.findall(r"/src/([^\s)]+\.(?:cpp|mm))", match.group(1)))


def reverse_dependencies(files: list[Path], device_names: set[str]) -> list[str]:
    edges: list[str] = []
    for path in files:
        relative = path.relative_to(ROOT).as_posix()
        in_guarded_layer = path.name in CORE_NAMES or path.name in device_names
        if not in_guarded_layer:
            continue
        for included in INCLUDE.findall(text(path)):
            if UI_INCLUDE.match(Path(included).name):
                edges.append(f"{relative} -> {included}")
    return sorted(set(edges))


def devtools_dependencies(files: list[Path]) -> list[str]:
    """Every include of a devtools header from a file outside the devtools set."""
    devtools_headers = {
        p.name for p in files if p.suffix == ".h" and is_devtools(p)
    }
    edges: list[str] = []
    for path in files:
        if is_devtools(path):
            continue
        for included in INCLUDE.findall(text(path)):
            if Path(included).name in devtools_headers:
                edges.append(f"{path.relative_to(SRC).as_posix()} -> {included}")
    return sorted(set(edges))


def strip_code(source: str) -> str:
    """Drop comments, string/char literals and preprocessor lines."""
    source = re.sub(r"^[ \t]*#.*(?:\\\n.*)*$", "", source, flags=re.MULTILINE)
    out: list[str] = []
    i, n = 0, len(source)
    while i < n:
        c = source[i]
        if c == "/" and i + 1 < n and source[i + 1] == "/":
            j = source.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and i + 1 < n and source[i + 1] == "*":
            j = source.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
            continue
        if c in "\"'":
            j = i + 1
            while j < n:
                if source[j] == "\\":
                    j += 2
                    continue
                if source[j] == c:
                    j += 1
                    break
                j += 1
            i = j
            out.append('""')
            continue
        out.append(c)
        i += 1
    return "".join(out)


_STATEMENT_KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "catch"}
_NOT_A_METHOD = ("struct", "class", "enum", "union", "friend", "using",
                 "typedef", "static_assert")


def public_method_count(header: Path, class_name: str) -> int:
    """Declarations callable from outside `class_name`, counted at class scope.

    Deliberately simple: it walks the class body at brace depth 1, tracks the
    access specifier, and counts statements naming an identifier followed by
    '('. Nested types raise the depth, so their members (the CardSlot function
    pointers) never count; data members carry no parenthesis.
    """
    source = strip_code(text(header))
    opening = re.search(rf"\bclass\s+{class_name}\b[^;{{]*\{{", source)
    if not opening:
        raise RuntimeError(f"class {class_name} not found in {header.name}")

    count = 0
    access = "private"
    buffer: list[str] = []
    depth = 1
    i = opening.end()

    def take(statement: str) -> int:
        stripped = statement.strip()
        if not stripped or access != "public":
            return 0
        if stripped.startswith(_NOT_A_METHOD):
            return 0
        named = re.search(r"([A-Za-z_~][A-Za-z_0-9]*)\s*\(", stripped)
        return 1 if named and named.group(1) not in _STATEMENT_KEYWORDS else 0

    while i < len(source):
        if depth == 1 and not buffer:
            specifier = re.match(r"\s*(public|private|protected)\s*:", source[i:])
            if specifier:
                access = specifier.group(1)
                i += specifier.end()
                continue
        c = source[i]
        if c == "{":
            if depth == 1:
                # Inline definition or nested type: the statement ends here and
                # the body is skipped whole.
                count += take("".join(buffer))
                buffer = []
                nested, j = 1, i + 1
                while j < len(source) and nested:
                    nested += (source[j] == "{") - (source[j] == "}")
                    j += 1
                i = j
                while i < len(source) and source[i] in " \t\r\n":
                    i += 1
                if i < len(source) and source[i] == ";":
                    i += 1
                continue
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        elif c == ";" and depth == 1:
            count += take("".join(buffer))
            buffer = []
            i += 1
            continue
        buffer.append(c)
        i += 1
    return count


def line_count(paths: list[Path]) -> int:
    return sum(text(path).count("\n") + (not text(path).endswith("\n")) for path in paths)


def metrics(files: list[Path], device_names: set[str]) -> tuple[dict[str, int], dict[str, int]]:
    groups = {
        "memory_lines": [SRC / name for name in ("Memory.cpp", "Memory.h", "MemorySnapshot.cpp")],
        "controller_lines": sorted(SRC.glob("EmulationController*.cpp")) + [SRC / "EmulationController.h"],
        "mainwindow_lines": sorted(SRC.glob("MainWindow*.cpp")) + sorted(SRC.glob("MainWindow*.h")),
    }
    values = {name: line_count(paths) for name, paths in groups.items()}
    compiled_sources = [p for p in files if p.suffix in {".cpp", ".mm"}]
    values["sources_outside_test_devices"] = sum(
        p.relative_to(SRC).as_posix() not in device_names for p in compiled_sources
    )
    for metric, header, class_name in FROZEN_FACADES:
        values[metric] = public_method_count(SRC / header, class_name)

    headers = ("Memory.h", "EmulationController.h", "MainWindow_ImGui.h", "GraphicsCard.h")
    all_code = files + sorted((ROOT / "tests").glob("*.cpp"))
    fanout = {
        header: sum(
            f'#include "{header}"' in text(path) for path in all_code
        )
        for header in headers
    }
    return values, fanout


def compare(label: str, values: dict[str, int], ceilings: dict[str, int]) -> list[str]:
    failures: list[str] = []
    for name, value in sorted(values.items()):
        ceiling = ceilings[name]
        status = "OK" if value <= ceiling else "GROWTH"
        print(f"{label}: {name} = {value} (baseline ceiling {ceiling}) [{status}]")
        if value > ceiling:
            failures.append(f"{name}: {value} > {ceiling}")
    return failures


def compare_edges(label: str, actual: list[str], allowed: list[str]) -> list[str]:
    """Grandfathered edges: a new one fails, and so does one left in the
    baseline after its last call site is gone (the ratchet only turns down)."""
    new = sorted(set(actual) - set(allowed))
    stale = sorted(set(allowed) - set(actual))
    print(f"{label}: {len(actual)} grandfathered edge(s)")
    if stale:
        print(f"STALE {label} baseline edges (remove them):", *stale, sep="\n  ")
    return ([f"new {label}: {edge}" for edge in new]
            + [f"stale allowed {label}: {edge}" for edge in stale])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    args = parser.parse_args()
    baseline = json.loads(text(args.baseline))
    files = source_files()
    devices = test_device_names()

    failures = compare_edges(
        "dependencies",
        reverse_dependencies(files, devices),
        sorted(baseline["allowed_reverse_dependencies"]),
    )
    failures += compare_edges(
        "devtools boundary",
        devtools_dependencies(files),
        sorted(baseline["allowed_devtools_dependencies"]),
    )

    values, fanout = metrics(files, devices)
    failures += compare("metric", values, baseline["metric_ceilings"])
    failures += compare("fanout", fanout, baseline["header_fanout_ceilings"])

    if failures:
        print("architecture_check: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("architecture_check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
