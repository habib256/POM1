#!/usr/bin/env python3
# Worked example for the POM1 telemetry SDK: drive the A1_TelemetryDemo "homing"
# game to its target using the pom1_telemetry harness library — no display, no
# human (CI-friendly via --headless). This is the end-to-end demonstration of
# the "dream SDK" loop: read game state -> decide input -> game converges.
#
#   Build : open sketchs/apple1/demo_telemetry/ in DevBench, or assemble A1_TelemetryDemo.asm
#   Run   : python3 tools/test_telemetry_demo.py [path/to/POM1]
# Exits 77 (skip) if POM1 or the demo binary is missing.

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pom1_telemetry import launch_headless, ProtocolError  # noqa: E402

PROG = "software/Telemetry/A1_TelemetryDemo.bin"
PORT = 6601


def main():
    pom1 = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("POM1", "build/POM1")
    if not os.path.exists(pom1):
        print(f"SKIP: {pom1} not found (build first)"); return 77
    if not os.path.exists(PROG):
        print(f"SKIP: {PROG} not found (build sketchs/apple1/demo_telemetry via DevBench)"); return 77

    try:
        with launch_headless(PROG, load_addr=0x0280, port=PORT, pom1=pom1) as tc:
            frame = tc.read_frame(4.0)
            assert frame is not None and len(frame) == 3, f"first frame: {frame!r}"

            for i in range(64):
                player, target, won = frame[0], frame[1], frame[2]
                if won:
                    print(f"PASS: harness homed player onto target ({target}) in "
                          f"{i} frames — deterministic, headless, no human")
                    return 0
                move = b"\x01" if player < target else b"\x02"   # 1 = +1, 2 = -1
                frame = tc.step(move)
                assert frame is not None and len(frame) == 3, f"frame {i}: {frame!r}"

            print("FAIL: player did not converge on target within 64 frames")
            return 1
    except (OSError, ProtocolError, AssertionError, RuntimeError) as e:
        print(f"FAIL: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
