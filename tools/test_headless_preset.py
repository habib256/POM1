#!/usr/bin/env python3
# Proves `--headless --preset N` applies the preset's cards with no display, by
# discriminating on a GEN2-only behaviour: a 6502 program reads a GEN2 soft
# switch ($C250, whose D7 = HST0 = the cycle-accurate beam blank flag) and emits
# it each frame.
#   * with --preset 12 (Uncle Bernie GEN2 HGR): the scanner runs -> HST0 toggles,
#   * with no preset (default 64K, $C250 is plain RAM): D7 is constant 0.
# So a toggling D7 can only mean the GEN2 card was actually plugged headless.
#
#   python3 tools/test_headless_preset.py [path/to/POM1]
# Exits 77 (skip) if POM1 is missing.

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pom1_telemetry import launch_headless  # noqa: E402

# $0300: arm lock-step; loop: A = ($C250 & $80) [D7=HST0], emit it, end-frame, loop.
PROG = bytes([
    0xA9, 0x02, 0x8D, 0x41, 0xC4,   # LDA #$02 / STA $C441   (arm lock-step)
    0xAD, 0x50, 0xC2,               # LDA $C250              (GEN2 soft switch; D7=HST0)
    0x29, 0x80,                     # AND #$80               (isolate D7)
    0x8D, 0x40, 0xC4,               # STA $C440              (TELE_DATA = HST0 bit)
    0xA9, 0x01, 0x8D, 0x41, 0xC4,   # LDA #$01 / STA $C441   (end-frame -> park)
    0x4C, 0x05, 0x03,               # JMP $0305              (loop)
])
GEN2_PRESET = 12
FRAMES = 96


def collect_d7(pom1, prog, preset):
    """Run the emitter (optionally under --preset) and return the set of D7
    values (0x00 / 0x80) seen across FRAMES frames."""
    extra = ("--preset", str(preset)) if preset is not None else ()
    port = 6610 if preset is not None else 6611
    with launch_headless(prog, load_addr=0x0300, port=port, pom1=pom1, extra=extra) as tc:
        seen = set()
        frame = tc.read_frame(4.0)
        assert frame is not None and len(frame) == 1, f"first frame: {frame!r}"
        for _ in range(FRAMES):
            seen.add(frame[0])
            frame = tc.step()
            assert frame is not None and len(frame) == 1, f"frame: {frame!r}"
        return seen


def main():
    pom1 = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("POM1", "build/POM1")
    if not os.path.exists(pom1):
        print(f"SKIP: {pom1} not found (build first)"); return 77

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(PROG); prog = f.name
    try:
        gen2 = collect_d7(pom1, prog, GEN2_PRESET)
        assert 0x80 in gen2 and 0x00 in gen2, \
            f"--preset {GEN2_PRESET}: GEN2 HST0 should toggle, saw {sorted(gen2)}"

        plain = collect_d7(pom1, prog, None)
        assert plain == {0x00}, \
            f"no preset: $C250 is plain RAM, D7 should be constant 0, saw {sorted(plain)}"

        print(f"PASS: --headless --preset {GEN2_PRESET} plugs GEN2 (HST0 toggles); "
              f"default 64K machine does not — preset/cards applied with no display")
        return 0
    except (OSError, AssertionError, RuntimeError) as e:
        print(f"FAIL: {e}"); return 1
    finally:
        os.unlink(prog)


if __name__ == "__main__":
    sys.exit(main())
