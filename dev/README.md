# dev/ — POM1 Apple-1 software sources

This tree holds the **6502 assembly sources** for every Apple-1 program shipped
with POM1, plus reusable libraries and shared cc65 linker configs. Compiled
artifacts (Woz hex `.txt`, `.bin`, `.rom`) live in `software/<dir>/` — that's
the path POM1's File menu, presets, and autodetect rules use at runtime.

## Layout

    dev/
    ├── lib/            shared 6502 libraries
    │   ├── apple1/     Wozmon ROM + PIA equates (every Apple-1 program)
    │   ├── m6502/      machine-agnostic helpers (math, RNG, decimal print)
    │   ├── tms9918/    P-LAB TMS9918 driver (Mode-2 bitmap)
    │   ├── hgr/        Uncle Bernie's GEN2 HGR data tables
    │   └── sokoban/    Sokoban level data (text / HGR / TMS variants)
    ├── projects/       one folder per buildable program (~28 projects)
    │   └── <name>/     each has README.md, Makefile, .asm, project .cfg, .py
    └── cc65/           shared cc65 linker configs (apple1.cfg, apple1_gen2.cfg, ...)

## Browse from POM1

POM1 → **Dev → Source Browser** opens a read-only view of this tree (left
pane = file list, right pane = file content). The same tree ships in macOS,
Windows, AppImage, and WASM releases.

## Build a project

Most projects come with a Makefile that uses `ca65` (assembler) + `ld65`
(linker) from the [cc65](https://cc65.github.io/) toolchain, then a `python3
emit_*_txt.py` post-step that emits the Woz hex dump POM1 actually loads.

    cd dev/projects/tms9918_logo
    make                    # → software/tms9918/TMS_Logo.{bin,txt}

Each project's README documents which preset to pick in POM1 and the load
address to type at the Wozmon `\` prompt.

## Naming convention

Project folders are `<machine_or_card>_<short_name>` in lower snake_case:
`tms9918_logo`, `hgr_orbital_pool`, `gt6144_life`. The `<machine>_` prefix is
deliberate — it pays off in flat listings and `grep`.

## Library include paths

Per-project Makefiles pass `-I` flags to `ca65` so projects can do
`.include "apple1.inc"` (from `dev/lib/apple1/`) or `.include "math.asm"` (from
`dev/lib/m6502/`) without worrying about relative paths.
