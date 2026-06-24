# BASIC Compiler — Applesoft Lite → 6502 (GEN2 / TMS9918)

POM1 has **two** Applesoft compilers, for two different goals:

| | `src/BasicCompiler.*` (tokenizer) | `src/BasicNativeCompiler.*` (**native**) |
|---|---|---|
| Output | tokenized program + launcher | **standalone 6502 machine code** |
| Runtime | the Applesoft **interpreter ROM** | **none** — only the graphics card |
| Speed of the program | same as the interpreter | **~20× faster** (integer/control code) |
| Coverage | full Applesoft (FP, SIN/SQR, …) | integer subset (FP = future phase) |
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

`src/BasicCompiler.{h,cpp}` — pure (`<string>/<vector>/<cstdint>` only), so it
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

Then update `targetTms()` / `targetGen2()` in `src/BasicCompiler.cpp`.

## Usage

### Standalone tool — `basicc`

```bash
# built by CMake (target basicc), or directly:
g++ -std=c++17 -I src tools/basicc.cpp src/BasicCompiler.cpp -o basicc

basicc --target tms  sketchs/basic_applesoft/3DHat.apf -o 3DHat_tms.hex
basicc --target gen2 sketchs/basic_applesoft/3DHat.apf -o 3DHat_gen2.hex
```

The output is a Wozmon-style hex dump (`Memory::loadHexDump` format) whose run
address is the launcher (`$0280`). To run it: boot the matching interpreter to
its `]` prompt, then load the hex and jump to `$0280`.

### Library

```cpp
#include "BasicCompiler.h"
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

`src/BasicNativeCompiler.*` is a **real native-code compiler**: it generates
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

Variables and arithmetic are **16-bit signed**. Supported: `+ - * /`, comparisons,
`AND/OR/NOT`, `ABS`, `FOR/NEXT`, `IF/THEN`, `GOTO`, `GOSUB/RETURN`, `END`, `REM`,
and integer graphics `HGR/HGR2`, `HCOLOR=`, `HPLOT` (point + `TO`-chains). Float
literals are rejected. **Phase 2 (future):** a standalone floating-point runtime
(`FADD/FMUL/.../SIN/SQR`) so float programs like `3DHat.apf` compile to native
code with no ROM either — the harder, larger half, where FP speed only improves if
the float library itself is faster than the ROM's.

### Use it

```bash
# emit assembly, then assemble + link to a standalone .bin (needs cc65):
build/basicc --native --card gen2 prog.bas -o prog.s
tools/basicc_native.sh gen2 prog.bas prog.bin       # BASIC -> standalone $0300 image

# run it with no interpreter (GEN2 card present):
pom1 --headless --preset 11 --load 0x0300:prog.bin --run 0x0300 --dump-gen2-frame out.png
```
