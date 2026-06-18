# GEN2 Snake — telemetry-reporting Snake on Uncle Bernie's GEN2 HGR card

*[← POM1 documentation index](../../../../doc/README.md)*

Classic **Snake**, in C (cc65), on Uncle Bernie's **GEN2 HGR colour graphics
card** (280×192 HIRES) — and it reports its state live over POM1's
**telemetry side channel** using the new *self-describing schema* frames.

> *Snake for Uncle Bernie's GEN2 HGR card / VERHILLE Arnaud 2026*

## What it does

- Draws the snake, the food and the top/bottom walls on the GEN2 HIRES screen,
  with the score printed top-right via the flicker-free `gen2_hgr_putu_field`
  HUD renderer (Beautiful Boot font) and a cycling-colour "HGR Snake" label
  top-left.
- **Telemetry:** at startup it emits a **schema frame** declaring exactly five
  fields, in order — `head_x:U8`, `head_y:U8`, `length:U8`, `alive:BOOL`,
  `score:U16` — then a **data frame** `[head_x, head_y, length, alive, score]`
  every game tick. It runs in **FREE-RUN** (no lock-step), so the game plays
  live and the UI shows state in real time.
- **Controls:** `WASD` *and* `ZQSD` (both layouts) set the heading; 180° reversal
  is forbidden. A test harness can also drive the snake by pushing a direction
  byte to `TELE_IN`: `1`=up, `2`=down, `3`=left, `4`=right.
- On death it shows `GAME OVER`, keeps emitting `alive=0` frames so the UI sees
  the death, and restarts on any key.

## Cell → pixel mapping

The 280×192 screen is an **8×8-pixel cell grid: 35 columns (0..34) × 24 rows
(0..23)** — cell `(cx, cy)` lives at pixel `(cx*8, cy*8)`. Each snake segment is
a solid white **6×6 block** inside its cell (1px gap on the right/bottom for grid
readability). The apple is a solid **red disk** (orange — HIRES's warmest tone),
and every few apples a time-limited **green bonus gem** appears for extra points.
Only the **top and bottom walls** exist (1px rules); the left/right sides are
**open**, so the snake **wraps horizontally** edge to edge. Rows 0–1 above the
top wall hold the score HUD; the playable rows are `TOP_WALL+1 .. ROWS-2`.

## Build

```bash
make -C dev/projects/gen2c/game_snake_telemetry
# -> software/Telemetry/GEN2Snake.bin (+ GEN2Snake.txt, Wozmon hex), origin $6000
```

This mirrors the POM1 Bench's *Uncle Bernie GEN2 HGR (C)* `cl65` invocation
(GEN2 C linker config `dev/cc65/apple1_gen2_c.cfg` + the `gen2c` runtime + the
shared `apple1c` text/keyboard base + `dev/lib/telemetry/telemetry.h`).

## Run in the DevBench

**DevBench → POM1 Bench → Examples → *Snake telemetry*** — target = **C / GEN2
HGR**. The example selects preset 11 (GEN2 plugged) and runs from `$6000`. Open
the **DevBench → Telemetry Side Channel** window (or the Bench *Serial Monitor*)
to watch the decoded **named** table: `head_x`, `head_y`, `length`, `alive`, `score`.

Headless, with a harness:

```bash
build/POM1 --headless --telemetry-port 6602 --preset 11 \
           --load 6000:software/Telemetry/GEN2Snake.bin --run 6000
```

```python
from pom1_telemetry import launch_headless
with launch_headless("software/Telemetry/GEN2Snake.bin",
                     load_addr=0x6000, port=6602, extra=["--preset", "11"]) as tc:
    st = tc.read_named()              # {"head_x":.., "head_y":.., "length":.., "alive":True, "score":..}
    tc.send(b"\x04")                  # steer right (1=up 2=down 3=left 4=right)
    st = tc.read_named()
```

## The SDK pieces it exercises

| Piece | File |
|-------|------|
| C telemetry companion (schema helpers) | `dev/lib/telemetry/telemetry.h` |
| asm telemetry equates/macros | `dev/lib/telemetry/telemetry.inc` |
| GEN2 HGR C runtime | `dev/lib/gen2c/gen2.h` |
| Apple-1 text/keyboard C base | `dev/lib/apple1c/apple1io.h` |
| Python harness (schema decode) | `tools/pom1_telemetry.py` |

Protocol + schema-frame spec: [`doc/TELEMETRY_SIDE_CHANNEL.md`](../../../../doc/TELEMETRY_SIDE_CHANNEL.md).
