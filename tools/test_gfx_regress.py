#!/usr/bin/env python3
"""
test_gfx_regress.py -- golden-image regression for POM1's graphics cards.

Launches POM1 headless, (optionally) loads + runs a program, renders the GEN2
HGR or TMS9918 framebuffer with no display (--dump-gen2-frame / --dump-tms-frame)
and compares the captured PNG byte-for-byte (sha256) against a committed golden
image. The capture is deterministic via --dump-after-cycles (a host-independent
point in emulated time), so the comparison is stable across machines.

This is the host side of the "develop graphics routines, regression-test the
*pixels*" loop -- the graphics-output counterpart to the telemetry side channel
(which tests game *state*). See doc/CLI.md (--dump-*-frame) and
doc/TELEMETRY_SIDE_CHANNEL.md.

Usage:
  test_gfx_regress.py --card gen2|tms --golden PATH [--load ADDR:bin] [--run ADDR]
      [--preset N] [--after-cycles N | --settle-ms N] [--pom1 PATH] [--update]

  --update   regenerate the golden from the current render, then exit 0.

Exit: 0 = match (or --update). 1 = mismatch / capture error. 77 = POM1 not
built (SKIP) -- ctest maps 77 to "skipped" via SKIP_RETURN_CODE.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile


def find_pom1(explicit):
    for c in (explicit, os.environ.get("POM1"), "build/POM1"):
        if c and os.path.exists(c):
            return c
    d = os.path.abspath(".")
    while True:
        p = os.path.join(d, "build", "POM1")
        if os.path.exists(p):
            return p
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def dump_child(out, err):
    """Print the tail of POM1's own output. Bounded: a hung emulator can have
    logged a great deal, and only the end of it says where it stopped."""
    for label, blob in (("stdout", out), ("stderr", err)):
        if not blob:
            continue
        text = blob.decode("utf-8", "replace") if isinstance(blob, bytes) else blob
        lines = text.strip().splitlines()
        if not lines:
            continue
        shown = lines[-40:]
        elided = len(lines) - len(shown)
        print(f"  --- POM1 {label} ({len(lines)} lines"
              + (f", last {len(shown)}" if elided else "") + ") ---")
        for line in shown:
            print("  | " + line)


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    ap = argparse.ArgumentParser(
        description="golden-image regression for POM1 graphics cards")
    ap.add_argument("--card", required=True, choices=["gen2", "tms"])
    ap.add_argument("--golden", required=True, help="path to the committed golden PNG")
    ap.add_argument("--load", default="", help="ADDR:binary to load (e.g. 0xE000:prog.bin)")
    ap.add_argument("--run", default="", help="ADDR to run after loading")
    ap.add_argument("--preset", default=None, help="machine preset (11=GEN2, 9=TMS9918)")
    ap.add_argument("--after-cycles", type=int, default=2_000_000,
                    help="deterministic settle in emulated cycles (default 2,000,000)")
    ap.add_argument("--settle-ms", type=int, default=0,
                    help="wall-clock settle instead of --after-cycles (non-deterministic; debug only)")
    ap.add_argument("--pom1", default=None, help="path to POM1 (else $POM1, else build/POM1)")
    ap.add_argument("--update", action="store_true", help="regenerate the golden, then exit 0")
    ap.add_argument("--timeout", type=int, default=90,
                    help="seconds to wait for the capture (default 90). Must stay BELOW the "
                         "ctest TIMEOUT: whoever fires first owns the diagnosis, and only this "
                         "one can print what POM1 said.")
    args = ap.parse_args()

    pom1 = find_pom1(args.pom1)
    if not pom1:
        print("SKIP: POM1 not built (no build/POM1) -- build it first; CI builds it")
        return 77

    out = tempfile.NamedTemporaryFile(suffix=".png", delete=False).name
    try:
        flag = "--dump-gen2-frame" if args.card == "gen2" else "--dump-tms-frame"
        cmd = [pom1, "--headless", flag, out]
        if args.preset is not None:
            cmd += ["--preset", str(args.preset)]
        if args.load:
            cmd += ["--load", args.load]
        if args.run:
            cmd += ["--run", args.run]
        if args.settle_ms > 0:
            cmd += ["--dump-settle-ms", str(args.settle_ms)]
        else:
            cmd += ["--dump-after-cycles", str(args.after_cycles)]

        # Capture the child's output rather than discarding it, and bound the
        # wait. Both halves are the same lesson, learned on a Windows CI job
        # that hung this capture for 61 s where it normally takes 1.1 s: with
        # DEVNULL and no timeout, ctest killed *python* and the report said
        # only "Timeout" — POM1's own log, the one artefact that could say
        # where it stopped, had been thrown away, and the emulator it left
        # behind was never reaped. A harness that cannot explain its own
        # failure costs a CI cycle every time it fires.
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  timeout=args.timeout)
            rc, child_out, child_err = proc.returncode, proc.stdout, proc.stderr
        except subprocess.TimeoutExpired as e:
            print(f"FAIL: POM1 capture did not finish within {args.timeout}s "
                  f"(normally ~1 s) — child killed")
            print("  cmd: " + " ".join(cmd))
            dump_child(e.stdout, e.stderr)
            return 1
        if rc != 0 or not os.path.exists(out) or os.path.getsize(out) == 0:
            print(f"FAIL: POM1 capture failed (rc={rc})")
            print("  cmd: " + " ".join(cmd))
            dump_child(child_out, child_err)
            return 1

        if args.update:
            os.makedirs(os.path.dirname(os.path.abspath(args.golden)), exist_ok=True)
            shutil.copyfile(out, args.golden)
            print(f"UPDATED golden {args.golden} ({sha(args.golden)[:16]})")
            return 0

        if not os.path.exists(args.golden):
            print(f"FAIL: golden missing: {args.golden} (create it with --update)")
            return 1

        got, want = sha(out), sha(args.golden)
        if got == want:
            print(f"PASS: {args.card} frame matches golden ({got[:16]})")
            return 0
        print(f"FAIL: {args.card} frame differs from golden\n"
              f"  got : {got[:16]}\n  want: {want[:16]}\n"
              f"  (review the render; if the change is intended, re-run with --update)")
        return 1
    finally:
        try:
            os.unlink(out)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
