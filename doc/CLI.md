# POM1 command-line flags

Full reference for headless / scripted runs. Implementation: **`CliDispatcher.cpp`**. Emulator architecture (mutex order, peripherals, `ctest` pins) → [`CLAUDE.md`](../CLAUDE.md).

Three phases: **A** boot-time, **B** first-frame preset overrides, **C** deferred — fire after the 15-frame card plug-in, in command-line order. Example: `--load 0300:foo.bin --run 0300 --paste keys.txt` composes. Malformed verbs log `[CLI] ERROR:` and exit 1.

| Flag | Phase | Effect |
|------|-------|--------|
| `--list-presets` | A | Print `kMachinePresets[]` and exit. |
| `--preset <N\|name>` / `-p` | A | Index or case-insensitive substring (first match). |
| `--terminal` | A | Force-enable Terminal Card (`127.0.0.1:6502`). |
| `--telemetry-port <N>` | A | Dev-only: open the telemetry side channel on `127.0.0.1:N` (1-65535). Binary frame-delimited bridge for automated game testing — the game writes state to `$C440-$C443`, an external harness reads it + drives input. See [`TELEMETRY_SIDE_CHANNEL.md`](TELEMETRY_SIDE_CHANNEL.md). |
| `--telemetry-log <path>` | A | Dev-only: tee the telemetry outbound frame stream (`0xAA <len16> <payload>`…) to a binary file for golden-trace CI (no live harness needed — diff against an expected capture). Implies enabling the port (default `:6503` unless `--telemetry-port` also given). |
| `--dump-gen2-frame <path>` | A | Headless one-shot: render the GEN2 HGR framebuffer (280×192) to a PNG, then exit — automated **graphics regression** (golden-image diff). Implies `--headless`. Combine with `--preset 11` (or `--enable hgr`) + `--load`/`--run` to capture a program's output. The render path is the same software rasteriser the GUI uses, so the capture is pixel-identical. Logs `[GFX] GEN2 frame WxH hash=0x… -> path` — an FNV golden you can assert without diffing files. |
| `--dump-tms-frame <path>` | A | Same, for the TMS9918 framebuffer (288×216, incl. the R7 border). Pair with `--preset 9` (or `--enable tms9918`). |
| `--dump-after-cycles <N>` | A | Deterministic settle for `--dump-*-frame`: run exactly N emulated cycles (host-independent) before the capture, instead of the wall-clock `--dump-settle-ms`. **Use this for regression** so the golden is stable across machines (e.g. `2000000`). |
| `--dump-settle-ms <N>` | A | Wall-clock settle (default 1000) before a `--dump-*-frame` capture. Quick/interactive; non-deterministic — prefer `--dump-after-cycles` for CI. |
| `--tape <path>` | A | Preload + auto-Play on boot (any preset). Default WOZ talk loads only with the POM1 Fantasy preset. |
| `--save-tape <path>` / `--save-tape-format <aci\|wav>` | A | Dump deck on clean shutdown. SIGINT/SIGTERM triggers `~MainWindow_ImGui`. |
| `--cpu-max` | A | Pin `executionSpeed = 1 000 000` cycles/frame. Beats `--speed`. |
| `--headless` | A | Run with **no GLFW window / GL / ImGui** — for CI & scripted runs on a display-less box. Default 64K machine + execution speed + telemetry + all Phase-C deferred verbs (`--load`/`--run`/`--paste`/`--step`/`--sd-*`/`--snapshot`/`--break`); idles until SIGINT/SIGTERM. Applies `--preset` / `--enable` / `--disable` (RAM + cards + BASIC ROM, plugged immediately) — e.g. `--preset 11` plugs GEN2 for headless HGR game tests. Pairs with `--telemetry-port` / `--telemetry-log` (see `tools/test_telemetry_lockstep.py`, `tools/test_headless_preset.py`). |
| `--speed <cycles/frame>` | B | Override (1× = 17045, 2× = 34091). |
| `--enable <card>[,…]` / `--disable <card>[,…]` | B | Rewrite deferred plug list. Names: `aci, sid, sid-se, microsd, tms9918, a1io-rtc, hgr, cffa1, krusader, wifi, terminal, jukebox, codetank, pr40, gt6144, iec`. `iec` cascade-enables microSD. Mutex rules enforced. `--disable krusader` is a no-op (ROM unload needs hard reset). Run after `--terminal`. |
| `--sid-chip <6581\|8580>` | B | Swap chip; libresidfp replays last register state. |
| `--jukebox-jumper <ram16\|ram32>` / `--jukebox-chip <flash\|eeprom>` | B | Juke-Box ROM window + chip mode. |
| `--codetank-jumper <lower\|upper>` / `--codetank-rom <path>` | B | Pick 16 kB half of 32 kB 28c256 / override default `roms/codetank/Codetank_GAME1.rom`. `--enable codetank` auto-schedules `--enable tms9918`; `--disable tms9918` cascade-unschedules CodeTank. |
| `--iec-disk <path>` | B | Mount a `.d64` (174 848 B standard 1541 image) on the P-LAB IEC daughterboard's virtual drive 8 at startup. Overrides the default `disks/iec/dev8.d64` probe. Failure logs `[IEC] WARN: --iec-disk: failed to mount <path>`. |
| `--silicon-strict` / `--no-silicon-strict` | B | Force-flip TMS9918 `siliconStrictMode` (paranoid 40c — drops VRAM writes < 40 cycles in Mode I+sprites, 4/6/6/2c in text/multicolor/no-sprites/blank, 4K/16K via R1 bit 7 — see `sketchs/doc/Programming_TMS9918.md` §17 Bug N°1). Contract: passing POM1 strict ⇒ silicon-safe. May 2026 ramp: 16→24→40c after Galaga sprite tables and LOGO BIRD/Demo redraws kept showing artefacts at every previous level. Default = ON for every preset except the Multiplexing Fantasy ones; the override survives the first frame but a later preset switch resets to default. Hardware menu has a runtime toggle and the status bar shows `STRICT` / `FANTASY`. |
| `--dram-refresh` / `--no-dram-refresh` | B | Force-flip the Apple-1 DRAM-refresh stall (4/65 cycles stolen from the CPU; the video beam keeps running, so beam-race GEN2 demos drift as on real DRAM silicon). Independent of `--silicon-strict` so headless beam-race captures can isolate it. Default follows the preset (ON except Multiplexing Fantasy); the override survives the first frame but a later preset switch resets to default. Runtime toggle in the Hardware → Silicon Strict inspector. |
| `--load <addr>:<path>` | C | Raw binary, rewrite reset, `hardReset` + `start`. Hex/decimal addr. Repeatable. |
| `--run <addr>` | C | `EmulationController::jumpTo()`. |
| `--paste <file>` | C | ≤4096 chars to keyboard queue (`\n`→CR, printable ASCII). |
| `--step <N>` / `--trace-brk` | C | Step N + BRK trace dump. |
| `--play` / `--rec` / `--rewind` | C | Cassette transport. `--rec` = `armRecording()` (no $C000 wait). |
| `--sd-mkdir <path>` / `--sd-put <h>:<g>` / `--sd-get <g>:<h>` | C | SD fixture seeding. |
| `--rtc-freeze "YYYY-MM-DD HH:MM:SS"` | C | Set `A1IO_RTC::rtcOffsetSeconds` (host rate keeps ticking). |
| `--snapshot-save <path>` | C | Write current state (RAM + card-enabled flags + per-card payload via `Peripheral::serialize`) to `<path>`. Format: see `SnapshotIO.h`. |
| `--snapshot-load <path>` | C | Restore state from a `.snap` written by `--snapshot-save`. Per-card serialize hooks default to no-op until each card migrates its internal state — see `Peripheral.h`. |
| `--break <addr>` | C | Arm M6502 PC-matched halt (single breakpoint). Fires *before* the instruction at `<addr>` executes; CPU stops itself, logs `[CPU] WARN breakpoint hit at $XXXX` once. Cleared by `hardReset()` (preset switch). Continue with manual `stepCpu()` past the address followed by `startCpu()`, or `clearCpuBreakpoint()` + `startCpu()`. |

Two telnet tests auto-launch POM1 from repo root: `tools/test_aci_telnet.py`, `tools/test_sdcard_subdir_navigation_telnet.py`.

`tools/test_gfx_regress.py` does **golden-image graphics regression** via the `--dump-*-frame` capture (launch headless → render → sha256 vs a committed golden; `--update` regenerates it). Frozen fixtures live in `tests/gfx/`; pinned by the `gfx_regress_*` ctest.
