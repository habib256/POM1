# BASIC Compiler — Applesoft Lite → 6502 (GEN2 / TMS9918)

POM1 has **two** Applesoft compilers, for two different goals:

| | `src/BasicTokeniserApplesoft.*` (tokenizer) | `src/BasicCompilerApplesoft.*` (**native**) |
|---|---|---|
| Output | tokenized program + launcher | **standalone 6502 machine code** |
| Runtime | the Applesoft **interpreter ROM** | **none** — only the graphics card |
| Speed of the program | same as the interpreter | **~20× faster** (integer/control code) |
| Coverage | full Applesoft (FP, SIN/SQR, …) | integer **+ binary32 float incl. `SIN`/`SQR`/`INT`** |
| Win | skips slow keyboard injection | **optimises + accelerates execution** |

The first half of this document covers the **tokenizer** (load-without-injection);
[the native compiler](#native-compiler--standalone-machine-code-real-speedup) is
covered at the end — that is the one that *optimises and accelerates execution
without the interpreter*.

---

## Tokenizer — load without injection

Compile an Applesoft listing **ahead of time** into a ready-to-run 6502 memory
image, so a program is **loaded and run directly** instead of having its listing
typed into the live interpreter one keystroke at a time ("BASIC injection").

> **Success criterion (met & pinned):** `sketchs/basic_applesoft/3DHat.apf` — a
> floating-point hidden-line 3-D plot (`FOR/NEXT`, `GOSUB`, `IF/GOTO`,
> `SIN/SQR/INT`, `HGR2/HCOLOR/HPLOT`) — **compiles and executes on both the GEN2
> HGR card and the TMS9918 card**, drawing the hat into each framebuffer. Pinned
> by the `basic_compiler_smoke` ctest.

**Contents:** [Why](#why) · [Model](#model-tokenize--launch-not-native-codegen) ·
[Pipeline](#pipeline) · [Memory image](#the-compiled-image) ·
[Launcher](#the-launcher-stub) · [Interpreter addresses](#interpreter-addresses) ·
[Usage](#usage) · [Test](#test) · [Limits & integration](#limits--integration)

---

## Why

Until now the only way to get an `.apf` listing into POM1 was **keyboard
injection** (`Pom1BenchHost::injectBasic`): boot the interpreter to its `]`
prompt, then *type* the whole listing through the keyboard FIFO and finally
`RUN`. That is correct but slow (a per-character FIFO, capped at ~30 s) and it
re-tokenizes the program live every single run.

The compiler does the tokenization **once, ahead of time**, on the host, and
hands POM1 a binary image it can drop into RAM and execute immediately — no
listing typed at runtime.

## Model: tokenize + launch, **not** native codegen

This is **not** a native 6502 code generator. Re-implementing Applesoft's
floating point, `SIN/COS/SQR`, `FOR/NEXT`, `GOSUB`, arrays and the per-card
`HGR/HPLOT/HCOLOR` primitives in compiled 6502 would be an enormous, bug-prone
undertaking. Instead the compiler emits the program in **Applesoft's own
tokenized layout** — byte-for-byte what the interpreter's `PARSE` routine would
build in RAM if you typed the line — and lets the **resident interpreter ROM**
supply every runtime. The output is "compiled" in the threaded-code sense: the
tokenized program is data, the ROM is the engine.

This reuse is exactly why a 30-line floating-point program like 3DHat *just
works* on both cards: it runs against the real, shipped interpreter, so the math
and graphics are bit-for-bit the interpreter's.

The interpreter ROM must already be **resident and cold-started** (its zero-page
`CHRGET`, `HIMEM`/`FRETOP`, output vector and FP scratch set up) before the
launcher runs — i.e. the normal `]`-prompt state right after `4000R` (TMS
CodeTank cart) or `9800R` (GEN2 card). Booting the interpreter is **not**
injection; only the per-line typing of the listing is, and that is what this
replaces.

## Pipeline

`src/BasicTokeniserApplesoft.{h,cpp}` — pure (`<string>/<vector>/<cstdint>` only), so it
links into the bench (desktop + WASM), the CLI, the `basicc` tool and the tests.

```
basic::compile(source, target) ->
   1. TOKENIZE each line        (Applesoft reserved-word table, prefix-greedy)
   2. LAY OUT program @ $0801    (absolute forward links, $00 $00 terminator)
   3. EMIT launcher stub         (install VARTAB; JSR SETPTRS; JMP NEWSTT)
   -> { zones[], entry, progEnd, hex }
```

### Tokenizer

Reproduces `applesoft-{tms9918,gen2}.s : PARSE_INPUT_LINE` exactly (the
reserved-word table and token values are **identical** for both cards, so one
tokenizer serves both):

* the input line is **upper-cased** (the Apple-1 keyboard the ROM reads is
  upper-case only, so the live "type the listing" path sees upper-case too);
* blanks are ignored **except** inside a string literal or a `DATA` statement;
* `?` is shorthand for `PRINT`;
* bytes `0`..`;` (`$30`..`$3B`: digits, `:`, `;`) are copied verbatim, never
  tokenized — this is what keeps `GOTO`/`GOSUB` line numbers as ASCII digits the
  interpreter re-parses at run time;
* otherwise the reserved-word table is scanned **in order** and the first keyword
  that matches as a **prefix** (blanks in the input skipped mid-match) wins —
  prefix-greedy, which is why `HGR2` precedes `HGR` and `COLOR=` carries its `=`;
* `REM` copies the rest of the line literally; `DATA` copies literally up to `:`.

## The compiled image

Two zones, matching what the ROM builds in RAM when a listing is entered:

```
$0800            : $00                         guard byte (TXTTAB-1)
$0801 ...        : tokenized program lines      each [linkLo][linkHi][numLo][numHi][tokens][$00]
   ... E         : last line ends with $00
   E+1, E+2      : $00 $00                       end-of-program link (hi=0 -> stop)
   progEnd=E+3   : VARTAB the launcher installs
$0280 ...        : launcher stub (14 bytes)      run address
```

Forward links are **absolute** addresses of the next line's first byte (computed
from the `$0801` origin), so `NEWSTT` walks the program and `FNDLIN`/`GOTO`
search it exactly as for a typed-in program. Lines are emitted in ascending
line-number order (`FNDLIN` relies on it).

## The launcher stub

14 bytes at `$0280` (page-2 scratch — free at the `]` prompt, and only
overwritten by variables once the program is already running). It installs
`VARTAB` just past the program, then enters the interpreter's run loop:

```asm
        LDA #<progEnd
        STA $69            ; VARTAB lo   ($69/$6A = standard Applesoft TXTTAB+2)
        LDA #>progEnd
        STA $6A            ; VARTAB hi
        JSR SETPTRS        ; TXTPTR=TXTTAB-1; CLEARC (ARYTAB=STREND=VARTAB,
                           ; FRETOP=MEMSIZ); STKINI resets the 6502 stack
        JMP NEWSTT         ; main statement loop -> runs from the first line
```

`SETPTRS` ends in `STKINI`, which resets the 6502 stack — so jumping in from the
abandoned `GETLN` keyboard wait at the `]` prompt is clean. The program runs to
`END`, which drops back to the interpreter's `]` prompt, leaving the drawing in
the framebuffer.

## Interpreter addresses

The two ROM entry points are the **only** card-specific addresses; everything
else (`$0801` origin, `VARTAB` at `$69`) is shared Applesoft layout.

| Target | cold start | `SETPTRS` | `NEWSTT` | HIMEM | framebuffer |
|---|---|---|---|---|---|
| **TMS9918** (CodeTank cart @ `$4000`) | `4000R` | `$4596` | `$46F2` | `$4000` | TMS VRAM pattern `$0000-$1800` |
| **GEN2 HGR** (`roms/applesoft-gen2.rom` @ `$9800`) | `9800R` | `$9D96` | `$9EF2` | `$2000` | hi-res page-2 RAM `$4000-$5FFF` |

These were extracted with `ld65 -Ln` from the assembled interpreters and pinned
against the shipped ROMs: the rebuilt TMS image is **byte-identical** to the
upper bank of `roms/codetank/CODETANKDEV.rom`; the rebuilt GEN2 image matches
`roms/applesoft-gen2.rom` except for one cosmetic byte far above both routines.

**Re-deriving the addresses** (only needed if an interpreter ROM is rebuilt — the
`basic_compiler_smoke` test fails loudly if an address drifts):

```bash
# TMS9918 (source present)
ca65 -g -I sketchs/tms9918/applesoft_tms9918 -I dev/lib/tms9918 \
     -o /tmp/m.o sketchs/tms9918/applesoft_tms9918/applesoft-tms9918.s
ca65 -g -I sketchs/tms9918/applesoft_tms9918 -o /tmp/io.o \
     sketchs/tms9918/applesoft_tms9918/io.s
ld65 -C sketchs/tms9918/applesoft_tms9918/applesoft_tms9918.cfg \
     -Ln /tmp/labels.txt -o /tmp/tms.bin /tmp/m.o /tmp/io.o
grep -iwE 'SETPTRS|NEWSTT' /tmp/labels.txt

# GEN2 source was removed when the ROM was prebuilt; restore it to rebuild:
#   git show 72b39a7^:sketchs/gen2/applesoft_gen2/<file>   (build at origin $9800)
```

Then update `targetTms()` / `targetGen2()` in `src/BasicTokeniserApplesoft.cpp`.

## Usage

### Standalone tool — `basicc`

```bash
# built by CMake (target basicc), or directly:
g++ -std=c++17 -I src tools/basicc.cpp src/BasicTokeniserApplesoft.cpp -o basicc

basicc --target tms  sketchs/basic_applesoft/3DHat.apf -o 3DHat_tms.hex
basicc --target gen2 sketchs/basic_applesoft/3DHat.apf -o 3DHat_gen2.hex
```

The output is a Wozmon-style hex dump (`Memory::loadHexDump` format) whose run
address is the launcher (`$0280`). To run it: boot the matching interpreter to
its `]` prompt, then load the hex and jump to `$0280`.

### Library

```cpp
#include "BasicTokeniserApplesoft.h"
basic::Result r = basic::compile(listing, basic::targetGen2());
if (r.ok) {
    for (const basic::Zone& z : r.zones) /* memcpy z.bytes -> RAM @ z.addr */;
    /* jump the CPU to r.entry */
}
```

## Test

`tests/basic_compiler_smoke_test.cpp` (ctest `basic_compiler_smoke`) compiles the
real `3DHat.apf` for **both** targets and, with no keyboard injection:

1. loads the interpreter ROM and cold-starts it (jump to `$4000`/`$9800`, run to
   the `]` prompt);
2. pokes the compiled image + launcher into RAM and jumps to `$0280`;
3. asserts the framebuffer fills with plotted pixels (TMS: ≥300 VRAM pattern
   bytes; GEN2: ≥300 hi-res page-2 bytes — the finished hat lights ~450, reached
   in ~250 M cycles; a cleared HGR screen has zero, so any plotting only adds).

It skips (ctest code 77) if a ROM or the source sketch is absent.

## Limits & integration

* **Cold start is required.** The compiled image assumes the interpreter's zero
  page is already set up. The headless test and the standalone flow boot the ROM
  first; the natural in-app home is the **DevBench**.
* **DevBench hook (next step).** A "Compile & run (native image)" path in
  `Pom1BenchHost` for the Applesoft BASIC targets would: cold-start the matching
  interpreter (as `injectBasic` already does for the ROM), then — instead of
  typing the listing — `basic::compile()` it and poke the zones + jump to the
  launcher. Same instant-load on desktop **and** WASM (the compiler is pure C++).
* **Scope.** The tokenizer covers the full Applesoft Lite statement/function set
  of both dialects (the shared `TOKEN_NAME_TABLE`). It does not renumber, expand
  abbreviations beyond `?`→`PRINT`, or validate semantics — a malformed program
  tokenizes and then errors at run time exactly as the interpreter would.

See also: `CLAUDE.md` (memory map, presets), `sketchs/tms9918/applesoft_tms9918/`
and the restored `sketchs/gen2/applesoft_gen2/` (interpreter sources),
`doc/DEVBENCH.md` (the bench that hosts the BASIC targets).

---

## Native compiler — standalone machine code (real speedup)

`src/BasicCompilerApplesoft.*` is a **real native-code compiler**: it generates
standalone 6502 assembly with native control flow, variables at fixed addresses,
and expressions compiled once into straight-line code. The output binary runs
with **no Applesoft interpreter** — only the graphics card. Every interpreter tax
disappears: token dispatch, per-iteration expression re-parsing, variable-name
string search, GOTO line search, and (for integer code) the float pack/unpack on
every operation.

### Why it's faster (measured)

Benchmarked on POM1's own 6502 core, native vs the same source on the interpreter
(both draw the **identical** picture):

| Program | native | interpreter | speedup |
|---|---|---|---|
| line-draw (pixel-plot bound) | 2.6 M cyc | 11.6 M cyc | **4.5×** |
| compute-heavy inner loop (arith + control) | 19–35 M cyc | 368–705 M cyc | **~20×** |

Pixel-bound code wins less (the per-pixel plot cost is shared); **control- and
arithmetic-bound code wins big**, which is exactly where the interpreter spends
most of its time on overhead rather than work. Pinned by `basic_native_run`
(asserts identical output **and** native faster) + `basic_native_codegen`.

### What makes it a compiler, not a tokenizer

- **Strength reduction.** `X*3` compiles to shifts+adds (`asl`/`adc`), not a
  16-iteration multiply — a classic compiler optimisation. This alone moved the
  arithmetic benchmark from ~6× to ~20×.
- **Native control flow.** `GOTO`→`JMP`, `GOSUB`/`RETURN`→`JSR`/`RTS`, `FOR`/`NEXT`
  and `IF` are native branches with a signed 16-bit compare; line numbers become
  labels (no run-time search).
- **Fixed-address variables + temporaries** (16-bit signed), allocated by the
  compiler — no name lookup, no float boxing.

### ABI + runtime

Generated code calls a tiny fixed runtime (`rt_hgr/rt_hcolor/rt_plot/rt_line/
rt_mul/rt_div/rt_cmp16`) implemented per card in `dev/lib/basicrt/basicrt_{gen2,
tms}.s`, which wrap the project's graphics asm leaf routines (GEN2 `plot_pixel`/
`clear_hgr`; TMS `plot_set`/`line_xy` — not the interpreter). 16-bit math is shared
in `basicrt_math.inc`. The program + runtime link via `basicc_native.cfg` into a
bare image that loads + runs at `$0300`.

### Scope (integer phase)

Variables and arithmetic are **16-bit signed**, allocated in **zero page** (so
every op uses the short/fast zp addressing mode). Supported: `+ - * /`,
comparisons, `AND/OR/NOT`, `ABS`, `FOR/NEXT`, `IF/THEN`, `GOTO`, `GOSUB/RETURN`,
`END`, `REM`, `PRINT` (string literals + signed integers, `;`/`,` separators,
trailing-`;` newline suppression — via the WOZ terminal), and integer graphics
`HGR/HGR2`, `HCOLOR=`, `HPLOT` (point + `TO`-chains) at the **full GEN2 hi-res
width 0..279** (16-bit X; TMS is natively 0..255). Float literals are rejected.
The float phase adds `SIN`, `SQR`, `INT` and decimal literals on top of the same
control flow. `INT` in the integer phase is the identity (links nothing).

### Phase 2 — standalone floating point (done; 3DHat compiles + runs)

**Phase 2a (done): a tested binary32 software-float runtime** —
`dev/lib/basicrt/basicrt_float.s`, the autonomous FP core (no Applesoft ROM). It
stores values as 4-byte IEEE-754 single and implements `fp_fromint16`,
`fp_toint16`, `fp_add`, `fp_sub`, `fp_mul`, `fp_div`, `fp_cmp` plus the
transcendentals `fp_int` (truncate toward zero), `fp_sqrt` (Newton–Raphson, 5
iterations) and `fp_sin` (2π range reduction → symmetry fold to [-π/2,π/2] → 4-term
Taylor) — operands in the zero-page slots `FA`/`FB`. Internally a value unpacks to
`{sign, E, SG}` with the 24-bit significand `SG ∈ [2^23, 2^24)` and `value = SG·2^E`,
computes, and repacks. Each transcendental is **feature-gated** (`-D FP_INT` /
`-D FP_SQRT` / `-D FP_SIN`) so it is assembled only when the program calls it. Pinned
by `basic_float_runtime` (cc65-gated): every op — arithmetic and transcendental — is
checked against the host's IEEE `float`/`sinf`/`sqrtf` over a value grid + randomised
pairs (5736 cases).

**Phase 2b (float codegen — validated): the compiler emits float code.**
`basicnative::compile(src, card, /*floatMode=*/true)` (also `basicc --native
--float`) compiles in **binary32**: variables/temporaries are 4-byte floats, the
expression codegen routes `+ - * /` and comparisons through `fp_*`, `FOR/NEXT`
loops on floats, `IF` tests a float, and `HPLOT`/`HCOLOR` coordinates are
converted with `fp_toint16`. A float program (e.g. a parabola `Y=(X-140)*(X-140)
/140`) compiles to a standalone binary that runs **with no interpreter and no ROM
float** and draws the same picture as the interpreter. Pinned by
`basic_native_run` (now covers an integer **and** a float program).

Measured (native vs the same source on the interpreter, identical output):

| Program | bound by | native | interpreter | speedup |
|---|---|---|---|---|
| integer compute loop | arith + control | 16.8 M | 368 M | **~22×** |
| integer line-draw | pixel plotting | 2.6 M | 11.6 M | **~4.5×** |
| **float** parabola | floating-point | 2.8 M | 5.6 M | **~2.0×** |

The float case is the honest ceiling for FP-bound code: the binary32 work itself
isn't cheaper than the ROM's float, so the ~2× gain is purely from removing
interpreter overhead. Control/integer code, where that overhead dominates, wins
an order of magnitude more.

**Phase 2c (done): `SIN`/`SQR`/`INT` + `3DHat.apf` runs native.** `primary()`
compiles `SIN`/`SQR`/`INT` to a `jsr fp_sin`/`fp_sqrt`/`fp_int` (auto-precision
forces the float phase the moment `SIN`/`SQR` appear), and logical `AND`/`OR` on
float truth values were fixed (they previously fell through to the comparison path).
With these, **`sketchs/basic_applesoft/3DHat.apf` — the MTU/Micro May-1981 hidden-line
3-D HAT (HGR2, nested `FOR`, `IF/GOTO`, `GOSUB/RETURN`, `INT`/`SQR`/`SIN`, decimal
literals, `HCOLOR=0` column erase) — compiles and runs standalone on both GEN2 and
TMS9918, drawing the sombrero with proper hidden-line removal.** Pinned by
`basic_native_run` (a native-only 3DHat case). It is compute-heavy (≈20 000
transcendental evaluations + per-point erase lines), so it is genuinely slow to
finish — but still faster than the same source on the interpreter, and with **no ROM
at all**.

#### HCOLOR=0 erase (hidden-line removal)

The GEN2 runtime `rt_plot` is **pen-aware**: `HCOLOR=0` clears the pixel
(`AND ~mask`) instead of setting it (`OR mask`), so the 3-D HAT's
"plot the point, then erase the column below it" hidden-line trick works. `rt_hgr`
seeds a non-zero default pen so a plain `HPLOT` before any `HCOLOR` still draws.

#### Peephole optimizer (code size)

Codegen routes every value through fixed 2/4-byte slots, which produces "define a
temp, then copy it elsewhere" chains. A peephole pass (`Codegen::optimizePeephole`)
fuses them: a store block into a temp whose value is next consumed by a copy block
is **retargeted to store straight into the destination** (the temp vanishes), and the
resulting self-copies are deleted. Liveness is intra-block (any label/branch/`jmp`/
GOSUB ends the scan; runtime `jsr fp_*`/`rt_*` are transparent since they never touch
compiler temps). This is what lets the full 3DHat fit GEN2's `$0300–$1FFF` window
(it shares RAM with the framebuffer at `$2000`): the pass trims ~640 bytes, taking the
image from an overflowing ~7.8 KB to **7157 B**.

### Choosing the precision (auto)

There is no single "float" — the right representation is a trade-off:

| representation | mantissa | bytes | notes |
|---|---|---|---|
| **integer 16-bit** | — | 2 | smallest + fastest; exact for whole numbers |
| **binary32** (used) | ~7 digits | 4 | IEEE single; the FP phase's runtime |
| Applesoft MBF (5-byte) | ~9 digits | 5 | the ROM's format — more precise, bigger/slower |
| binary16 / fixed-point | ~3 digits | 2 | enough for bounded graphics coords; a future "fast" tier |

`compile(..., FpMode::Auto)` (the default, also `basicc --native`) picks the
**smallest sufficient**: integer unless a line needs a fraction — a decimal
literal or a `/` (Applesoft division is real) — in which case binary32. So a
program that doesn't use floats **never links the float runtime at all**. `--int`
and `--float` force a tier. binary16 / fixed-point graphics tiers are a documented
future option for when binary32 precision is overkill.

### Minimal code size (dead-stripping)

Code/RAM size is a first-class concern, so the compiler emits **only the runtime
symbols it actually uses**, and the runtime (`basicrt_*.s`) gates every routine on
a `-D RT_xxx` flag the build derives from those imports. Unused routines — and the
560-byte hi-res pixel tables — never reach the binary. Measured GEN2 image sizes:

| program | full runtime | dead-stripped |
|---|---|---|
| `X=5+2 : X=X+1` (no runtime) | 1695 B | **89 B** |
| `PRINT` + `FOR` (no graphics) | 1746 B | **265 B** |
| `HGR : HPLOT x,y` | 1686 B | 1165 B |
| `HGR` + `HPLOT…TO` lines | 1833 B | 1606 B |

### Diagnostics (writing new programs)

Errors name the **exact Applesoft line** the author must fix, in plain language:

```
line 20: FOR expects a variable
line 10: HPLOT expects 'x,y'
line 10: GOTO 99: no such line number in the program
line 1: every program line must start with a line number
```

`GOTO`/`GOSUB`/`THEN` targets are checked against the program's line numbers at
compile time (not left to a cryptic link error), float literals are rejected with
a line number in the integer phase, and `NEXT` without a matching `FOR` is caught.

### Benchmarks (size + speed by program type)

Generated by `basic_native_bench` (cc65 + GEN2 ROM gated; `ctest -R basic_native_bench -V`).
Each program is compiled to a standalone GEN2 image and run on POM1's 6502 core; the
speedup is native-vs-the-same-source-on-the-interpreter (both draw the same picture).

| program | phase | binary | rt routines | native cyc | interp cyc | speedup |
|---------|-------|-------:|------------:|-----------:|-----------:|--------:|
| int-arith (strength-reduced `*`) | int | 1616 B | 4 | 0.2 M | 4.2 M | **21×** |
| int-raster (nested fill) | int | 1380 B | 4 | 5.8 M | 82.6 M | **14×** |
| float-arith (parabola, `+-*/`) | float | 2914 B | 9 | 2.8 M | 5.6 M | **2.0×** |
| transcend (`SIN`) | float | 3415 B | 9 | 8.4 M | 11.6 M | **1.4×** |
| lines (16-bit Bresenham) | int | 1568 B | 4 | 1.6 M | 3.8 M | **2.4×** |
| **3dhat** (full demo) | float | 7157 B | 13 | (compute-heavy) | — | runs native |

Reading the table: **size** scales with what's linked — integer programs link 4 runtime
routines and **no** float code; the float phase adds the `fp_*` core; only 3DHat pulls in
all 13 (incl. `fp_int`/`fp_sqrt`/`fp_sin`). **Speed**: control/integer code wins an order of
magnitude (interpreter overhead removed); FP-bound code wins ~1.4–2× (the binary32 work
itself isn't cheaper than the ROM's float, so the gain is purely overhead). The honest
takeaway — native is always faster, and *much* faster the more the program is bound by
control flow and integer arithmetic rather than raw float throughput.

### Use it

```bash
# emit assembly, then assemble + link to a standalone .bin (needs cc65):
build/basicc --native --card gen2 prog.bas -o prog.s
tools/basicc_native.sh gen2 prog.bas prog.bin           # integer program
tools/basicc_native.sh --float gen2 3DHat.apf hat.bin   # float program (SIN/SQR/INT, decimals)

# run it with no interpreter (GEN2 card present):
pom1 --headless --preset 11 --load 0x0300:prog.bin --run 0x0300 --dump-gen2-frame out.png
```
