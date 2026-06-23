# dev/ — 6502 libraries, projects & build tree

Source and Makefiles for every Apple-1 program POM1 ships. This is the
developer-facing tree: reference for writing Apple-1 6502 assembly and cc65-C
software. The compiled artefacts land under `software/<dir>/` — that's what
POM1 loads; release bundles ship `dev/` (cfgs + runtime libs) next to the cc65
toolchain so the in-app DevBench can rebuild from here.

## Map

| Path | What |
|---|---|
| [`lib/`](lib/README.md) | Shared 6502 libraries — the hub. Per-card asm + cc65-C tracks (`apple1`, `gen2`, `tms9918`, `sid`, …), the card-neutral `gfx/` layer, the cross-library zero-page contract. **Start here.** |
| [`cc65/`](cc65/README.md) | Linker `.cfg` files, the shared `Makefile.common` fragment, and the Woz-hex emit scripts. The build layer. |
| `projects/` | Multi-file, multi-target builds that don't fit the one-source DevBench sketch model. CI gate: `make -C dev/projects`. Currently: [`codetank/`](projects/codetank/README.md) (P-LAB CodeTank cartridge launcher menus). |
| [`TODO6502.md`](TODO6502.md) | Open work on the 6502-software side (new programs, lib showcases, perf, tooling). |

## Build flow

Each project carries a tiny Makefile that sets `PROJECT` / `LOAD_CFG` /
`OUT_DIR` / `LIB` and `include`s [`cc65/Makefile.common`](cc65/README.md):
`ca65` assembles, `ld65` links against a `.cfg` from `cc65/`, and an optional
`emit_*` step writes a Woz-hex `.txt` alongside the `.bin` under
`software/<dir>/`. `make -C dev/projects` builds every project (release/CI gate);
`make -C dev/lib check` validates the libraries themselves. See
[`cc65/README.md`](cc65/README.md) for the linker configs and the standard
project Makefile shape.

## Library organisation, in one breath

The tree under [`lib/`](lib/README.md) is crossed on two axes: by **card /
peripheral** (one subdirectory each) and by **language track** — an assembly
track and a cc65 **C** track for the same card (`gen2`/`gen2c`,
`tms9918`/`tms9918c`, `apple1`/`apple1c`). A program picks ONE card and ONE
language and links only that. Within a track, a file reaches your program either
as a textual `.include` or as a separately-compiled `.o`/archive that `ld65`
dead-strips when unused. [`lib/README.md`](lib/README.md) owns the full
rationale for both splits — read it before consuming anything.

Single-source DevBench **sketches** (one `.asm` or `.c` per program, edited and
built inside the in-app Bench) live outside `dev/`, under the per-profile
`sketchs/` tree; their starters are the `_template*` folders there. Projects
under `dev/projects/` are the multi-file counterpart.
