# A1 Telemetry Demo — the POM1 game-testing SDK in ~30 lines

A minimal **homing game** that exists only to demonstrate the full automated
test loop Uncle Bernie described as the "dream SDK": *compile → load into the
emulator → run an automated test that sees the game state and decides the
inputs* — with no human and no display.

Each frame the game emits its state `[player, target, won]` to the telemetry
port and **parks** (lock-step). The external test reads the frame, sends a
direction byte (`1` = +1, `2` = −1) and ACKs; the game steps the player one tick
and loops. The player converges on the target, driven entirely by the test.

## The three SDK pieces it exercises

| Piece | File |
|-------|------|
| 6502-side library (equates + macros) | `dev/lib/telemetry/telemetry.inc` |
| Python harness library | `tools/pom1_telemetry.py` |
| The automated test (worked example) | `tools/test_telemetry_demo.py` |

Mechanism + protocol: [`doc/TELEMETRY_SIDE_CHANNEL.md`](../../../doc/TELEMETRY_SIDE_CHANNEL.md).

## Build · run · test

```bash
# 1. assemble (cc65) -> software/Telemetry/A1_TelemetryDemo.bin (origin $0280)
make -C dev/projects/a1_telemetry_demo

# 2. (optional) run it by hand and connect your own harness
build/POM1 --headless --telemetry-port 6601 \
           --load 0280:software/Telemetry/A1_TelemetryDemo.bin --run 0280

# 3. the automated test — headless, no display
python3 tools/test_telemetry_demo.py
```

The test launches POM1 itself (`--headless`), so it runs in CI on a display-less
box. Write your own game's test the same way: emit your state with the
`telemetry.inc` macros, then drive it from a few lines of `pom1_telemetry`.
