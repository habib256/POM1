# POM1 documentation map

The single index to every prose doc in the repo — start here when you (human or
AI) need to find the right document. Grouped by purpose. File paths are relative
to the repo root.

> **Two TODO/CHANGELOG axes.** Emulator (C++) work: `TODO.md` (open) →
> `CHANGELOG.md` (shipped). 6502-software (`dev/`) work: `dev/TODO6502.md` (open)
> → `CHANGELOG.md` (shipped). `git log` is the authoritative history.

## Start here

| Doc | For | What |
|---|---|---|
| [`README.md`](../README.md) | users | Feature tour, the 14 machine presets, software library, per-card hardware reference. |
| [`QUICKSTART.md`](../QUICKSTART.md) | new users | Your first Apple-1 program in 5 minutes (BASIC → Wozmon → the Bench). |
| [`CLAUDE.md`](../CLAUDE.md) | AI / contributors | **Emulator-side architecture, invariants, gotchas.** The auto-loaded entry point: memory map, MMIO, peripheral bus, mutex order, presets, testing. |
| [`CHANGELOG.md`](../CHANGELOG.md) | everyone | Shipped work — emulator (from `TODO.md`) + 6502 software (from `dev/TODO6502.md`). |
| [`TODO.md`](../TODO.md) | contributors | Open **emulator** work, grouped by subsystem. |

## Writing 6502 software (`dev/`)

What runs *inside* the emulated Apple 1 — the libraries and `dev/projects/` programs.

| Doc | What |
|---|---|
| [`dev/APPLE1DEV.md`](../dev/APPLE1DEV.md) | **Agent playbook** — decision tree (which card/language), I/O cheat sheet, deployment, gotchas, example index. Read this first for `dev/` work. |
| [`dev/Programming_Apple1_ASM.md`](../dev/Programming_Apple1_ASM.md) | Detailed ASM guide (FR): 6502, cc65, text, HGR, TMS9918 (Sokoban + Connect 4 trilogies). |
| [`dev/Programming_Apple1_C.md`](../dev/Programming_Apple1_C.md) | C guide (cc65): the shared Apple-1 text base + GEN2 HGR / TMS9918 graphics layers. |
| [`dev/SILICONBUGS.md`](../dev/SILICONBUGS.md) | **TMS9918 vs real silicon** — strict VRAM timing, sprite quirks. *Mandatory before optimising any VRAM loop.* Referenced from ~10 source files. |
| [`dev/TODO6502.md`](../dev/TODO6502.md) | Open **6502-software** work / `dev/projects` backlog. |
| `dev/lib/*/README.md` | Per-library docs — `apple1` (equates), `m6502` (math), `tms9918`, `hgr`, `gen2`/`gen2c`, `gfx` (shared geometry/numbers), `sid`, `sd`, `gt6144`, `a1io`, `wifi`, `games/*`, `text40`, `apple1c`, `telemetry`. |
| `dev/projects/*/README.md` | Per-program docs — ~50 ready-to-run programs (one folder each; `dev/projects/_template/` is the starting point). |

## Emulator & card reference (`doc/`)

| Doc | What |
|---|---|
| [`CLI.md`](CLI.md) | **Full CLI flag table** (headless / scripted runs). Implementation: `CliDispatcher.cpp`. |
| [`DEVBENCH.md`](DEVBENCH.md) | POM1 Bench (in-app cc65/Wozmon IDE) — the language×machine target matrix. |
| [`CC65_WASM.md`](CC65_WASM.md) | Running the cc65 toolchain in the browser (WASM build) — architecture + status. |
| [`GEN2_RELEASE.md`](GEN2_RELEASE.md) | Uncle Bernie GEN2 colour card developer guide ("Bernie SDK") — `$C25x` switches, HST0, porting. |
| [`GEN2_RELEASE_questions.md`](GEN2_RELEASE_questions.md) | GEN2 hardware spec Q&A (Q1–Q10, from Bernie's PDF). Referenced from `Gen2VideoScanner` / `GraphicsCard`. |
| [`TELEMETRY_SIDE_CHANNEL.md`](TELEMETRY_SIDE_CHANNEL.md) | The `$C440-$C443` automated-testing bridge (protocol, lock-step, CLI). Referenced from `TelemetryPort`. |
| [`TMS9918-SPRITE_INIT.md`](TMS9918-SPRITE_INIT.md) | Canonical TMS9918 sprite-init semantics. Referenced from `TMS9918.cpp`. |
| [`TMS9918-SPRITE_BEST_PRACTICES.md`](TMS9918-SPRITE_BEST_PRACTICES.md) | Operational sprite checklist (complements SPRITE_INIT; timing → `SILICONBUGS.md`). |
| [`TMS9918_Sprite_Emulation_FR.md`](TMS9918_Sprite_Emulation_FR.md) | French deep-dive: TMS9918 sprite-emulation problems & references. |
| [`SWTPC_GT-6144.md`](SWTPC_GT-6144.md) · [`SWTPC_PR-40.md`](SWTPC_PR-40.md) | Research on the two 1976 SWTPC cards POM1 emulates. |
| [`Apple1_Peripherals_Inventory_FR.md`](Apple1_Peripherals_Inventory_FR.md) | French deep-dive: complete Apple-I peripheral inventory. |
| [`Apple1_Software_Catalog_FR.md`](Apple1_Software_Catalog_FR.md) | French deep-dive: Apple-1 software catalog for the emulator. |
| [`AUDIT.md`](AUDIT.md) | Historical C++ audit (2026-05-29). The 2 *Medium* findings (JSR/RTS cycles) were since fixed by `cpu_harte_smoke`; the 2 *Low* ones are tracked in `TODO.md`. |

## Reference assets (binary — not prose)

All **non-prose reference material** lives under [`doc/reference/`](reference/) — ignore it
for code understanding (the `doc/` top level is prose docs only). It holds the original
hardware manuals (PDFs — `doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf` is the GEN2
spec source, plus Apple-1 Graphic Card / SID / microSD / CFFA1 / Cassette / Jukebox /
Preliminary Apple BASIC / Terrapin & Turtle Geometry Logo), photos & screenshots
(`*.jpg` / `*.png`), media (`*.mp4`), software archives (`*.zip`), and original-software
readmes / game instructions (`*.txt`). The `doc/JUKEBOX_ROM_CREATOR/` toolkit (ROM-packing
scripts + its own manuals) stays in place.
