# dev/lib — POM1 6502 software libraries

*[← dev/ index](../README.md)*  ·  build layer: [`../cc65/`](../cc65/README.md)  ·  projects: [`../projects/codetank/`](../projects/codetank/README.md)

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
tutorials ship with POM1's user documentation.

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
  paths, beam-racing (`gen2_blit.s`, `gen2_sync.asm`), and pairs with the
  Apple-1-as-teaching-machine assembly tutorials in POM1's user documentation.
- The **C track** (cc65) lowers the barrier: a beginner writes a GEN2 / TMS9918
  game without learning 6502, backed by the matching cc65 tutorials in that same
  user documentation.

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

Every cell links to that lib's own README (the per-lib API + ZP detail).

| Lib | Track | What |
|---|---|---|
| [`apple1`](apple1/README.md) / [`apple1c`](apple1c/README.md) | asm / C | Apple-1 ROM+PIA base: equates, text out (WOZ `ECHO`), keyboard |
| [`m6502`](m6502/README.md) | asm | machine-agnostic math / trig / LFSR / division |
| [`gen2`](gen2/README.md) / [`gen2c`](gen2c/README.md) | asm / C | Uncle Bernie GEN2 HGR card (280×192 HIRES, LORES, beam-sync) |
| └ [`gen2/sprites`](gen2/sprites/README.md) | asm | GEN2 sprite data + blit tables |
| [`tms9918`](tms9918/README.md) / [`tms9918c`](tms9918c/README.md) | asm / C | P-LAB TMS9918 card (modes 1/2, sprites) |
| [`gfx`](gfx/README.md) | C | card-neutral geometry + number formatting (link-time backend) |
| [`sid`](sid/README.md) [`sd`](sd/README.md) [`wifi`](wifi/README.md) [`a1io`](a1io/README.md) [`gt6144`](gt6144/README.md) | asm | peripheral drivers: A1-SID, microSD, Wi-Fi modem, A1-IO/RTC, SWTPC GT-6144 |
| [`text40`](text40/README.md) | asm | 40×24 text-mode UI helpers (layout / menu / repeat) |
| [`games`](games/README.md) | asm | display-agnostic game engines — see per-game READMEs below |
| └ [`games/chess`](games/chess/README.md) · [`games/rogue`](games/rogue/README.md) · [`games/sokoban`](games/sokoban/README.md) | asm | chess engine, roguelike FOV/dungeon-gen, Sokoban core |
| [`telemetry`](telemetry/README.md) | asm + C | dev telemetry side channel (`$C440-$C443`) |

## Consuming a library (build integration)

How a lib reaches your program, mechanically — the integration *model* (A vs B)
is decided above; this is the build wiring. Project Makefiles set these and
`include ../../cc65/Makefile.common` (linker configs + that fragment are
documented in [`../cc65/`](../cc65/README.md)).

- **`.include`-style asm (Model A).** Add the lib's directory to `LIB` as a
  `ca65 -I` flag and `.include "<file>"` in your source. `ca65` searches every
  `-I` path, so `.include "apple1.inc"` / `.include "print.asm"` resolves once
  the dir is on the line:

      LIB := -I ../../lib/apple1 -I ../../lib/m6502 -I ../../lib/gen2

  Then in the `.asm`: `.include "apple1.inc"` / `.include "zp.inc"` /
  `.include "print.asm"` … Nothing is dead-stripped — you assemble all of it.

- **Separately-compiled asm objects (Model B).** Heavy `.asm` carrying
  `.export`/`.import` (e.g. `tms9918m1.asm`, `math.asm`, `gen2_blit.s`) go in
  `EXTRA_ASM`. `Makefile.common` assembles each to its own `.o` with the same
  `$(LIB)` includes and puts it on the `ld65` line, so `ld65` dead-strips it if
  unreferenced:

      EXTRA_ASM := ../../lib/tms9918/tms9918m1.asm

  Cross-TU zero-page is shared by `.importzp` in the lib and `.exportzp` in your
  TU — `zp.inc` emits the matching exports for the baseline slots; heavy libs
  that need more (`math.asm`, `tms9918m2.asm`) list them in their own README.

- **cc65 C track (Model B).** The C runtimes ship a `.mk` fragment
  (`apple1c.mk`, `gen2c.mk`, `tms9918c.mk`) exposing **per-family** source sets.
  Set the dir variables, `include` the fragment, and put only the families you
  call into `SRCS`; `ld65` strips whole families you omit. Link the card-neutral
  `gfx` layer as its prebuilt archive (`gfx-<card>.lib`). Sketch:

      GEN2C := ../../lib/gen2c
      APPLE1C := ../../lib/apple1c
      include $(APPLE1C)/apple1c.mk
      include $(GEN2C)/gen2c.mk
      SRCS := main.c $(GEN2C_CORE_SRCS) $(GEN2C_TEXT_SRCS) $(APPLE1C_SRCS)
      INCS := $(GEN2C_INCS) $(APPLE1C_INCS) -I ../../lib/gfx
      cl65 -t none -Oirs -C $(GEN2CFG) $(INCS) $(SRCS) ../../lib/gfx/gfx-gen2.lib -o main.bin

  Build the `gfx-<card>.lib` archive once with `make -C ../../lib/gfx <card>`.

## Cross-library zero-page map

ZP is the scarce shared resource and **collisions raise no error** — they
silently corrupt the other claimant (see the DANGER banner in
[`apple1/zp.inc`](apple1/zp.inc)). This table lists every slot a lib *reserves
by name* so you can spot overlaps before they bite. Slots come in two flavours:
**fixed-address** (the `zp.inc` baseline, `$00-$07` in declaration order) and
**floating** (a `.res 1` / `.importzp` whose address is wherever the linker lays
the `ZEROPAGE` segment — these don't collide by *address*, they collide by
*name*: two libs reserving the same symbol share one byte, which is intended for
the `zp.inc` baseline and accidental otherwise).

**Baseline — `apple1/zp.inc`, fixed `$00-$07`** (opt-in; `.include` once before
any other ZP `.res`; `.exportzp`'d for cross-object use):

| Addr | Symbol | Owner / users |
|---|---|---|
| `$00` | `tmp` | general scratch — `kbd.asm`, `math.asm`, `tms9918*.asm`, chess |
| `$01` | `tmp2` | general scratch — `math.asm`, `tms9918m2.asm`, chess |
| `$02`/`$03` | `print_ptr_lo`/`hi` | `apple1/print.asm` |
| `$04`/`$05` | `mul_tmp`/`mul_res0` | `m6502/multiply.asm` |
| `$06`/`$07` | `prng_lo`/`hi` | `m6502/prng8.asm` **and** `prng16.asm` (shared pair) |

**Floating slots each lib reserves** (own `.res` in `ZEROPAGE`, or `.importzp`
the caller must declare). Each *track is used by one card at a time*, so the
real collision risk is mixing a peripheral driver with a graphics track that
both park names in the same low region — check before combining:

| Lib | Slots | Notes |
|---|---|---|
| `m6502/math.asm` | imports `tmp`,`tmp2`,`arg_lo/hi`,`arg2_lo/hi`,`th_lo/hi` (+ BSS `prod_lo/hi`,`sign_flag`,`lfsr_lo/hi`) | caller-declared/`.exportzp`'d; owns no ZP itself |
| `tms9918/tms9918m1.asm` · `_text.asm` · `_console.asm` | `vdp_lo`,`vdp_hi`,`vdp_src_lo`,`vdp_src_hi`,`vdp_row`,`vdp_col` (console adds `cur_row`,`cur_col`,`con_tmp`) | `.exportzp`'d so `m2`/console/text share them |
| `tms9918/tms9918m2.asm` | `pix_x`,`pix_y`,`pix_addr_lo/hi`,`pix_mask`,`pix_byte`,`ln_x0/y0/x1/y1`,`ln_dx/dy/sx/sy`,`ln_err`,`ln_err_hi`,`pen_color` | bitmap/line raster state; imports `tmp`,`tmp2` |
| `gen2/subbyte_fill.asm` | `sb_ptr_lo`,`sb_ptr_hi` | sub-byte HGR fill pointer |
| `text40/layout.asm` | `key_up_code`,`key_left_code` | arrow-key remap |
| `sd/sd.asm` | `sd_str_lo`,`sd_str_hi` | string pointer |
| `wifi/acia.asm` | `acia_str_lo`,`acia_str_hi` | string pointer |
| `a1io/a1io.asm` | `a1io_target` | register selector |
| `games/chess/chess_engine.asm` | `ce_sq`,`ce_dir`,`ce_target`,`ce_piece`,`ce_color`,`ce_dirs_left`,`ce_dir_ptr`,`ce_match`,`attacker_color`,`attacked_sq`,`atk_piece` | 11 B engine-private; imports `tmp`,`tmp2` |

`gen2`/`gen2c`/`tms9918c` keep their ZP convention in their own READMEs; the
`.res`-bearing TMS helpers (`sprite_triangle.asm`, `buffer_editor.asm`,
`tms9918_5strigger.asm`) declare project-private state documented at their use
sites. When ZP is tight, **alias** a slot onto existing scratch *before* the
include — the `.ifndef` guards detect it (recipe in `apple1/zp.inc`).

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
