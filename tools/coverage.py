#!/usr/bin/env python3
"""Per-module line and branch coverage for POM1, with thresholds that can gate.

WHY NOT A SINGLE PERCENTAGE
    POM1 is ~85 600 lines of src/, and the parts are not comparable. The 6502
    core is pinned by two cycle-exact oracles and a functional test suite; the
    parsers are fuzzed; `MainWindow_*` is 17 000 lines of ImGui drawing that no
    test binary even links. Averaging those into one number produces a figure
    that moves for the wrong reasons and can be improved by writing tests for
    whatever is easiest. A per-module table says where the risk actually is,
    and a threshold per module is a promise you can keep.

WHAT A MODULE IS
    MODULES below, in order — first pattern that matches a file owns it, so the
    specific rules come before the general ones. The split follows the one
    CLAUDE.md already uses to talk about the codebase (core, parsers,
    topology/config, snapshot, devices, platform, UI, devtools), not the
    directory layout, because that is the split the thresholds are about.

USAGE
    tools/coverage.py                     # configure, build, run, report
    tools/coverage.py --no-build          # reuse the coverage build dir
    tools/coverage.py --min parsers:90 --min cpu:85
    tools/coverage.py --json out.json     # machine-readable, for CI trends

    The default build directory is build-coverage/ beside the repo. It is a
    SEPARATE tree on purpose: the instrumentation forces -O0 and LTO off, so
    sharing build/ would leave every developer's normal build de-optimised.

REQUIREMENTS
    Clang, plus llvm-profdata and llvm-cov from the SAME toolchain that
    compiled the tree — a version mismatch reports "unsupported coverage
    format version" and nothing else. On macOS they are found through xcrun.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# First match wins. The patterns are matched against the repo-relative path.
MODULES = [
    ("cpu", (
        r"^src/M6502\.cpp$",
        r"^src/Disassembler\.cpp$",
    )),
    ("parsers", (
        r"^src/MemoryImageLoader\.cpp$",
        r"^src/IntelHexFile\.",
        r"^src/PcmFile\.cpp$",
        r"^src/D64Image\.cpp$",
        r"^src/FileBytes\.",
        r"^src/HexDumpFile\.",
        r"^src/BasicTokeniser",
        r"^src/DbgFile\.cpp$",
    )),
    ("snapshot", (
        r"^src/MemorySnapshot\.cpp$",
        r"^src/SnapshotIO\.",
        r"^src/SnapshotPublisher\.cpp$",
        r"^src/RewindBuffer\.cpp$",
    )),
    ("topology", (
        r"^src/CardTopology\.cpp$",
        r"^src/CardTypes\.",
        r"^src/BusConflicts\.",
        r"^src/MachineConfig\.",
        r"^src/MachinePresets\.cpp$",
        r"^src/MachineCoordinator\.cpp$",
        r"^src/StagedCardConfiguration\.",
    )),
    ("memory", (
        r"^src/Memory\.cpp$",
        r"^src/PeripheralBus\.cpp$",
        r"^src/Peripheral\.",
    )),
    ("control", (
        r"^src/EmulationController",
        r"^src/KeyboardController\.cpp$",
        r"^src/LockOrder\.",
    )),
    ("devtools", (
        r"^src/bench/",
        r"^src/hgrpaint/",
        r"^src/hgrsprite/",
        r"^src/tmspaint/",
        r"^src/tmssprite/",
        r"^src/sfxbeep/",
        r"^src/sidtrack/",
        r"^src/Pom1.*Host",
        r"^src/Pom1Bench",
        r"^src/BasicCompiler",
        r"^src/BenchDebugSession\.",
        r"^tools/basicc\.cpp$",
    )),
    ("ui", (
        r"^src/MainWindow_",
        r"^src/main_imgui\.cpp$",
        r"^src/Screen_ImGui\.cpp$",
        r"^src/CassetteDeck_ImGui\.cpp$",
        r"^src/Pom1CrtEffects\.",
        r"^src/CrtEffectStack",
        r"^src/PomRenderer",
        r"^src/Apple1KeyMap\.",
        r"^src/WindowGeometry\.",
        r"^src/FullscreenExpand\.",
    )),
    ("platform", (
        r"^src/ResourceLocator\.cpp$",
        r"^src/NativeFileDialog",
        r"^src/Logger\.",
        r"^src/CliDispatcher\.cpp$",
        r"^src/MacNative",
        r"^src/X11ErrorGuard\.cpp$",
        r"^src/AudioDevice\.cpp$",
        r"^src/AudioService\.",
    )),
    # Everything else under src/ is an emulated card.
    ("devices", (r"^src/",)),
]

# Line-coverage floors for the modules POM1 actually promises something about.
# They are RATCHETS, set just under what the suite measures today, not targets:
# the point is to notice a module losing coverage, which is what happens when a
# branch is added and no case comes with it. Raise one when a refactor earns it;
# never lower one to make CI green.
#
# Four modules are gated, and they are the four TODO.md names: the parsers (the
# surface that reads files POM1 did not write), the topology policy, the
# snapshot format, and the CPU. `memory`, `devices`, `platform`, `control`,
# `ui` and `devtools` are REPORTED and not gated — a floor under 2.4 % UI
# coverage would be a number pretending to be a promise.
THRESHOLDS = {
    "cpu":      90.0,   # measured 93.3 — Klaus + Harte + the interrupt suite
    "parsers":  90.0,   # measured 93.6 — four fuzzers land here
    "topology": 85.0,   # measured 89.9
    "snapshot": 85.0,   # measured 88.9
}

# Not POM1's code, or not C++ POM1 tests exercise: vendored Dear ImGui (which
# sits at the repo ROOT, so the pattern must anchor as well as match a middle
# segment — `/imgui/` alone missed all 16 000 lines of imgui.cpp and dumped
# them into "other"), the CMake-generated tree, the test sources themselves,
# and the 6502 C runtime under dev/ that cc65 compiles for the Apple-1.
IGNORE = re.compile(r"(third_party|(^|/)imgui/|^tests/|_deps/|^build|^dev/)")


def clang_major() -> str:
    """Major version of the clang that will compile (or compiled) the tree."""
    compiler = os.environ.get("CXX", "clang++")
    try:
        out = subprocess.run([compiler, "--version"],
                             capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""
    m = re.search(r"version (\d+)\.", out)
    return m.group(1) if m else ""


def llvm_tool(name: str) -> str:
    """Resolve an llvm-* binary from the toolchain that compiled the tree.

    Version matching is not a nicety: llvm-cov reads a profile format that is
    versioned with the compiler, and a mismatch reports "unsupported coverage
    format version" and nothing usable. So the versioned name for the clang in
    play is tried FIRST, and Debian/Ubuntu's /usr/lib/llvm-N/bin layout — where
    the unversioned name often simply does not exist — is searched by hand.
    """
    if sys.platform == "darwin":
        try:
            found = subprocess.run(["xcrun", "--find", name],
                                   capture_output=True, text=True, check=True)
            return found.stdout.strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
    major = clang_major()
    candidates = []
    if major:
        candidates += [f"{name}-{major}", f"/usr/lib/llvm-{major}/bin/{name}"]
    candidates.append(name)
    for candidate in candidates:
        if os.path.isabs(candidate):
            if os.access(candidate, os.X_OK):
                return candidate
            continue
        path = shutil.which(candidate)
        if path:
            return path
    sys.exit(f"{name} not found (clang major = {major or 'unknown'}). It must come "
             f"from the same LLVM toolchain that compiled the tree, or llvm-cov "
             f"reports an unsupported coverage format version.\n"
             f"Debian/Ubuntu: apt install llvm-{major or 'N'}")


def module_of(path: str) -> str:
    for name, patterns in MODULES:
        for pattern in patterns:
            if re.search(pattern, path):
                return name
    return "other"


def run(cmd, **kw):
    result = subprocess.run(cmd, **kw)
    if result.returncode != 0 and kw.get("check", True):
        sys.exit(f"command failed ({result.returncode}): {' '.join(cmd[:4])} …")
    return result


def configure_and_build(build_dir: str, jobs: int) -> None:
    os.makedirs(build_dir, exist_ok=True)
    run(["cmake", REPO, "-DPOM1_COVERAGE=ON", "-DCMAKE_BUILD_TYPE=Debug"],
        cwd=build_dir, stdout=subprocess.DEVNULL)
    run(["cmake", "--build", ".", "-j", str(jobs)],
        cwd=build_dir, stdout=subprocess.DEVNULL)


def run_tests(build_dir: str, raw_dir: str) -> int:
    if os.path.isdir(raw_dir):
        shutil.rmtree(raw_dir)
    os.makedirs(raw_dir)
    env = dict(os.environ)
    # One profile per PROCESS (%p): ctest runs each test as its own binary, and
    # a shared file would have them overwrite each other's counters.
    env["LLVM_PROFILE_FILE"] = os.path.join(raw_dir, "pom1-%p.profraw")
    result = subprocess.run(["ctest", "--output-on-failure"], cwd=build_dir,
                            env=env, stdout=subprocess.DEVNULL)
    return result.returncode


def merge(profdata: str, raw_dir: str, out: str) -> None:
    raws = [os.path.join(raw_dir, f) for f in os.listdir(raw_dir)
            if f.endswith(".profraw")]
    if not raws:
        sys.exit(f"no .profraw files in {raw_dir} — did the tests run instrumented?")
    print(f"merging {len(raws)} profile(s)…")
    run([profdata, "merge", "-sparse", "-o", out] + raws)


def objects(build_dir: str) -> list:
    """Every instrumented binary a test can have executed."""
    found = []
    for root, _dirs, files in os.walk(build_dir):
        for name in files:
            path = os.path.join(root, name)
            if not os.access(path, os.X_OK) or os.path.isdir(path):
                continue
            if name.endswith((".o", ".a", ".cmake", ".txt", ".profraw", ".py",
                              ".dylib", ".so")):
                continue
            if "CMakeFiles" in root and "CompilerId" in root:
                continue
            with open(path, "rb") as fh:
                magic = fh.read(4)
            # Mach-O (64-bit + universal) or ELF.
            if magic in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\x7fELF"):
                found.append(path)
    return found


def export(cov: str, profdata: str, objs: list) -> dict:
    args = [cov, "export", "-instr-profile", profdata, "-summary-only"]
    args.append(objs[0])
    for extra in objs[1:]:
        args += ["-object", extra]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("llvm-cov export failed:\n" + result.stderr[:2000])
    return json.loads(result.stdout)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default=os.path.join(REPO, "build-coverage"))
    ap.add_argument("--no-build", action="store_true",
                    help="reuse the existing coverage build tree")
    ap.add_argument("--no-run", action="store_true",
                    help="reuse the profiles already merged")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--min", action="append", default=[], metavar="MODULE:PCT",
                    help="fail when MODULE's line coverage is under PCT")
    ap.add_argument("--gate", action="store_true",
                    help="apply the checked-in THRESHOLDS (what CI runs)")
    ap.add_argument("--json", metavar="PATH", help="write the table as JSON")
    args = ap.parse_args()

    profdata_tool = llvm_tool("llvm-profdata")
    cov_tool = llvm_tool("llvm-cov")

    build_dir = os.path.abspath(args.build_dir)
    raw_dir = os.path.join(build_dir, "coverage-raw")
    merged = os.path.join(build_dir, "pom1.profdata")

    if not args.no_build:
        print(f"configuring + building instrumented tree in {build_dir}…")
        configure_and_build(build_dir, args.jobs)
    if not args.no_run:
        print("running ctest under instrumentation…")
        rc = run_tests(build_dir, raw_dir)
        if rc != 0:
            print(f"NOTE: ctest exited {rc}; coverage is still reported, but a "
                  f"failing suite covers less than a green one.")
        merge(profdata_tool, raw_dir, merged)

    objs = objects(build_dir)
    if not objs:
        sys.exit(f"no instrumented binaries found under {build_dir}")
    print(f"exporting coverage over {len(objs)} binaries…")
    data = export(cov_tool, merged, objs)

    totals = {}
    for entry in data.get("data", [{}])[0].get("files", []):
        # Forward slashes always — MODULES and IGNORE are written with them.
        path = os.path.relpath(entry["filename"], REPO).replace(os.sep, "/")
        if IGNORE.search(path) or path.startswith(".."):
            continue
        bucket = totals.setdefault(module_of(path),
                                   {"lines": [0, 0], "branches": [0, 0],
                                    "functions": [0, 0], "files": 0})
        bucket["files"] += 1
        summary = entry["summary"]
        for key in ("lines", "branches", "functions"):
            bucket[key][0] += summary[key]["covered"]
            bucket[key][1] += summary[key]["count"]

    def pct(covered, count):
        return 100.0 * covered / count if count else 0.0

    order = [name for name, _ in MODULES] + ["other"]
    rows = [(name, totals[name]) for name in order if name in totals]

    print()
    print(f"{'module':<10} {'files':>5} {'lines':>16} {'line %':>7} {'branch %':>9}")
    print("-" * 52)
    grand = {"lines": [0, 0], "branches": [0, 0]}
    report = {}
    for name, bucket in rows:
        lp = pct(*bucket["lines"])
        bp = pct(*bucket["branches"])
        print(f"{name:<10} {bucket['files']:>5} "
              f"{bucket['lines'][0]:>7}/{bucket['lines'][1]:<8} {lp:>6.1f}% {bp:>8.1f}%")
        for key in grand:
            grand[key][0] += bucket[key][0]
            grand[key][1] += bucket[key][1]
        report[name] = {"files": bucket["files"],
                        "lines_covered": bucket["lines"][0],
                        "lines_total": bucket["lines"][1],
                        "line_pct": round(lp, 2),
                        "branch_pct": round(bp, 2)}
    print("-" * 52)
    print(f"{'ALL':<10} {sum(b['files'] for _, b in rows):>5} "
          f"{grand['lines'][0]:>7}/{grand['lines'][1]:<8} "
          f"{pct(*grand['lines']):>6.1f}% {pct(*grand['branches']):>8.1f}%")
    print("\nThe ALL row is printed for completeness and is the least useful number\n"
          "here: it averages a cycle-exact CPU against ImGui draw code no test\n"
          "binary links. Read the rows.")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2, sort_keys=True)
        print(f"\nwrote {args.json}")

    failures = []
    checks = list(args.min)
    if args.gate:
        checks += [f"{name}:{floor}" for name, floor in sorted(THRESHOLDS.items())]
    for spec in checks:
        if ":" not in spec:
            sys.exit(f"--min wants MODULE:PCT, got {spec!r}")
        name, want = spec.rsplit(":", 1)
        if name not in report:
            failures.append(f"{name}: no such module (have: {', '.join(sorted(report))})")
            continue
        got = report[name]["line_pct"]
        if got < float(want):
            failures.append(f"{name}: {got:.1f}% line coverage, threshold {want}%")
    if failures:
        print("\ncoverage thresholds not met:")
        for line in failures:
            print(f"  {line}")
        print("\nThese floors are ratchets set just under what the suite already\n"
              "measures (THRESHOLDS in this script). A module that drops below one\n"
              "gained a branch without a case for it — write the case. Do not lower\n"
              "the floor to make this green.")
        return 1
    if checks:
        print(f"\nOK: {len(checks)} coverage threshold(s) met.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
