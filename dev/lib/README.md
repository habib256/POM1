# dev/lib — POM1 6502 software libraries

*[← dev/ index](../README.md)*  ·  build layer: [`../cc65/`](../cc65/README.md)  ·  projects: [`../projects/codetank/`](../projects/codetank/README.md)

*Tutorial: [6502 developer guides index](../../sketchs/doc/README.md) — step-by-step programming guides for every track in this tree.*

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

## Porting between cards (TMS9918 ↔ GEN2)

The two graphics tracks look symmetric (init / clear / plot / line / text on
both sides) but the hardware contracts do NOT port 1:1. Five asymmetries that
bite, verified against the current lib sources:

| Contract | TMS9918 | GEN2 |
|---|---|---|
| **Display gating** | R1 bit 6 is a real enable. Contract: **blank first, enable last** — `init_vdp_g1`/`init_vdp_g2` force the bit off on the register pass so the VRAM bursts run blanked; the C tables (`SCREEN1_TABLE`, `SCREEN2_TABLE`) ship R1 = `$80` and `screen1_prepare()` / `screen2_init_bitmap()` do the first enable only once tables + SAT park are valid. Helpers: `vdp_display_off` / `vdp_display_on` ([`tms9918/tms9918_pad.asm`](tms9918/tms9918_pad.asm)). | **No enable bit** — the screen always shows *something*. The analogue is ordering: park the display on TEXT, scrub the framebuffer (plain RAM, writable in any mode), and flip to HIRES **last**. `gen2_hgr_init_clear` ([`gen2/gen2_init.asm`](gen2/gen2_init.asm)) is exactly that sequence (`GEN2_TEXTON` → clear `$2000-$3FFF` → `gen2_hgr_init`); plain `gen2_hgr_init` flips immediately and shows power-on SRAM garbage until the caller's own clear lands. |
| **Write pacing** | The data port drops writes spaced under ~16c during active Mode I/II display. **pad18 contract**: `JSR tms9918_pad18` between stores = 22c store-to-store (`tms9918_pad12` is a legacy alias resolving to pad18; the pad is flag-transparent by invariant). Free zones — display blanked or V-blank — serve ~2c ScreenOff slots, so init / full-screen bursts always fit ([`tms9918/tms9918_pad.asm`](tms9918/tms9918_pad.asm)). | Framebuffer = plain bus RAM: **no pacing**, `STA` at full speed. The care moves to the **soft switches**: READ-only toggles that return HST0 in bit 7 (bits 0-6 are floating-bus garbage). Poll only a switch already in your program's state (the poll IS a toggle) and OR two samples ≥ 4 cycles apart to mask the 3-cycle colour-burst notch ([`gen2/gen2.inc`](gen2/gen2.inc)). |
| **Frame wait** | `tms_wait_end_of_frame()` **drains** the stale F flag, polls, and **returns a status snapshot** (fresh F merged with C/5S accumulated across the wait). One status read per frame: a read latch-clears F+5S+C atomically — test the returned copy, never re-read ([`tms9918c/tms9918.c`](tms9918c/tms9918.c); propagated by `vsync_wait()`). | `gen2_wait_vbl()` **returns void** — HST0 is a level, not a latch: nothing to drain, no status to snapshot. Level-samples HST0 and returns ~3 lines into V-blank ([`gen2c/gen2_init.c`](gen2c/gen2_init.c)). Asm: `gen2_waitvbl` (coarse) / `gen2_beam_lock` (cycle-exact) in [`gen2/gen2_sync.asm`](gen2/gen2_sync.asm). |
| **Frame counter** | `vsync_frames` ([`tms9918c/vsync.c`](tms9918c/vsync.c)), bumped by `vsync_wait()`. | None — count your own around `gen2_wait_vbl()` calls. |
| **Exit to monitor** | `JSR vdp_display_off` then `JMP WOZMON`. ⚠ An R1 write also loads the VRAM address counter — never flip the display between a `vdp_set_write/read` and its data stream. Both tracks now use the `$FF1A` **prompt entry** (`WOZMON` in [`apple1/apple1.inc`](apple1/apple1.inc); the C runtime's `woz_mon()` in [`tms9918c/apple1_asm.s`](tms9918c/apple1_asm.s) was unified onto it June 2026 — `woz_mon_silent()` keeps the silent `$FF1F` warm restart for callers that printed their own status). | `JSR gen2_text_restore` (TEXT + PAGE1 toggles, [`gen2/gen2_init.asm`](gen2/gen2_init.asm)) then `JMP WOZMON` — hands the display back to the text page the monitor echoes on. The helper deliberately RTSes instead of jumping to Wozmon itself (pure GEN2 layer; the `WOZMON` equate belongs to the caller's `apple1.inc`). |

### The cc65 `-t none` DATA trap (both C tracks)

The `-t none` crt0 has **no copydata**: an initialized global — even `= 0` —
lands in the DATA segment and is **never copied** to its run address on a
load ≠ run (ROM) cfg; it silently holds power-on garbage. Only *uninitialized*
globals live in BSS, which crt0's zerobss clears on **every** entry (each
`4000R` / `6000R` re-run). RAM-load cfgs (load = run, e.g. `apple1_gen2_c.cfg`)
dodge the first-run bite because DATA ships inside the loaded image — but the
value is still stale on a warm re-entry without a re-load, and a move to a ROM
cfg breaks it. House pattern: **BSS + lazy default** or **zero-meaningful
encodings** —

- [`tms9918c/random.c`](tms9918c/random.c) — LFSR states deliberately
  uninitialized; `rand8()`/`rand16()` auto-seed when they read 0 (the old
  `= 0xAC` initializers were never applied, and a 0 state zero-locks an LFSR).
- [`tms9918c/screen2_pixel.c`](tms9918c/screen2_pixel.c) — `PLOT_MODE_SET` is
  defined as 0 precisely so the zerobss'd `screen2_plot_mode` **is** the
  default.
- [`gfx/gfx_text.c`](gfx/gfx_text.c) — cursor statics kept genuinely
  uninitialized; BSS zero = home position.
- [`gen2c/gen2_init.c`](gen2c/gen2_init.c) — `gen2_hgr_base` is BSS with a
  lazy default at the use sites (`if (!gen2_hgr_base) gen2_hgr_base = 0x20`):
  zero is not a valid page base, so BSS-zero means "not set yet".

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
| `gen2/hgr_plot.asm` (include) | caller defines `cur_x`,`cur_y`,`ptr_lo`,`ptr_hi` | `plot_pixel` preserves `cur_x`/`cur_y`; needs the `hgr_lo/hi/col/mask` tables alongside |
| `gen2/hgr_clear.asm` (include) | caller defines `ptr_lo`,`ptr_hi` | `clear_hgr` trashes both (same pair as `hgr_plot.asm` — intended shared) |
| `tms9918/sprite_triangle.asm` · `buffer_editor.asm` | import `tmp`,`tmp2`,`arg_lo/hi` (triangle also `arg2_lo/hi`) | reuse `math.asm`'s argument slots — see the `arg_*` rule below |
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

Three cross-cutting rules the tables can't show:

- **`tmp`/`tmp2` are pure scratch.** Any lib JSR may clobber them — never
  hold a value in them across a call into *any* lib routine.
- **`arg_lo`/`arg_hi` (+ `arg2_*`) are a shared argument/return window, not
  storage.** `m6502/math.asm`, `tms9918/sprite_triangle.asm` and
  `tms9918/buffer_editor.asm` all `.importzp` the same bytes; a value parked
  there is NOT live across a call between these modules — reload per call.
- **Wozmon reserves `$24-$2B`** (`XAML/XAMH/STL/STH/L/H/YSAV/MODE`): the
  monitor rewrites them on every prompt interaction, so a resident program
  must not park state there.
  [`../cc65/apple1_4k.cfg`](../cc65/apple1_4k.cfg) guards the reserve via its
  ZP ceiling (`$00-$22`); [`../cc65/apple1_gen2.cfg`](../cc65/apple1_gen2.cfg)
  does **not** (its ZP window is `$00-$3F`) — a gen2 program whose `ZEROPAGE`
  segment grows past `$23` overlaps the reserve and loses those bytes to any
  Wozmon round-trip.

### Symbol-collision registry (deliberate link-exclusive shadows)

Several Model-B objects export the SAME symbols on purpose — alternative
backends behind one seam. `ld65` errors on the duplicate if you link both
sides of a row; that error is the guard, not a bug. Pick per card / per need:

| Symbol(s) | Defined by | Pick |
|---|---|---|
| `init_vdp_g2`, `clear_bitmap`, `disable_sprites`, `vdp_set_write/read`, `calc_pix_addr`, `plot_set(_x16)`, `line_xy(16)`, `tms9918_pad18`, `vdp_display_off` + the `pix_*`/`ln_*`/`pen_color` ZP | [`tms9918/tms9918m2.asm`](tms9918/tms9918m2.asm) + [`tms9918/tms9918_pad.asm`](tms9918/tms9918_pad.asm) **vs** [`gen2/gen2_logom2.asm`](gen2/gen2_logom2.asm), which re-exports the whole seam (pads / VDP address ops become no-ops) | one backend per link — how LOGO's card-independent core targets either card |
| `disable_sprites`, `vdp_set_write`, `vdp_set_read` | [`tms9918/tms9918m1.asm`](tms9918/tms9918m1.asm) **vs** [`tms9918/tms9918m2.asm`](tms9918/tms9918m2.asm) | one TMS mode per program (m1 XOR m2) |
| `draw_bubble` | [`tms9918/bubble.asm`](tms9918/bubble.asm) **vs** [`gen2/gen2_bubble.asm`](gen2/gen2_bubble.asm) | per card |
| `text_blit_glyph` | [`tms9918/text_bitmap.asm`](tms9918/text_bitmap.asm) **vs** [`gen2/gen2_text_bitmap.asm`](gen2/gen2_text_bitmap.asm) (same `A`/`pix_x`/`pix_y`/`pen_color` contract) | per card |
| the whole `rt_*` seam (`rt_hgr`, `rt_hcolor`, `rt_plot`, `rt_line`, `rt_gr`, `rt_color`, `rt_loresplot`, `rt_hlin/vlin`, `rt_text`, `rt_home` + `rt_*` ZP) | [`basicrt/basicrt_tms.s`](basicrt/basicrt_tms.s) **vs** [`basicrt/basicrt_gen2.s`](basicrt/basicrt_gen2.s) | BASIC-compiler runtime backend: per target card |
| every sprite label (`creat_wolf_pat`, …) | `tms9918/sprites_*.asm` **vs** [`gen2/sprites/`](gen2/sprites/)`*_hgr.asm` — same names, **different data** (TMS native 32 B/sprite vs HGR 16×3 B rows) | per card; the format is implicit in which `.o` you pull |
| `prng_lo`/`prng_hi` (ZP state; Model A) | [`m6502/prng8.asm`](m6502/prng8.asm) **and** [`m6502/prng16.asm`](m6502/prng16.asm) | shared **by design** (`.ifndef`-guarded, no link error): including both compiles, but the two generators stir ONE state pair — use one |

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
