# GEN2Countdown — 20 → 0 counter on the GEN2 HGR card

*[← POM1 documentation index](../../../../doc/README.md)*

Tiny **C** program for Uncle Bernie's GEN2 colour card: displays a large
centred digit on the HIRES 280×192 screen and counts down from **20 to 0**
(one number per ~second), prints `LIFTOFF!` at zero, then **hands control
back to Wozmon** (the `\` prompt on the Apple-1 text display).

It's a minimal example of the **"Uncle Bernie GEN2 HGR (C)"** target: it
relies on the `dev/lib/gen2c` runtime (`gen2_hgr_init` / `gen2_hgr_clear` /
`gen2_hgr_puts` / `gen2_hgr_putu`) and the text base under `dev/lib/apple1c`
(`woz_mon`). No telemetry.

## Build

```sh
make            # -> "software/Graphic HGR/GEN2Countdown.bin" (+ .txt Woz-hex)
make clean
```

The `Makefile` reuses the POM1 Bench `cl65` invocation: linker config
`dev/cc65/apple1_gen2_c.cfg` (code + C stack at `$6000-$BEFF`, above the GEN2
framebuffers) + the gen2c per-family modules + `apple1io.c`/`apple1io_asm.s`,
origin `$6000`.

## Run

- **DevBench → POM1 Bench**: new sketch, target *C / GEN2 HGR*, paste the
  source, compile, upload.
- **CLI**:
  ```sh
  build/POM1 --preset 11 \
      --load 6000:"software/Graphic HGR/GEN2Countdown.bin" --run 6000
  ```
- Loading the `.bin`/`.txt` from `software/Graphic HGR/` auto-plugs the GEN2
  card and opens its window (preset 11 = Uncle Bernie's GEN2 HGR Color).

## Cadence tuning

The countdown speed is a coarse CPU busy loop, `#define TICK_SPINS 55000u`
in `GEN2Countdown.c` (~1 s at ~1 MHz). Increase to slow down, decrease to
speed up.
