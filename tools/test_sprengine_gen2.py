#!/usr/bin/env python3
"""
test_sprengine_gen2.py -- end-to-end validation of the GEN2 masked sprite
engine (dev/lib/gen2c gen2_sprmask.s + gen2_sprengine.c) through the REAL
toolchain and the REAL emulator.

Builds sketchs/gen2/demo_sprengine's SELFTEST binary with cc65 (the phased
variant of the demo: background -> 3 masked sprites drawn over the decor ->
sprites hidden/restored -> idle), then captures the GEN2 framebuffer headless
at three deterministic points in emulated time (--dump-after-cycles) plus a
no-program blank baseline, and asserts:

  1. background != blank        (the decor is non-uniform -- the test means something)
  2. sprites   != background    (the masked draw actually shows sprites)
  3. restored  == background    (SHA-identical PNG: the save-under + masked
                                 draw + restore round trip put every covered
                                 byte back EXACTLY -- palette bit included)

The three windows are frame-counted in the selftest binary (gen2_wait_vbl),
each several million cycles wide, so the fixed sample points below are stable
across machines (headless emulated time is host-independent).

Exit: 0 = pass. 1 = fail. 77 = SKIP (no cc65 or no POM1 binary) -- ctest maps
77 to "skipped" via SKIP_RETURN_CODE.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

# Sample points (emulated cycles). See main.c (SPRENGINE_SELFTEST): window A
# (background only) spans ~[<2M .. >7M], window B (sprites drawn) ~[<8M ..
# >13M], window C (restored, idle forever) from ~<15M on.
CYCLES_BACKGROUND = 4_000_000
CYCLES_SPRITES = 10_000_000
CYCLES_RESTORED = 16_000_000

DEMO_DIR = "sketchs/gen2/demo_sprengine"
TEST_BIN = "main_test.bin"


def find_pom1(explicit):
    for c in (explicit, os.environ.get("POM1"), "build/POM1"):
        if c and os.path.exists(c):
            return c
    return None


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def capture(pom1, repo, out, cycles, load=None):
    cmd = [pom1, "--headless", "--preset", "11",
           "--dump-gen2-frame", out, "--dump-after-cycles", str(cycles)]
    if load:
        cmd += ["--load", "0x6000:" + load, "--run", "0x6000"]
    rc = subprocess.run(cmd, cwd=repo, stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL).returncode
    if rc != 0 or not os.path.exists(out) or os.path.getsize(out) == 0:
        print(f"FAIL: POM1 capture failed (rc={rc}, cycles={cycles})")
        return None
    return sha(out)


def main():
    ap = argparse.ArgumentParser(
        description="masked sprite engine draw/restore round-trip (GEN2)")
    ap.add_argument("--pom1", default=None,
                    help="path to POM1 (else $POM1, else build/POM1)")
    ap.add_argument("--repo", default=None,
                    help="repo root (default: parent of this script's dir)")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo or
                           os.path.join(os.path.dirname(__file__), ".."))
    pom1 = find_pom1(args.pom1)
    if not pom1:
        print("SKIP: POM1 not built (no build/POM1)")
        return 77
    pom1 = os.path.abspath(pom1)
    if not shutil.which("cl65"):
        print("SKIP: cc65 (cl65) not on PATH")
        return 77

    # Build the phased selftest binary through the demo's own Makefile (also
    # regenerates sprites_msk.h from sprites.txt if the tool changed).
    demo = os.path.join(repo, DEMO_DIR)
    r = subprocess.run(["make", "-C", demo, "selftest"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        print("FAIL: could not build the selftest demo:\n"
              + r.stdout.decode(errors="replace")[-2000:])
        return 1
    binpath = os.path.join(demo, TEST_BIN)

    tmp = tempfile.mkdtemp(prefix="sprengine_")
    try:
        f = lambda n: os.path.join(tmp, n + ".png")
        blank = capture(pom1, repo, f("blank"), CYCLES_BACKGROUND)
        bg = capture(pom1, repo, f("bg"), CYCLES_BACKGROUND, binpath)
        spr = capture(pom1, repo, f("spr"), CYCLES_SPRITES, binpath)
        rst = capture(pom1, repo, f("rst"), CYCLES_RESTORED, binpath)
        if None in (blank, bg, spr, rst):
            return 1

        ok = True
        if bg == blank:
            print("FAIL: background frame equals blank frame "
                  "(decor was not drawn -- did the binary run?)")
            ok = False
        if spr == bg:
            print("FAIL: sprite frame equals background frame "
                  "(masked draw invisible)")
            ok = False
        if rst != bg:
            print("FAIL: restored frame differs from background frame "
                  f"(round trip not byte-exact)\n  bg : {bg[:16]}\n"
                  f"  rst: {rst[:16]}")
            ok = False
        if ok:
            print(f"PASS: masked draw visible ({spr[:16]}) and draw+restore "
                  f"round trip byte-exact ({bg[:16]})")
            return 0
        return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
