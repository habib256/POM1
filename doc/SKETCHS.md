# POM1 Sketchs — folder layout & copy-me starters

Editable source files for the **POM1 DevBench**, organized by machine profile at
the repo root in [`sketchs/`](../sketchs/). DevBench opens this folder by default.
When a sketch is loaded, the path and file extension select the active environment
(ASM/C + Apple-1 / TMS9918 / GEN2 HGR).

Workflow (Verify, Upload, target matrix) → [`DEVBENCH.md`](DEVBENCH.md).

---

## Layout

| Path | Contents |
|---|---|
| `sketchs/apple1/<sketch>/` | Apple-1 text, GT-6144, P-LAB SID/RTC, Wozmon-hex starters |
| `sketchs/tms9918/<sketch>/` | P-LAB TMS9918, TMS9918C and CodeTank sketches |
| `sketchs/gen2/<sketch>/` | Uncle Bernie GEN2 HGR (asm and gen2c C) |

Copy-me starters sort to the top of each profile thanks to the `_` prefix:

| Folder | What |
|---|---|
| [`sketchs/apple1/_template/`](../sketchs/apple1/_template/) | Minimal asm + C hello world |
| [`sketchs/tms9918/_template_tms9918c/`](../sketchs/tms9918/_template_tms9918c/) | Minimal TMS9918 sprite C starter |
| [`sketchs/gen2/_template_gen2c/`](../sketchs/gen2/_template_gen2c/) | Minimal GEN2 HGR C starter |

Each sketch folder may include a **`.sketch.json`** sidecar with build metadata
DevBench reads at compile time:

- `profile` — `apple1`, `tms9918`, or `gen2`
- `language` — `asm`, `c`, `hex`, or `raw`
- `cfg` — linker config when not the target default
- `extraAsm` — additional `.asm` files from `dev/lib/`
- `defines` — ca65 `-D` symbols passed to the main source **and** every `extraAsm` module, so gated lib code (`.ifdef`) compiles consistently. Used by `tool_logo` (`["CODETANK_BUILD"]`) to build the full CodeTank feature set (on-bitmap text, speech bubbles, buffer editor) for the bench's 8 KB dual-bank + CodeTank profile.
- Dual-bank load (`.lo` @ `$0280` + `.hi` @ `$E000`) is inferred automatically when the linker cfg declares `%O.lo` and `%O.hi` (e.g. HGR Sokoban, text Chess). Single-bank GEN2 sketches use `apple1_gen2.cfg` (`file = %O` @ `$E000` only).
- `asset` / `assetAddr` — companion binary loaded alongside the program

Sketches without a sidecar still work: profile from path, language from extension,
Apple-1 text cfg by default.

**Complex multi-file projects** (several local sources, shared `.inc` files) stay
under [`dev/projects/`](../dev/projects/) and build via `make -C dev/projects`.

---

## `_template` — Apple-1 text (asm + C)

Path: [`sketchs/apple1/_template/`](../sketchs/apple1/_template/)

The smallest useful Apple-1 program. Copy the whole folder, rename, and start
editing.

### Assembly (`Hello.asm`)

```bash
make -C sketchs/apple1/_template    # -> software/Apple-1 demos/Hello.bin
```

By hand (from the sketch folder):

```bash
ca65 -I ../../../dev/lib/apple1 Hello.asm -o Hello.o
ld65 -C ../../../dev/cc65/apple1_4k.cfg Hello.o -o Hello.bin
```

Run in POM1: **File → Load Memory → Hello.bin**, then `280R`.

### C (`hello.c`)

```bash
cl65 -t none -Oirs -C ../../../dev/cc65/apple1_c.cfg -I ../../../dev/lib/apple1c \
     hello.c ../../../dev/lib/apple1c/apple1io.c ../../../dev/lib/apple1c/apple1io_asm.s \
     -o hello.bin
```

Run in POM1: **File → Load Memory → hello.bin**, then `0300R`.

### DevBench shortcut

*DevBench → POM1 Bench*, **New → Apple-1 dual 4K/8K (text)**, then **Upload**.

### Next steps

- ASM → [`Programming_Apple1_ASM.md`](../sketchs/doc/Programming_Apple1_ASM.md),
  playbook [`APPLE1DEV.md`](../sketchs/doc/APPLE1DEV.md)
- C → [`Programming_Apple1_C.md`](../sketchs/doc/Programming_Apple1_C.md)
- Graphics: HGR (`dev/lib/gen2c/`), TMS9918 (`dev/lib/tms9918c/`),
  GT-6144 (`sketchs/apple1/gt6144_demo_hello/`)

### A bigger real-world example — Applesoft Lite

[`sketchs/apple1/applesoft_lite/`](../sketchs/apple1/applesoft_lite/) packages the
full **Applesoft Lite** floating-point BASIC interpreter
([txgx42/applesoft-lite](https://github.com/txgx42/applesoft-lite)) as a
multi-file ASM sketch — its `.sketch.json` lists the linked modules
(`extraAsm`: `io.s`, `cffa1.s`, `wozmon.s`) and a local `applesoft_lite.cfg` that
links the canonical `$E000-$FFFF` ROM. Open `applesoft-lite.s` and **Verify**;
the result is byte-identical to the shipped `roms/applesoft-lite-cffa1.rom`. (This
is the *source* of the interpreter behind the Bench's **BASIC → Applesoft Lite**
runtime, which boots the same code relocated to `$6000`.)

---

## `_template_gen2c` — GEN2 HGR C

Path: [`sketchs/gen2/_template_gen2c/`](../sketchs/gen2/_template_gen2c/)

About 10 lines of `main()`. Copy the folder to start a new colour-graphics project.

```bash
make -C sketchs/gen2/_template_gen2c    # -> main.bin (origin $6000)
```

Run: POM1 preset 11 (GEN2 HGR Color) → Load Memory → `main.bin` → `6000R`.

The Makefile links **only the GEN2 families this program calls** (CORE + TEXT +
RECT). ld65 strips at the `.o` granularity — see `dev/lib/gen2c/gen2c.mk`.
Swap the commented `GEN2C_ALL_SRCS` line in the Makefile when you need the
full runtime.

Shipping sketches under `sketchs/gen2/` that emit Woz-hex into `software/` —
copy [`sketchs/gen2/demo_bounces/`](../sketchs/gen2/demo_bounces/) for that flow.

Further reading: [`Programming_C_Quickstart.md`](../sketchs/doc/Programming_C_Quickstart.md),
[`Programming_GEN2C.md`](../sketchs/doc/Programming_GEN2C.md).

### Applesoft GEN2 — floating-point BASIC with GEN2 graphics

[`sketchs/gen2/applesoft_gen2/`](../sketchs/gen2/applesoft_gen2/) is Applesoft
Lite turned into the BASIC for Uncle Bernie's card: CFFA1 disk I/O dropped, a
full Apple II-style graphics + console command set added — `TEXT GR GR2 HGR HGR2
MIX NOMIX SHOW VBL COLOR= HCOLOR= PLOT HLIN..AT VLIN..AT HPLOT..TO HOME HTAB
VTAB` and the `SCRN(x,y)` function — and **`PRINT` retargeted to the GEN2
screen** (`APRINT` keeps the Apple-1 terminal). **Run** cold-starts the
interpreter at `$6000`; you type BASIC on the Apple-1 terminal and program
output + graphics appear on the GEN2 card. `SHOW n` does tear-free page-flip
double buffering. The README covers the command set, the `PRINT`/`APRINT` output
model, double buffering and the memory map. Pinned by the `applesoft_gen2_smoke`
ctest.

---

## `_template_tms9918c` — TMS9918 sprite C

Path: [`sketchs/tms9918/_template_tms9918c/`](../sketchs/tms9918/_template_tms9918c/)

About 18 lines of `main()`, using the no-tearing **shadow sprite** workflow from
the start.

```bash
make -C sketchs/tms9918/_template_tms9918c
```

Run: POM1 preset 9 (TMS9918 CodeTank) → Load Memory → `main.bin` → `4000R`.

### Why the shadow workflow

`tms_set_sprite()` writes one SAT attribute at a time. If the beam reads mid-write,
sprites tear. The shadow API (`tms_shadow_set` / `tms_shadow_move` /
`tms_shadow_clear` + `tms_shadow_flush()`) updates a 128-byte RAM mirror and
flushes in one burst. Combined with a VBlank wait, it's flicker-free. A beginner
should **never** call `tms_set_sprite()` per frame.

The Makefile links `TMS9918C_CORE_SRCS + SCREEN1_SRCS + SPRITES_SRCS +
APPLE1_SRCS` only — see `dev/lib/tms9918c/tms9918c.mk` for every family.

Shipping sketches that emit Woz-hex into `software/Apple-1_TMS_CC65/` — copy
[`sketchs/tms9918/demo_sprite_animals/`](../sketchs/tms9918/demo_sprite_animals/).

Further reading: [`Programming_C_Quickstart.md`](../sketchs/doc/Programming_C_Quickstart.md),
[`Programming_TMS9918C.md`](../sketchs/doc/Programming_TMS9918C.md),
`dev/lib/tms9918c/sprite_shadow.h`.
