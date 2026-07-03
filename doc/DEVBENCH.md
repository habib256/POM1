# POM1 DevBench — target reference

The **POM1 Bench** (menu *DevBench → POM1 Bench (sketch editor)…*) is an in-app,
Arduino-style code editor: write 6502 **assembly** or **C**, **Verify** (compile)
or **Upload** (compile **and** run) without leaving the window. Each *New* sketch
drops in a working `HELLO WORLD` starter for the chosen target.

Desktop shells out to the **cc65** toolchain (`ca65`/`ld65`/`cl65`). The WebAssembly
build uses the bundled cc65 WASM tools, so the same asm/C starter targets compile
in-browser. **Official release packages (Windows ZIP / macOS `.dmg` / Linux AppImage)
ship a bundled cc65 next to POM1** — both asm (`ca65`/`ld65`) and C (`cl65`/`cc65`)
work out of the box with nothing to install. POM1 finds it exe-relative and points
`CC65_HOME` at the bundled runtime (see `ensureCc65Home` in `Pom1BenchHost.cpp`).
Only a **git-checkout / source build** needs a system cc65: `sudo apt install cc65`
(Debian/Ubuntu) · `brew install cc65` (macOS) · `pacman -S cc65` (Arch) ·
<https://cc65.github.io/>. The *New* dialog shows **green** (ready) or **orange**
(needs cc65/dev files) per target.

New to this? Start with [`QUICKSTART.md`](../QUICKSTART.md) §3.

---

## Workflow

1. **DevBench → POM1 Bench**. Click **New** (file icon).
2. Pick a **Language** (Assembly · `ca65/ld65`, or C · `cc65/cl65`) × a **Machine**.
3. Edit the starter, then:
   - **Verify** (✓ pill) — assemble/compile only; errors land in the gutter +
     console (click an error line to jump to it). A plain-language hint is
     prepended for common cc65 diagnostics.
   - **Run** (▶ pill) — build **and run**. POM1 switches to the target's machine
     preset, loads the program and starts it; **you don't type a run command**.
     The status line points you at the window where the output appears (the
     TMS9918 / GEN2 window opens automatically for graphics targets).

Picking a target applies its **machine preset** (cards + RAM), so Run always uses
the right hardware. The tab shows a **`*`** while the sketch has unsaved edits;
the **Save** (floppy-disk) toolbar button writes to the open file, or opens the
Save dialog for a new sketch.

---

## Target Matrix

The *New* dialog is a 2 × 3 grid. Each cell = one target with its own linker
config, libraries and run mechanism:

| Language | Machine | Preset | Linker cfg | Runs from | (re-)run in Wozmon |
|---|---|---|---|---|---|
| **asm** | Apple-1 dual 4K/8K (text) | 0 | `apple1_4k.cfg` | RAM `$0280` | `280R` |
| **asm** | P-LAB TMS9918 | 1 | `codetank.cfg` | **CODETANKDEV.rom** (ROM `$4000`) | `4000R` |
| **asm** | Uncle Bernie GEN2 HGR | 2 | `apple1_gen2.cfg` | RAM `$E000` (HGR fb `$2000-$3FFF`) | `E000R` |
| **C** | Apple-1 dual 4K/8K (text) | 0 | `apple1_c.cfg` | RAM `$0300` | `0300R` |
| **C** | P-LAB TMS9918 | 1 | `codetank_c.cfg` | **CODETANKDEV.rom** (ROM `$4000`) | `4000R` |
| **C** | Uncle Bernie GEN2 HGR | 2 | `apple1_gen2_c.cfg` | RAM `$6000` (HGR fb `$2000-$3FFF`) | `6000R` |

**Apple-1 dual 4K/8K (text) is the place to start** — no graphics card, output via
the WOZ Monitor.

### Libraries Each Target Links

- **asm** (all three): `ca65` sees `-I` for every directory under `dev/lib/`, so any
  `.include "apple1.inc"` / `hgr_tables.inc` / `tms9918.inc` / `gen2.inc` … just
  works; `ld65` links with the cfg above.
- **C / Apple-1 text**: the shared **`dev/lib/apple1c/`** base
  (`apple1io.h` — `woz_puts` / `apple1_getkey` / `woz_mon`).
- **C / GEN2 HGR**: **`dev/lib/gen2c/`** (`gen2.h` — `gen2_hgr_*`) **plus** the
  shared `apple1c` base, so a GEN2 C program can draw HIRES *and* print to the
  terminal / read the keyboard.
- **C / TMS9918**: the TMS9918 C runtime under **`dev/lib/tms9918c/`**
  (`screen1.h` / `tms9918.h`).

### Per-target C build specs — `dev/bench/*.json`

What each C target actually compiles/links is **data, not code**: one JSON spec
per target under **`dev/bench/`** (`apple1c.json`, `gen2c.json`, `tms9918c.json`),
loaded at build time by `Pom1BenchHost`. This is the **editable source of
truth** — add a runtime module to `dev/lib/…`, list it in the matching spec, and
the Bench picks it up on the next Run with **no emulator rebuild**.

Schema (all paths in MEMFS-style `/dev/…` form):

```json
{
  "cfg": "/dev/lib/tms9918c/cc65/codetank_c.cfg",
  "defines": ["POM1_GFX_TMS"],
  "incDirs": ["/dev/lib/tms9918c", "/dev/lib/gfx", "/dev/lib/telemetry"],
  "cSources":   [ { "path": "/dev/lib/tms9918c/apple1.c", "name": "apple1.c" }, … ],
  "asmSources": [ { "path": "/dev/lib/tms9918c/apple1_asm.s", "name": "apple1_asm.s" }, … ]
}
```

- **Desktop** derives the full `cl65` command line from the parsed spec
  (`benchCSpecCl65Cmd`), mapping `/dev/…` onto the resolved `dev/` tree.
- **WASM** forwards the raw JSON text to `window.POM1cc65.buildC` (the specs are
  MEMFS-preloaded with the rest of `dev/`); per-sketch `EXTRA_ASM` modules are
  folded into `asmSources` before hand-off.
- **Fallback:** if `dev/bench/<target>.json` is missing or unparsable (old
  bundle layouts), a byte-identical compiled-in copy (`kBenchCSpec*` in
  `Pom1BenchHost.cpp`) is used and a one-line notice goes to stderr — packaged
  builds never break. **Keep the embedded copies in sync when editing a spec.**
  Release packagers ship `dev/bench/` next to `dev/cc65` + `dev/lib`.

### Starter files & sketch layout

The built-in *New* dialog embeds default starters in `src/Pom1BenchHost.cpp`.
Editable copies, sidecar metadata, and copy-me templates → [`SKETCHS.md`](SKETCHS.md).

### File extension → action

The **file extension drives what Verify/Run does**, and it is re-evaluated every
time you switch tabs (the status-bar mode and the toolbar follow the front tab):

| Extension | Action |
|---|---|
| `.s` / `.asm` | assemble (ca65 / ld65) |
| `.c` | compile (cc65 / cl65) |
| `.hex` / `.txt` | load Woz-Monitor hex |
| `.apf` | **Applesoft** BASIC — interpreter follows the path (see below). All four targets **compile** (tokeniser) |
| `.bas` / `.ibas` | **Integer** BASIC ($E000) — **compile** (tokeniser); cold-start + image @ `pp` + RUN ($EFEC) |
| `.logo` | **APPLE-1 LOGO** — **inject** (`LogoProgramLoader` pokes the proc table + feeds one call line); GEN2/HGR path → GEN2, else TMS9918 |
| `.md` / `.markdown` | render as a document (Edit/Preview toggle) — see below |
| anything else | **do nothing** (Verify/Run report "nothing to build") |

The machine follows the path: `sketchs/tms9918…` → TMS9918, `sketchs/gen2…` →
GEN2, otherwise Apple-1 text. An `.apf` in a TMS9918 path tokenises into
**Applesoft TMS9918**, in a GEN2/HGR path into **Applesoft GEN2**, elsewhere into
the stock microSD Applesoft. A `.bas`/`.ibas` always tokenises into **Integer BASIC**.

### BASIC — four Applesoft machines

*New* → language **BASIC** offers four machines, each cold-starting the matching
in-ROM interpreter. **All four now COMPILE** the listing with the host-side
tokeniser (`BasicTokeniserApplesoft`): the program is tokenised ahead of time into a `$0801`
image + a `$0280` launcher and loaded directly, then the launcher is entered — no
per-character keyboard typing, no 127-char line cap, instant, and identical on
WASM (the tokeniser is pure C++). The resident interpreter ROM still supplies the
runtime (FP, `SIN`/`SQR`, `HPLOT`…). Two reserved-word tables are used: the GEN2/
TMS9918 graphics dialect (HGR/HPLOT/COLOR=…) and the reduced Applesoft Lite dialect
for the microSD/CFFA1 ROMs (no graphics/trig; MENU/SAVE/LOAD/CLS — token bytes
diverge past `$98`). (For *native* 6502 codegen — no interpreter at runtime — see
[`BASIC_COMPILER.md`](BASIC_COMPILER.md).) **Integer BASIC** (`.bas`/`.ibas`) also
tokenises now — its own context-sensitive tokeniser (`BasicTokeniserInteger`,
`namespace ibasic`): program stored down from HIMEM, cold-start then image @ `pp` +
RUN ($EFEC). Source: `sketchs/apple1/integer_basic/integer-basic.s` (== `roms/basic.rom`).

| Machine | Interpreter | Boot | Deploy |
|---|---|---|---|
| Applesoft Lite (Apple-1) | `roms/applesoft-lite-cffa1.rom` (`$E000`) | `E000R` | **compile (tokeniser, Lite)** |
| Applesoft Lite + microSD | `roms/applesoft-lite-microsd.rom` (`$6000`) | `6000R` | **compile (tokeniser, Lite)** |
| Applesoft GEN2 HGR | `roms/applesoft-gen2.rom` (`$9800`, GEN2 card) | `9800R` | **compile (tokeniser, graphics)** |
| Applesoft TMS9918 | `roms/codetank/CODETANKDEV.rom` upper bank (CodeTank `$4000`, jumper Upper) | `4000R` | **compile (tokeniser, graphics)** |

The graphics variants (GEN2/TMS9918) add the Apple II graphics command set
(`TEXT/GR/HGR/COLOR=/HCOLOR=/PLOT/HLIN/VLIN/HPLOT`, `PRINT` → the card's screen,
`APRINT` → the Apple-1 terminal). The BASIC editor **hides the gutter line
numbers** — the program's own line numbers (10, 20, …) are what count.

### LOGO — two turtle machines

*New* → language **LOGO** offers two machines, each cold-starting the resident
**APPLE-1 LOGO V2.6** turtle interpreter (`sketchs/tms9918/tool_logo/TMS_Logo_16k.asm`;
manual → [`APPLE-1_LOGO-2.6-MANUAL.md`](../sketchs/tms9918/tool_logo/APPLE-1_LOGO-2.6-MANUAL.md)).

| Machine | Interpreter | Boot | Deploy |
|---|---|---|---|
| LOGO TMS9918 | `roms/codetank/Codetank_GAME3.rom` **lower** bank (CodeTank `$4000`, jumper Lower) | `4000R` | **inject** (proc table + entry line) |
| LOGO GEN2 HGR | `roms/logo-gen2.rom` (`$6000`, GEN2 card) | `6000R` | **inject** (proc table + entry line) |

Unlike Applesoft there is **no tokenised memory image**: a LOGO procedure is stored
as its **raw ASCII source** in the interpreter's `proc_table` (the interpreter
re-parses each body line at run time). So the LOGO path **injects** rather than
compiles — `LogoProgramLoader` (pure C++, WASM-safe) parses the listing into:

- **procedures** (`TO NAME :p1 :p2` … `END`) → poked straight into `proc_table`
  (244-byte slots: 6-byte space-padded name, `n_params`, 2 param names, `body_len`,
  ≤224-byte CR-separated body) with `n_procs` set to the count, and
- one **entry line** — a single immediate call (e.g. `MAIN`, or the sole immediate
  command; several immediate lines are wrapped into a synthesized driver procedure).

On **Run**, `injectLogo` cold-starts the interpreter to its `?` prompt (so `main`
inits the turtle + zeroes `n_procs`), pokes the proc table while the CPU is parked
(`writeMemoryBatch`), queues **only the entry line**, and resumes the REPL. Feeding
one short line is deliberate: LOGO's `REPEAT` break-poll would eat queued type-ahead,
so a *pasted* multi-line program drops every line after the first `REPEAT` — but
procedure **bodies never travel the keyboard** here, so that never bites. **Verify**
parses the listing host-side and reports the procedure/entry summary without touching
the machine.

Addresses (fixed by the LOGO RAM linker cfg, stable across CODE placement, pinned by
`bench_logo_inject_smoke`): `proc_table` `$E431` (TMS) / `$B431` (GEN2), `n_procs`
`$0260` / `$02E3`. **The TMS machine keeps its 8 KB Parmigiani dual-bank** — PROCBSS
lives in the `$E000-$EFFF` high bank, so (unlike the Applesoft TMS path) it must
*not* be relaxed to a linear 16 KB view, which would unback `$E000`.

Ready-to-run, **machine-neutral** `.logo` programs live in
[`sketchs/logo/`](../sketchs/logo/) (rosette, polygons, stars, recursive flower/tree,
random rays/meadow) — the LOGO counterpart of `sketchs/basic_applesoft/`. Turns are
`TR`/`TL` (**not** `RT`/`LT`); nested `REPEAT` is one level deep.

**Interactive REPL.** Because the interpreter stays resident at its `?` prompt after
Run, a one-line **REPL input** appears below the build console (only for LOGO targets).
Type a procedure call, a turtle command (`FD 50`), or even a `TO … END` definition and
press Enter — the line is fed to the live interpreter (one line at a time, Up/Down
recalls history) and echoed into the console as a record of what you sent. The turtle
draws on the graphics-card window and the interpreter's own text output — the `?`
prompt, `OK`, `PRINT`, error messages — shows on the **Apple-1 screen window** (that's
its job). It lets you drive and extend a running program without a re-cold-start.

### Markdown documents

Opening a `.md` shows a formatted **Preview** (toggle to **Edit** to see the
source). Links `[text](file.md)` are **clickable** — a link to another local file
that exists (resolved relative to the document) opens it in a new tab; external
`http(s)://` links are copied to the clipboard.

ASM guide → [`Programming_Apple1_ASM.md`](../sketchs/doc/Programming_Apple1_ASM.md) ·
C guide → [`Programming_Apple1_C.md`](../sketchs/doc/Programming_Apple1_C.md).

---

## How each machine runs

### Apple-1 text (asm `$0280` / C `$0300`)
Plain 40×24 terminal. The asm starter prints with `JSR ECHO`; the C starter with
`woz_puts`. Loaded into RAM and run in place — the simplest target.

### TMS9918 — everything runs from a **CODETANKDEV** cartridge
All TMS9918 code (asm **and** C) is wrapped into a **persistent CodeTank dev ROM**
`roms/codetank/CODETANKDEV.rom`, flashed, the jumper set to the lower 16 KB bank,
and booted at **`4000R`** — TMS9918 software always runs from a CodeTank cartridge
on the real card, and the Bench mirrors that. Because the dev ROM lives under
`roms/codetank/`, it also appears in **File → P-LAB CodeTank Library**, so you can
re-select your last build any time. (The asm target builds with `codetank.cfg` at
`$4000`; the C target builds a 16 KB ROM image.) The file is git-ignored —
it's regenerated on every TMS9918 Upload.

### GEN2 HGR (asm `$E000` / C `$6000`)
Uncle Bernie's 280×192 HIRES card. `apple1_gen2*.cfg` reserves `$2000-$3FFF` for
the framebuffer, so program code sits above it. The asm starter draws text with
the Beautiful Boot font via `plot_pixel`; the C starter uses `gen2_hgr_puts` /
`gen2_hgr_putu`. Card reference → [`GEN2_RELEASE.md`](GEN2_RELEASE.md).

**ACI speaker / chiptune demos** (*A-1-CrazyCycle*, any program toggling `$C030`):
`WOZ_talk.mp3` is inserted only on the **POM1 Fantasy** preset; in audio-stream mode it
blocks live ACI TAPE OUT pulses. DevBench **Run** on GEN2 auto-ejects before load.

## Toolchain-free quick target (hex)

One target needs **no compiler** — handy when cc65 isn't installed (the Bench
defaults to **Wozmon hex** when it can't find cc65):

- **Wozmon hex** — paste a Woz-Monitor hex dump (`AAAA: BB BB …`, optional trailing
  `xxxxR`); Upload loads it (and auto-runs if the `R` line is present). No build step.

---

## Tips

- **Examples** (book icon) — a graduated set on the Apple-1 text target (print a
  character → a string → a loop → read the keyboard, then the same in C), plus
  larger demos (GEN2 HGR *A-1-CrazyCycle*, the Telemetry SDK demo). Each example
  selects its own machine.
- **Toolchain status** (🛠 icon) — a popup showing what the cc65 probe found
  (ca65/ld65/cl65 paths, whether `dev/` was located, per-target readiness) — your
  first stop when a target is greyed/orange.
- **Serial Monitor** (magnifier) opens the Telemetry side channel.
- **Stop** (■) halts the emulated CPU.
- **Build errors**: the console echoes the raw `ca65`/`ld65`/`cl65` output; a
  one-line `[bench] Hint:` is prepended for the common ones (undefined symbol,
  range error, segment overflow, unknown identifier).
- **Out of room?** The Apple-1 text C target (`apple1_c.cfg`) gives C only
  ~2.75 KB (`$0300-$0FFF`); a `ld65: Range error` there means switch to a roomier
  target (GEN2 = ~24 KB) — see [`Programming_Apple1_C.md`](../sketchs/doc/Programming_Apple1_C.md) §7.

The Bench module is portable (`bench/`); POM1 wires the targets above in
`src/Pom1BenchHost.cpp` (`kP1Targets[]`).
