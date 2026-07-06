#!/usr/bin/env python3
"""test_lib_micro.py — unit-level 6502 micro-tests for POM1's dev/lib routines.

Runs every driver in dev/lib/test/micro/ (*.s via ca65+ld65, *.c via cl65)
headless inside POM1 and asserts REAL VALUES read back from a RAM "result
mailbox" — no golden hashes.

Mechanism
---------
POM1's headless CLI executes its deferred verbs in argv order, so

    POM1 --headless --preset N --silicon-strict
         --load ADDR:test.bin --run ADDR --step NSTEPS
         --snapshot-save out.snap
         --dump-tms-frame scratch.png --dump-after-cycles 1000

loads the driver, cold-starts it, single-steps NSTEPS instructions
synchronously, THEN saves a full machine snapshot (the driver ends in a spin
loop, so overshoot is harmless). The trailing frame dump is only an exit
ticket — it makes the headless process terminate on its own with a clean
exit code instead of waiting for SIGINT. The harness then parses the
snapshot's MEM section (documented in src/SnapshotIO.h: "POM1SNAP" header +
8-byte-named sections; MEM payload starts with the full 64 KB RAM) and
compares the driver's mailbox bytes against the EXPECT lines in its header.

Every run also asserts ZERO `[TMS9918 DROP` lines on stderr, so each
micro-test doubles as a silicon-strict pacing test for the routine under
test (both data-port writes AND reads are slot-gated in strict mode; the
non-Fantasy presets arm strict mode themselves in applyHeadlessConfig —
the explicit --silicon-strict below is declarative, headless ignores it).
Caveat: POM1's strict gating uses the openMSX slot tables (worst active
gap ~8c, laxer than the real chip's ~16c floor), and a short burst can
ride a free zone (VBlank / blanked) without drops — the assertion reliably
catches unpadded back-to-back access, not marginal 9-16c pacing.

Driver header contract (comment lines, ';' for .s / '*' or '//' for .c):
    POM1-LIB-MICRO-TEST          required marker
    LIBS:  path ...              lib sources, relative to dev/lib
                                 (.s tests: assembled+linked; .c tests:
                                 handed to cl65 with the driver)
    CFG:   micro.cfg             linker cfg (relative to dev/lib/test/micro
                                 for .s, to dev/lib for .c)
    PRESET: 1                    --preset index (default 1)
    MODE:  codetank              .c build mode:
                                   codetank — 32 KB CodeTank ROM (lower 16 KB =
                                              program), boot 4000R (TMS9918c)
                                   gen2     — raw binary via apple1_gen2_c.cfg,
                                              loaded/run at LOAD/RUN (GEN2 HGR C)
    LOAD/RUN: 0300               load/entry address (default 0300; gen2 .c: 6000)
    STEPS: 120000                --step budget (instructions; driver spins
                                 at the end, so bigger is merely slower)
    EXPECT: ADDR B0 B1 ...       hex bytes expected at ADDR (repeatable)

Exit codes: 0 all green, 1 failures, 77 skip (cc65 or build/POM1 missing).
"""

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MICRO_DIR = REPO / "dev" / "lib" / "test" / "micro"
LIB_DIR = REPO / "dev" / "lib"
POM1_BIN = REPO / "build" / "POM1"

# ca65 include path for the .s drivers (textual .include + macro packs).
ASM_INCLUDE_DIRS = [
    LIB_DIR / "apple1",
    LIB_DIR / "tms9918",
    LIB_DIR / "m6502",
    LIB_DIR / "basicrt",
    LIB_DIR / "beep",
    LIB_DIR / "sid",
    LIB_DIR / "games" / "rogue",
]

RUN_TIMEOUT_S = 60


class TestSpec:
    def __init__(self, path: Path):
        self.path = path
        self.libs: list[str] = []
        self.cfg = "micro.cfg"
        self.preset = 1
        self.mode = "ram"           # "ram" (.s at LOAD/RUN) | "codetank" (.c)
        self.load = 0x0300
        self.run = 0x0300
        self.steps = 200000
        self.expects: list[tuple[int, bytes]] = []
        self.marker = False
        self._parse()

    def _parse(self):
        head = self.path.read_text(encoding="utf-8").splitlines()[:80]
        for raw in head:
            line = re.sub(r"^[;/*\s]+", "", raw).strip()
            if line.startswith("POM1-LIB-MICRO-TEST"):
                self.marker = True
            m = re.match(r"(LIBS|CFG|PRESET|MODE|LOAD|RUN|STEPS|EXPECT):\s*(.*)", line)
            if not m:
                continue
            key, val = m.group(1), m.group(2).strip()
            if key == "LIBS":
                self.libs = val.split()
            elif key == "CFG":
                self.cfg = val
            elif key == "PRESET":
                self.preset = int(val)
            elif key == "MODE":
                self.mode = val.lower()
            elif key == "LOAD":
                self.load = int(val, 16)
            elif key == "RUN":
                self.run = int(val, 16)
            elif key == "STEPS":
                self.steps = int(val)
            elif key == "EXPECT":
                parts = val.split()
                addr = int(parts[0], 16)
                data = bytes(int(b, 16) for b in parts[1:])
                if not data:
                    raise ValueError(f"{self.path.name}: empty EXPECT line")
                self.expects.append((addr, data))
        if not self.marker:
            raise ValueError(f"{self.path.name}: missing POM1-LIB-MICRO-TEST marker")
        if not self.expects:
            raise ValueError(f"{self.path.name}: no EXPECT lines")


def run_cmd(cmd, cwd=None):
    return subprocess.run(
        [str(c) for c in cmd], cwd=cwd, capture_output=True, text=True,
        timeout=RUN_TIMEOUT_S,
    )


def build_asm(spec: TestSpec, workdir: Path) -> Path:
    objs = []
    inc = []
    for d in ASM_INCLUDE_DIRS:
        inc += ["-I", str(d)]
    for i, lib in enumerate(spec.libs):
        src = LIB_DIR / lib
        if not src.exists():
            raise RuntimeError(f"LIBS entry not found: {src}")
        obj = workdir / f"lib{i}.o"
        r = run_cmd(["ca65", *inc, src, "-o", obj])
        if r.returncode != 0:
            raise RuntimeError(f"ca65 {lib} failed:\n{r.stderr}{r.stdout}")
        objs.append(obj)
    drv_obj = workdir / "driver.o"
    r = run_cmd(["ca65", *inc, spec.path, "-o", drv_obj])
    if r.returncode != 0:
        raise RuntimeError(f"ca65 {spec.path.name} failed:\n{r.stderr}{r.stdout}")
    out = workdir / "test.bin"
    cfg = MICRO_DIR / spec.cfg
    r = run_cmd(["ld65", "-C", cfg, drv_obj, *objs, "-o", out])
    if r.returncode != 0:
        raise RuntimeError(f"ld65 {spec.path.name} failed:\n{r.stderr}{r.stdout}")
    return out


def build_c_codetank(spec: TestSpec, workdir: Path) -> Path:
    srcs = [spec.path] + [LIB_DIR / lib for lib in spec.libs]
    for s in srcs:
        if not s.exists():
            raise RuntimeError(f"LIBS entry not found: {s}")
    cfg = LIB_DIR / spec.cfg
    # cl65 writes intermediate .o files next to the SOURCES: stage copies in
    # the workdir and compile there so nothing lands beside the lib files.
    staged = []
    for s in srcs:
        dst = workdir / s.name
        dst.write_bytes(s.read_bytes())
        staged.append(dst.name)
    binf = workdir / "test.bin"
    r = run_cmd(["cl65", "-t", "none", "-Oirs", "-C", cfg,
                 "-I", LIB_DIR / "tms9918c", *staged, "-o", binf],
                cwd=workdir)
    if r.returncode != 0:
        raise RuntimeError(f"cl65 {spec.path.name} failed:\n{r.stderr}{r.stdout}")
    image = binf.read_bytes()
    if len(image) != 0x4000:
        raise RuntimeError(
            f"{spec.path.name}: expected a 16 KB ROM image, got {len(image)} B "
            "(codetank_c.cfg has fill = yes)")
    rom = workdir / "test.rom"           # 28c256 image: program + empty upper
    rom.write_bytes(image + b"\xff" * 0x4000)
    return rom


def build_c_gen2(spec: TestSpec, workdir: Path) -> Path:
    """GEN2 HGR C driver: cl65 with apple1_gen2_c.cfg -> raw binary loaded at the
    cfg's STARTADDRESS ($6000), run with --load/--run (no CodeTank ROM wrap)."""
    srcs = [spec.path] + [LIB_DIR / lib for lib in spec.libs]
    for s in srcs:
        if not s.exists():
            raise RuntimeError(f"LIBS entry not found: {s}")
    cfg = LIB_DIR / spec.cfg              # e.g. ../cc65/apple1_gen2_c.cfg
    staged = []                           # cl65 drops .o beside sources -> stage
    for s in srcs:
        dst = workdir / s.name
        dst.write_bytes(s.read_bytes())
        staged.append(dst.name)
    binf = workdir / "test.bin"
    r = run_cmd(["cl65", "-t", "none", "-Oirs", "-C", cfg,
                 "-I", LIB_DIR / "gen2c", "-I", LIB_DIR / "apple1c",
                 "-I", LIB_DIR / "gfx", *staged, "-o", binf],
                cwd=workdir)
    if r.returncode != 0:
        raise RuntimeError(f"cl65 {spec.path.name} failed:\n{r.stderr}{r.stdout}")
    return binf


def run_pom1(spec: TestSpec, artefact: Path, workdir: Path):
    snap = workdir / "out.snap"
    png = workdir / "scratch.png"        # exit ticket only, never inspected
    cmd = [POM1_BIN, "--headless", "--preset", spec.preset, "--silicon-strict"]
    if spec.mode == "codetank":
        cmd += ["--codetank-rom", artefact, "--codetank-jumper", "lower",
                "--run", "4000"]
    else:
        cmd += ["--load", f"{spec.load:04X}:{artefact}",
                "--run", f"{spec.run:04X}"]
    # The frame dump is only an "exit ticket" that makes headless stop after
    # --step and save the snapshot; pick the one for this card (a --dump-tms-frame
    # on a GEN2 preset would have no TMS to render).
    dump_flag = "--dump-gen2-frame" if spec.mode == "gen2" else "--dump-tms-frame"
    cmd += ["--step", spec.steps, "--snapshot-save", snap,
            dump_flag, png, "--dump-after-cycles", "1000"]
    r = run_cmd(cmd, cwd=REPO)           # cwd: POM1 resolves roms/ relative
    return r, snap


def parse_snapshot_ram(snap: Path) -> bytes:
    data = snap.read_bytes()
    if data[:8] != b"POM1SNAP":
        raise RuntimeError(f"bad snapshot magic in {snap}")
    off = 16                              # magic + u32 version + u32 flags
    while off + 12 <= len(data):
        name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        (length,) = struct.unpack_from("<I", data, off + 8)
        if name == "MEM":
            ram = data[off + 12:off + 12 + 65536]
            if len(ram) != 65536:
                raise RuntimeError("truncated MEM section")
            return ram
        off += 12 + length
    raise RuntimeError("MEM section not found in snapshot")


def check_expects(spec: TestSpec, ram: bytes) -> list[str]:
    errs = []
    for addr, want in spec.expects:
        got = ram[addr:addr + len(want)]
        if got != want:
            errs.append(
                f"  mailbox mismatch at ${addr:04X}:\n"
                f"    want {want.hex(' ')}\n"
                f"    got  {got.hex(' ')}")
    return errs


def run_one(spec: TestSpec, keep: bool, verbose: bool) -> tuple[bool, str, float]:
    t0 = time.monotonic()
    workdir = Path(tempfile.mkdtemp(prefix=f"pom1_micro_{spec.path.stem}_"))
    try:
        if spec.path.suffix == ".c":
            if spec.mode == "codetank":
                artefact = build_c_codetank(spec, workdir)
            elif spec.mode == "gen2":
                artefact = build_c_gen2(spec, workdir)
            else:
                return False, ("MODE for .c tests must be 'codetank' or 'gen2', "
                               f"got '{spec.mode}'"), 0.0
        else:
            artefact = build_asm(spec, workdir)
        r, snap = run_pom1(spec, artefact, workdir)
        errs = []
        if r.returncode != 0:
            errs.append(f"  POM1 exited {r.returncode}")
        drops = [l for l in r.stderr.splitlines() if "[TMS9918 DROP" in l]
        if drops:
            errs.append(f"  {len(drops)} silicon-strict DROP line(s), first:\n"
                        f"    {drops[0]}")
        if not snap.exists():
            errs.append("  snapshot file was not written")
        else:
            ram = parse_snapshot_ram(snap)
            errs += check_expects(spec, ram)
        if verbose and not errs:
            print(f"    cmd: {' '.join(str(c) for c in r.args)}")
        msg = "\n".join(errs)
        return (not errs), msg, time.monotonic() - t0
    except Exception as e:  # build error, timeout, parse error…
        return False, f"  {e}", time.monotonic() - t0
    finally:
        if keep:
            print(f"    workdir kept: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-k", "--filter", default="",
                    help="only run tests whose filename contains this substring")
    ap.add_argument("--keep", action="store_true",
                    help="keep per-test temp build dirs")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not POM1_BIN.exists():
        print(f"SKIP: {POM1_BIN} not found (build POM1 first)")
        return 77
    if shutil.which("ca65") is None or shutil.which("ld65") is None:
        print("SKIP: cc65 (ca65/ld65) not on PATH")
        return 77
    have_cl65 = shutil.which("cl65") is not None

    sources = sorted(list(MICRO_DIR.glob("*.s")) + list(MICRO_DIR.glob("*.c")))
    sources = [p for p in sources if args.filter in p.name]
    if not sources:
        print(f"SKIP: no micro-tests found in {MICRO_DIR}")
        return 77

    failures = 0
    ran = 0
    t0 = time.monotonic()
    for path in sources:
        try:
            spec = TestSpec(path)
        except ValueError as e:
            print(f"FAIL {path.name}\n  {e}")
            failures += 1
            continue
        if path.suffix == ".c" and not have_cl65:
            print(f"skip {path.name} (cl65 not on PATH)")
            continue
        ok, msg, dt = run_one(spec, args.keep, args.verbose)
        ran += 1
        if ok:
            print(f"PASS {path.name}  ({dt:.1f}s)")
        else:
            print(f"FAIL {path.name}  ({dt:.1f}s)\n{msg}")
            failures += 1

    total = time.monotonic() - t0
    print(f"\n{ran - failures}/{ran} micro-tests passed in {total:.1f}s "
          f"({len(sources) - ran} skipped)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
