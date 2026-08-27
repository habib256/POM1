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


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def source_files() -> list[Path]:
    return sorted(
        p for p in SRC.rglob("*")
        if p.suffix in SOURCE_SUFFIXES and "third_party" not in p.parts
    )


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    args = parser.parse_args()
    baseline = json.loads(text(args.baseline))
    files = source_files()
    devices = test_device_names()

    actual_edges = reverse_dependencies(files, devices)
    allowed_edges = sorted(baseline["allowed_reverse_dependencies"])
    new_edges = sorted(set(actual_edges) - set(allowed_edges))
    stale_edges = sorted(set(allowed_edges) - set(actual_edges))
    print(f"dependencies: {len(actual_edges)} grandfathered reverse edge(s)")
    if stale_edges:
        print("STALE baseline edges (remove them):", *stale_edges, sep="\n  ")

    values, fanout = metrics(files, devices)
    failures = compare("metric", values, baseline["metric_ceilings"])
    failures += compare("fanout", fanout, baseline["header_fanout_ceilings"])
    if new_edges:
        failures += [f"new reverse dependency: {edge}" for edge in new_edges]
    if stale_edges:
        failures += [f"stale allowed dependency: {edge}" for edge in stale_edges]

    if failures:
        print("architecture_check: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("architecture_check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
