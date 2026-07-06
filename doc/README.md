# POM1 documentation map

The single index to every prose doc in the repo — start here when you (human or
AI) need to find the right document. Grouped by purpose. File paths are relative
to the repo root.

> **Two TODO/CHANGELOG axes.** Emulator (C++) work: `TODO.md` (open) →
> `CHANGELOG.md` (shipped). 6502-software (`dev/`) work: [`dev/TODO6502.md`](../dev/TODO6502.md) (open)
> → `CHANGELOG.md` (shipped). `git log` is the authoritative history.

## Start here

| Doc | For | What |
|---|---|---|
| [`README.md`](../README.md) | users | Feature tour, the 13 presets (3 DevBench + 10 machines), software library, per-card hardware reference. |
| [`QUICKSTART.md`](../QUICKSTART.md) | new users | Your first Apple-1 program in 5 minutes (BASIC → Wozmon → the Bench). |
| [`CLAUDE.md`](../CLAUDE.md) | AI / contributors | **Emulator-side architecture, invariants, gotchas.** The auto-loaded entry point: memory map, MMIO, peripheral bus, mutex order, presets, testing. |
| [`CHANGELOG.md`](../CHANGELOG.md) | everyone | Shipped work — emulator (from `TODO.md`) + 6502 software (from [`dev/TODO6502.md`](../dev/TODO6502.md)). |
| [`TODO.md`](../TODO.md) | contributors | Open **emulator** work, grouped by subsystem. |

## Writing 6502 software (`sketchs/doc/` + `dev/` tree)

Guides live in [`sketchs/doc/`](../sketchs/doc/). Source, libraries and build configs stay under [`dev/`](../dev/).

| [`sketchs/doc/README.md`](../sketchs/doc/README.md) | Index of 6502 developer guides in this tree. |

| Doc | What |
|---|---|
| [`sketchs/doc/APPLE1DEV.md`](../sketchs/doc/APPLE1DEV.md) | **Agent playbook** — decision tree (which card/language), I/O cheat sheet, deployment, gotchas, example index. Read this first for `dev/` work. |
| [`sketchs/doc/Programming_C_Quickstart.md`](../sketchs/doc/Programming_C_Quickstart.md) | **C beginner cheat sheet** — 30-second decision, 3 side-by-side hello-worlds, function-chooser per library, top-10 pitfalls. Read this first for cc65 work. |
| [`sketchs/doc/Programming_Apple1_ASM.md`](../sketchs/doc/Programming_Apple1_ASM.md) | Detailed ASM guide: 6502, cc65, text, HGR, TMS9918 (Sokoban + Connect 4 trilogies). |
| [`sketchs/doc/Programming_Apple1_C.md`](../sketchs/doc/Programming_Apple1_C.md) | C guide (cc65): the shared Apple-1 text base. Card layers split into the four files below. |
| [`sketchs/doc/Programming_GEN2.md`](../sketchs/doc/Programming_GEN2.md) | GEN2 HGR colour graphics in 6502 assembly. |
| [`sketchs/doc/Programming_GEN2C.md`](../sketchs/doc/Programming_GEN2C.md) | GEN2 HGR colour graphics in C (`gen2c` runtime). |
| [`sketchs/doc/Programming_TMS9918.md`](../sketchs/doc/Programming_TMS9918.md) | **TMS9918 vs real silicon** — strict VRAM timing, sprite quirks, full ASM programming guide. *Mandatory before optimising any VRAM loop.* Referenced from ~10 source files. |
| [`sketchs/doc/Programming_TMS9918C.md`](../sketchs/doc/Programming_TMS9918C.md) | TMS9918 C runtime (`tms9918c`, nippur72 port) — the C-side surface plus the silicon-handling that leaks through. |
| [`dev/TODO6502.md`](../dev/TODO6502.md) | Open **6502-software** work / `dev/projects` backlog. |
| [`sketchs/doc/CC65.md`](../sketchs/doc/CC65.md) | cc65 linker configs, `Makefile.common`, emit scripts. |
| [`dev/lib/README.md`](../dev/lib/README.md) | **6502 library root** — the two integration models (textual `.include` vs separately-compiled `.o`/archive), the **"two tracks per card" decision record** (why asm+C duplication is chosen, not debt), directory map, ZP convention, validation gate. Read before consuming any lib. |
| `dev/lib/*/README.md` | Per-library docs — `apple1` (equates), `m6502` (math), `tms9918`/`tms9918c`, `gen2`/`gen2c`, `gfx` (shared geometry/numbers), `sid`, `sd`, `gt6144`, `a1io`, `wifi`, `games/*`, `text40`, `apple1c`, `telemetry`. Each asm/C pair names its **source of truth** for shared equates/fonts. |
| [`dev/lib/Makefile`](../dev/lib/Makefile) | **Library self-validation gate** — `make -C dev/lib check`: asm↔C hardware-equate drift (`tools/check_lib_equates.py`), font-master drift (`tools/build_shared_font.py --check`), `zp.inc` `$00-$07` layout pin, and a compile of every C/asm source **decoupled from its consumers**. Companion to `make -C dev/projects`; both must be green to ship. |
| `sketchs/<profile>/*/README.md` | Per-sketch notes (starters under `_template*`). |
| `dev/projects/*/README.md` | Per-program docs for complex multi-file projects under `dev/projects/`. Layout + sidecars → [`doc/SKETCHS.md`](SKETCHS.md). |

## Emulator & card reference (`doc/`)

| Doc | What |
|---|---|
| [`CLI.md`](CLI.md) | **Full CLI flag table** (headless / scripted runs). Implementation: `CliDispatcher.cpp`. |
| [`DEVBENCH.md`](DEVBENCH.md) | POM1 Bench (in-app cc65/Wozmon IDE) — the language×machine target matrix; how release packages bundle cc65. |
| [`BASIC_COMPILER.md`](BASIC_COMPILER.md) | **Applesoft Lite → 6502 image compiler** (`src/BasicTokeniserApplesoft.*`, `basicc` tool) — tokenize-and-launch an `.apf` ahead of time so it loads + runs with no keyboard injection (GEN2 / TMS9918). |
| [`SKETCHS.md`](SKETCHS.md) | `sketchs/` folder layout, `.sketch.json` sidecars, copy-me `_template*` starters. |
| [`CC65_WASM.md`](CC65_WASM.md) | Running the cc65 toolchain in the browser (WASM build) — architecture + status. |
| [`GEN2_RELEASE.md`](GEN2_RELEASE.md) | Uncle Bernie GEN2 colour card developer guide ("Bernie SDK") — `$C25x` switches, HST0, porting. |
| [`GEN2_RELEASE_questions.md`](GEN2_RELEASE_questions.md) | GEN2 hardware spec Q&A (Q1–Q10, from Bernie's PDF). Referenced from `Gen2VideoScanner` / `GraphicsCard`. |
| [`HGR_SPRITES_X1_X2.md`](HGR_SPRITES_X1_X2.md) | **Design spec (FR) — sprites HGR ×1 (artefact, bit 7 palette sélectionnable) / ×2 (couleur choisie).** Formats `.asm`/`.inc`, contrat de parité, ABI runtime `inflate_x2`/`blit_x2`, export éditeur. Réf : `src/hgrsprite/`, `dev/lib/gen2c/`, `tools/build_hgr_sprites.py`. |
| [`sketchs/doc/HGR-SPRITE_BEST_PRACTICES.md`](../sketchs/doc/HGR-SPRITE_BEST_PRACTICES.md) | **Guide pratique (FR) — animer des sprites HGR.** Compagnon runtime de `HGR_SPRITES_X1_X2.md` : mouvement 2 px lisse (pré-shifts), rendu incrémental double-buffer, trim RAM par phase, LUTs (0 mul/div/frame), modèle de coût & cap 60 fps, pièges cc65 `-Oirs`/DevBench, taxonomie des renderers. Projet : `sketchs/gen2/demo_sprite_animals/`. |
| [`TELEMETRY_SIDE_CHANNEL.md`](TELEMETRY_SIDE_CHANNEL.md) | The `$C440-$C443` automated-testing bridge (protocol, lock-step, CLI). Referenced from `TelemetryPort`. |
| [`TMS9918_TRANSFER_WINDOWS.md`](TMS9918_TRANSFER_WINDOWS.md) | **TMS9918 CPU→VRAM transmission zones** — the 5 access windows, how `TMS9918::transmissionZone()` detects them, per-zone minimum write spacing, and the zone-dependent drop model. Referenced from `TMS9918.cpp` + `tms9918m1/m2.asm`. |
| [`sketchs/doc/TMS9918-SPRITE_INIT.md`](../sketchs/doc/TMS9918-SPRITE_INIT.md) | Canonical TMS9918 sprite-init semantics. Referenced from `TMS9918.cpp`. |
| [`sketchs/doc/TMS9918-SPRITE_BEST_PRACTICES.md`](../sketchs/doc/TMS9918-SPRITE_BEST_PRACTICES.md) | Operational sprite checklist (complements SPRITE_INIT; timing → [`Programming_TMS9918.md`](../sketchs/doc/Programming_TMS9918.md)). |
| [`TMS9918_Sprite_Emulation_FR.md`](TMS9918_Sprite_Emulation_FR.md) | French deep-dive: TMS9918 sprite-emulation problems & references. |
| [`SWTPC_GT-6144.md`](SWTPC_GT-6144.md) · [`SWTPC_PR-40.md`](SWTPC_PR-40.md) | Research on the two 1976 SWTPC cards POM1 emulates. |
| [`Apple1_Peripherals_Inventory_FR.md`](Apple1_Peripherals_Inventory_FR.md) | French deep-dive: complete Apple-I peripheral inventory. |
| [`Apple1_Software_Catalog_FR.md`](Apple1_Software_Catalog_FR.md) | French deep-dive: Apple-1 software catalog for the emulator. |

## Reference assets (binary — not prose)

All **non-prose reference material** lives under [`doc/reference/`](reference/) — ignore it
for code understanding (the `doc/` top level is prose docs only). It holds the original
hardware manuals (PDFs — `doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf` is the GEN2
spec source, plus Apple-1 Graphic Card / SID / microSD / CFFA1 / Cassette / Jukebox /
Preliminary Apple BASIC / Terrapin & Turtle Geometry Logo), photos & screenshots
(`*.jpg` / `*.png`), media (`*.mp4`), software archives (`*.zip`), and original-software
readmes / game instructions (`*.txt`). The `doc/JUKEBOX_ROM_CREATOR/` toolkit (ROM-packing
scripts + its own manuals) stays in place.
