# dev/lib/basicrt — native BASIC compiler runtime

*[← dev/lib index](../README.md)*  ·  compiler: [`../../../doc/BASIC_COMPILER.md`](../../../doc/BASIC_COMPILER.md)  ·  driver: [`../../../tools/basicc_native.sh`](../../../tools/basicc_native.sh)

Runtime + linker configs for the **native** Applesoft→6502 compiler
(`src/BasicCompilerApplesoft.{h,cpp}`, tool `basicc_native`). The compiler emits
6502 that **calls** the `rt_*` / `fp_*` routines here; a compiled program links
one card runtime + the shared integer/float cores into a **standalone binary**
that runs with **no Applesoft interpreter** — only the graphics card. This is a
distinct track from the ROM-tokenising `basicc` (see `doc/BASIC_COMPILER.md`);
everything in this directory is the interpreter-free path.

## Files

| File | Model | Role |
|---|---|---|
| [`basicrt_math.inc`](basicrt_math.inc) | textual `.include` | card-agnostic 16-bit integer core (`rt_mul`/`rt_div`/`rt_cmp16`/`rt_print`…). Feature-gated per `-D RT_xxx` so a program assembles only what it imports. Caller must declare the ZP ABI (`rt_a`, `rt_b`) + scratch (`m_prod`, `m_rem`, `m_sign`). |
| [`basicrt_gen2.s`](basicrt_gen2.s) | object (`.o`) | GEN2 HGR card runtime: `rt_hgr`/`rt_hcolor`/`rt_plot`/`rt_line` with a **full 16-bit X (0..279)** — wider than the 8-bit project `plot_pixel` lib. Reuses the lib's Y-indexed scanline base tables. |
| [`basicrt_tms.s`](basicrt_tms.s) | object (`.o`) | TMS9918 card runtime, same `rt_*` ABI. Hi-res wraps `tms9918m2.asm` (Mode-II); lo-res drives Multicolor mode self-contained (needs only `tms9918_pad18`). |
| [`basicrt_float.s`](basicrt_float.s) | object (`.o`) | standalone **binary32** (IEEE-754 single) software float — the FP-phase foundation, no Applesoft ROM. `fp_fromint16`/`fp_toint16`/`fp_add`/`fp_sub`/`fp_mul`/`fp_div`/`fp_cmp` (+ gated `fp_int`/`fp_sqrt`/`fp_sin`). Operands in ZP slots `FA`/`FB`, result in `FA`. |

## ABI in one line

- **Integer / graphics**: 16-bit operands in ZP `rt_a`/`rt_b`, plot state in
  `rt_px`/`rt_py`/`rt_x0..rt_y1`. Every routine is `import`ed by the compiled
  program; `ld65` dead-strips the unused ones.
- **Float**: 4-byte little-endian binary32 in `FA`/`FB`; internally unpacked to
  `{sign, signed-16 exponent E, 24-bit significand}` so `value = SG · 2^E`, then
  repacked. Transcendentals are `-D FP_INT`/`FP_SQRT`/`FP_SIN` gated — they link
  only when the program uses them.

## Linker configs

The compiler picks one `.cfg` per (card × program-kind); `basicc_native.sh`
wires the choice.

| Config | Image base | Use |
|---|---|---|
| [`basicc_native.cfg`](basicc_native.cfg) | `$0300` | default — HGR programs (GEN2 or TMS). `basic_main` is emitted first, so entry == MAIN start (`$0300`). |
| [`basicc_native_gen2_lores.cfg`](basicc_native_gen2_lores.cfg) | `$0C00` | **GEN2 lo-res only**: the Apple-II lo-res page-2 framebuffer sits at `$0800–$0BFF`, so a `$0300` image that grows past `$0800` would overwrite itself on `GR`/`PLOT`. Base `$0C00` keeps `$0400–$0BFF` clear. TMS keeps pixels in VRAM, so it never needs this. |
| [`basicrt_float.cfg`](basicrt_float.cfg) | `$0300` | the `basicrt_float.s` test image; float working registers occupy ZP `$10–$BF`. |

> **GEN2 RAM squeeze**: GEN2 standalone code shares RAM with the `$2000` HGR
> framebuffer, so the image must fit `$0300–$1FFF` (BSS relocated to `$0200`).
> The compiler peephole's ~640 B saving is what lets `3DHat.apf` fit — see
> `basic_native_run` in `tests/CMakeLists.txt`.

## Tests

All cc65-gated (skip 77 without the toolchain):

- **`basic_float_runtime`** — [`../../../tests/basic_float_runtime_test.cpp`](../../../tests/basic_float_runtime_test.cpp): assembles `basicrt_float.s` (with `-D FP_INT/FP_SQRT/FP_SIN`) and checks every `fp_*` op against the host IEEE `float`/`sqrtf`/`sinf` over a grid + random pairs.
- **`basic_native_codegen` / `basic_native_run` / `basic_native_bench`** — the end-to-end compiler pins: build the standalone binary, run it vs the interpreter, assert same framebuffer + native faster; `3DHat.apf` runs native on both cards.

See [`doc/BASIC_COMPILER.md`](../../../doc/BASIC_COMPILER.md) for the compiler
pipeline and [`doc/CC65_WASM.md`](../../../doc/CC65_WASM.md) for the WASM path.
