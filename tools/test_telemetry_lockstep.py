#!/usr/bin/env python3
# Regression test for the telemetry side channel's deterministic lock-step mode,
# built on the pom1_telemetry harness library. Runs POM1 with --headless, so it
# needs no display (CI-friendly).
#
# Assembles a tiny 6502 program that arms lock-step and emits one frame per loop
# (parking on the end-frame marker), then verifies over TCP that:
#   1. the parked frame is delivered on connect,
#   2. NO further frame arrives without an ACK (the CPU is provably parked),
#   3. each ACK releases exactly one more frame.
#
#   python3 tools/test_telemetry_lockstep.py [path/to/POM1]
# Exits 77 (skip) if POM1 is missing.

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pom1_telemetry import launch_headless  # noqa: E402

# $0300: arm lock-step ($02->$C441), then per-frame write $AA to TELE_DATA +
# end-frame marker (parks until ACK), JMP loop.
PROG = bytes([
    0xA9, 0x02, 0x8D, 0x41, 0xC4,   # LDA #$02 / STA $C441  (arm lock-step)
    0xA9, 0xAA, 0x8D, 0x40, 0xC4,   # LDA #$AA / STA $C440  (TELE_DATA)
    0xA9, 0x01, 0x8D, 0x41, 0xC4,   # LDA #$01 / STA $C441  (end-frame -> park)
    0x4C, 0x05, 0x03,               # JMP $0305            (loop)
])
PORT = 6602


def main():
    pom1 = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("POM1", "build/POM1")
    if not os.path.exists(pom1):
        print(f"SKIP: {pom1} not found (build first)"); return 77

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(PROG); prog = f.name
    try:
        with launch_headless(prog, load_addr=0x0300, port=PORT, pom1=pom1) as tc:
            assert tc.read_frame(4.0) == b"\xaa", "first (parked) frame"
            assert tc.read_frame(0.8) is None, "expected park: no frame without ACK"
            for n in (1, 2, 3):
                assert tc.step() == b"\xaa", f"frame after ACK #{n}"
                assert tc.read_frame(0.8) is None, f"expected re-park after ACK #{n}"
        print("PASS: lock-step delivers exactly one frame per ACK; CPU parks between frames")
        return 0
    except (OSError, AssertionError, RuntimeError) as e:
        print(f"FAIL: {e}"); return 1
    finally:
        os.unlink(prog)


if __name__ == "__main__":
    sys.exit(main())
