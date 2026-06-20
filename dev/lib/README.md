# dev/lib — POM1 6502 software libraries

*[← POM1 documentation index](../../doc/README.md)*

Reusable 6502 code shared by the Apple-1 programs POM1 ships (`sketchs/<profile>/`
DevBench sketches and `dev/projects/<card>/` multi-file builds). The compiled
artefacts land under `software/<dir>/` — that's what POM1 loads; release bundles
omit `dev/`.

The tree is organised on **two crossed axes**: by **target** (one subdirectory
per card / peripheral) and by **language track** (assembly `.include`-style libs
and cc65 **C** runtimes for the same card). A program picks ONE card and ONE
language and links only that.

## The two integration models

The single most important thing to know before consuming anything here: a file
reaches your program in one of **two** ways, and they are not interchangeable.
The split exists because ROM space is tight and `ld65` only dead-strips at object
granularity — so code you want trimmed-when-unused must be a separate object,
while tiny helpers and pure data are cheaper pasted inline.

### Model A — textual `.include` (no separate compilation)

The file is `.include`d into your program's **single** translation unit, as if
pasted at that point. Nothing is dead-stripped — you get all of it — so this fits
small routines, equates and data.

- **All `.inc` files** — pure equates / macros / data tables, always textual:
  every card's `<card>.inc` (`apple1.inc`, `gen2.inc`, `tms9918.inc`, `sid.inc`,
  …), `apple1/zp.inc`, font tables, Sokoban level packs, sprite data.
- **Small routine libs** — `apple1/` (`print.asm`, `kbd.asm`, `delay.asm`,
  `print_num.asm`), `text40/`, the peripheral drivers (`sid/`, `sd/`, `wifi/`,
  `a1io/`, `gt6144/`), `m6502/multiply.asm` / `prng8.asm` / `prng16.asm`,
  `gen2/` init+sync+plot helpers.
- **Zero-page**: these share the canonical slot pool declared by
  [`apple1/zp.inc`](apple1/zp.inc) — `.include "zp.inc"` once, before any other
  ZP `.res`. ⚠ It is a **positional contract** ($00-$07 in fixed order); see the
  DANGER banner in that file. Pinned by `apple1/test/`.

### Model B — separately-compiled object (`.o`) linked by `ld65`

The file is assembled / compiled on its own to a `.o` and put on the linker line
(asm via `EXTRA_ASM`, C via the `.mk` source sets). It carries `.export` /
`.import` symbols and lands in a linker-config segment; `ld65` **dead-strips**
whichever objects your program never references. Cross-TU zero-page is shared via
`.importzp` (and `zp.inc` emits the matching `.exportzp`).

- **Heavy asm objects** — `m6502/math.asm`, `tms9918/tms9918m1.asm` /
  `tms9918m2.asm`, `games/chess/chess_engine.asm`, `gen2c/gen2_blit.s`.
- **Every cc65 C runtime** — [`apple1c/`](apple1c/), [`gen2c/`](gen2c/),
  [`tms9918c/`](tms9918c/). Each ships a `.mk` fragment that splits the runtime
  into **per-family** objects (e.g. `gen2_text.o`, `gen2_sprites.o`) so a program
  links only the families it calls — that per-`.o` granularity is the whole point.
- **`gfx/` as cc65 archives** — the card-neutral geometry/number layer builds to
  `gfx-gen2.lib` / `gfx-tms.lib` (`ar65`). An archive contributes **zero** bytes
  for symbols a program doesn't reference, so it is purely additive on a link
  line. The backend (per-pixel store) is resolved at **link time** to the one
  card the program links — no runtime dispatch (Parmigiani's "one board at a
  time"; see [`gfx/README.md`](gfx/README.md)).

### Which one does a given file want?

| Signal | Model |
|---|---|
| It's a `.inc` | A — textual include (equates/data/macros) |
| `.asm`/`.s`, no `.export`, routines only | A — `.include` it |
| `.asm`/`.s` with `.export` / `.import` + a CODE segment | B — assemble to `.o`, link it (`EXTRA_ASM`) |
| `.c` / cc65 runtime (has a `.mk`) | B — compile via the `.mk` source set |
| `gfx_*.c` | B — link the matching `gfx-<card>.lib` archive |

When in doubt, **the lib's own README says** (e.g. `apple1` "Drop each `.asm` in
via `.include`"; `tms9918` "links the matching `.o`"). Per-card programming
guides live under `sketchs/doc/Programming_*.md`.

## Why two tracks per card (a decision, not debt)

**Status: accepted.** Applies to the asm↔C pairs — [`gen2`](gen2/)/[`gen2c`](gen2c/),
[`tms9918`](tms9918/)/[`tms9918c`](tms9918c/), [`apple1`](apple1/)/[`apple1c`](apple1c/).

**Context.** Each card carries two hand-written drivers, an assembly track and a
cc65 C track; roughly half of `dev/lib` is "the same card, twice." The reflex is
to call that debt and collapse it to one runtime + backends (the `gfx/` model) or
generate one track from the other.

**Decision.** Keep both, on purpose. They are two *products*, not a primary and a
stale copy:

- The **asm track** expresses the bare metal — smallest ROM, cycle-exact hot
  paths, beam-racing (`gen2_blit.s`, `gen2_sync.asm`), and the Apple-1-as-
  teaching-machine guides (`sketchs/doc/Programming_*ASM*`, `Programming_GEN2`,
  `Programming_TMS9918`).
- The **C track** (cc65) lowers the barrier: a beginner writes a GEN2 / TMS9918
  game without learning 6502 (`Programming_GEN2C`, `Programming_TMS9918C`).

Neither derives mechanically from the other — cc65 output is not the idiomatic
teaching asm, and hand-tuned beam-race asm does not lower to portable C.
"Deduplicate by generation" is a non-starter; the duplication is the price of
serving two audiences.

**What is NOT duplicated** (the boundary is drawn, not blurred):

- Hardware **equates** — one canonical source (the asm `.inc`), the C side
  mirrors it, `tools/check_lib_equates.py` fails CI on drift.
- The **font** — one master glyph file, per-format tables generated
  (`tools/build_shared_font.py`).
- Card-neutral **geometry + numbers** — written once in [`gfx/`](gfx/), both
  cards link it.

So what stays written twice is only the per-card *driver logic* whose idiom is
genuinely track-specific. The shared *data and contracts* are factored or pinned.

**Consequences (accepted).**

- The two tracks agree on the **hardware contract** (guarded), not on
  **implementation**. Behavioural parity is *not* a goal: the asm track may
  exploit cycle tricks the C track won't; the C track may be simpler and slower.
  That divergence is allowed by design.
- A change to a card's hardware contract (an address, an opcode, the font) must
  edit the canonical source; the drift check tells you which mirror to follow.
- A change to *behaviour* (a bug fix in one track) does **not** auto-propagate to
  the other, and is not expected to — port it by hand if it is conceptually
  shared. Nothing enforces this because nothing can.

This record exists so the duplication reads as a chosen tradeoff, reviewable on
its merits, rather than as rot someone forgot to clean up. If that calculus ever
changes (a third card would triple the cost, say), revisit it here.

## At the C level: three pieces, not one runtime (a decision, not debt)

**Status: accepted.** Applies to the C track — [`gen2c`](gen2c/),
[`tms9918c`](tms9918c/), [`gfx`](gfx/).

**Context.** The alternative considered was to make `gen2c` / `tms9918c` *backends*
of one shared C runtime and push the [`gfx/`](gfx/) link-time-backend model up into
init and mode management, so a card-neutral runtime drives both cards.

**Decision.** Keep the three pieces distinct, by design:

- **`gen2c`** — the rich GEN2 runtime (HIRES + NTSC artifact colour, LORES
  16-colour blocks, double-buffer pages).
- **`tms9918c`** — the rich TMS9918 runtime (cell mode, bitmap mode, hardware
  sprites, per-cell colour).
- **`gfx`** — the card-neutral, **additive** layer (geometry, numbers, the
  `gfx_text` cursor façade), bound to one card at link time.

Do **not** merge the two rich runtimes into backends, and do **not** grow `gfx`
past its neutral boundary into init / mode / colour / sprites.

**Why.** Geometry, numbers and monospaced text are *genuinely* card-neutral —
that is exactly `gfx`'s scope, and why it works. Init, the mode model, the
double-buffer story, colour (GEN2 NTSC-artifact + LORES vs TMS per-cell) and
sprites (TMS hardware, GEN2 has none) are where the two cards are *most*
different. A shared interface there is either the lowest common denominator
(throws away each card's reason to exist) or a fat union of per-card no-ops
(worse than honest separation). `gfx` already says this of itself: *additive and
deliberately neutral — it does not level down the rich per-card APIs.*

**Already true today (the proof, not the promise).**
[`sketchs/portable/hello_gfx_text/`](../../sketchs/portable/hello_gfx_text/)
compiles **one** drawing source for **both** cards: every draw call is neutral
(`gfx_gotoxy` / `gfx_text` / `gfx_putu` / `gfx_putx`), and only the one-time card
bring-up is `#ifdef`'d (`gen2_hgr_init()` vs `tms_init_regs(SCREEN2_TABLE)` +
`screen2_init_bitmap()`). That residual `#ifdef` is the *honest* expression of
where the cards genuinely differ, not debt to factor away — and bring-up is
exactly the wrong thing to neutralise: **GEN2 is double-buffered**
(`gen2_set_draw_page` / `gen2_show_page`) while **TMS9918 is single-buffered,
draw-straight-to-VRAM** (no page flip at all). A neutral `vid_present()` would be
a real flip on one card and a silent no-op on the other — the fat-union trap, in
one function.

**The boundary (explicit).** Neutral = `gfx` (geometry + numbers + `gfx_text`).
Per-card = init, mode, double-buffer, colour, sprites — kept in `gen2c` /
`tms9918c`. A portable **mono** program links `gfx` + the one card's init it
needs; a program that wants colour or sprites calls that card's runtime directly.
If a future card lands that shares GEN2's *or* TMS's model closely enough to make
a neutral init/present pay off, revisit it here — until then the neutral layer
stops at `gfx`'s current edge.

## Directory map

| Lib | Track | What |
|---|---|---|
| [`apple1`](apple1/) / [`apple1c`](apple1c/) | asm / C | Apple-1 ROM+PIA base: equates, text out (WOZ `ECHO`), keyboard |
| [`m6502`](m6502/) | asm | machine-agnostic math / trig / LFSR / division |
| [`gen2`](gen2/) / [`gen2c`](gen2c/) | asm / C | Uncle Bernie GEN2 HGR card (280×192 HIRES, LORES, beam-sync) |
| [`tms9918`](tms9918/) / [`tms9918c`](tms9918c/) | asm / C | P-LAB TMS9918 card (modes 1/2, sprites) |
| [`gfx`](gfx/) | C | card-neutral geometry + number formatting (link-time backend) |
| [`sid`](sid/) [`sd`](sd/) [`wifi`](wifi/) [`a1io`](a1io/) [`gt6144`](gt6144/) | asm | peripheral drivers: A1-SID, microSD, Wi-Fi modem, A1-IO/RTC, SWTPC GT-6144 |
| [`text40`](text40/) | asm | 40×24 text-mode UI helpers (layout / menu / repeat) |
| [`games`](games/) | asm | display-agnostic game engines (chess, rogue, sokoban) |
| [`telemetry`](telemetry/) | asm + C | dev telemetry side channel (`$C440-$C443`) |

## Validation

```
make -C dev/lib check
```

Validates the libraries themselves, **decoupled** from the projects that consume
them: asm↔C constant drift (`tools/check_lib_equates.py`), shared-font drift
(`tools/build_shared_font.py --check`), the `zp.inc` `$00-$07` layout pin, and a
compile of every C/asm source (so a primitive no project links yet still can't
rot). Companion to `make -C dev/projects` — both must be green to ship. See
[`Makefile`](Makefile).
