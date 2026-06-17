# cc65 in WebAssembly — running the DevBench asm/C path in the browser

**Status (2026-06-17): foundation built + proven; browser wiring scaffolded, not yet shipped.**

The desktop DevBench shells out to native `ca65`/`ld65`/`cc65`/`cl65`. The web
(Emscripten) build has no subprocesses, so today it exposes only the Wozmon-hex
target and shows a "desktop only — download the app" CTA (`IBenchHost::headerNote()`).
This document is the plan + the proven pieces for making the full asm/C Bench run
in-browser by compiling the cc65 toolchain itself to WASM (the approach
8bitworkshop uses).

## What is done and verified

- **`tools/build_cc65_wasm.sh`** compiles the cc65 tools to WASM with `emcc`:
  `ca65` (209 KB wasm), `ld65` (142 KB), `cc65` (419 KB), `ar65` (38 KB). Each is
  a MODULARIZEd module with MEMFS, `callMain`, and `FS` exported.
- **`tools/cc65_wasm.js`** orchestrates them: instantiate a tool, populate its
  MEMFS, `callMain([...argv])`, read the output file; carry `.o` files between
  tools. Works in Node and the browser (caller supplies `loadFactory(name)`).
- **Byte-identical to native (asm path).** A real Apple-1 program assembled
  (`ca65 -I … -o prog.o prog.s`) then linked (`ld65 -C apple1_4k.cfg …`) entirely
  in WASM produces a `.bin` **byte-for-byte equal** to the native cc65 build.
  Verified with the homebrew `ca65`/`ld65` 2.18 on the same source.
- **`cc65` (C compiler) runs in WASM** and emits correct 6502 asm. Its output
  differs from homebrew only by the cc65 **version** (the WASM build tracks git
  master 2.19: the `sp`→`c_sp` zero-page-stack rename, label counter, version
  comment) — not a WASM defect.

Reproduce: `tools/build_cc65_wasm.sh --out build-wasm/cc65` then drive it with the
Node self-test pattern in this doc's history (see `tools/cc65_wasm.js`).

## Architecture

```
 POM1.wasm  (C++ Bench)                     served next to POM1.html:
 ┌─────────────────────┐   EM_ASM/EM_JS     cc65/ca65.{js,wasm}
 │ Pom1BenchHost::      │  ───────────────▶  cc65/ld65.{js,wasm}
 │   compile()  (WASM)  │                    cc65/cc65.{js,wasm}
 │                      │  ◀───────────────  cc65/ar65.{js,wasm}
 │  writes .bin to RAM, │   bin bytes        cc65_wasm.js  (glue)
 │  runs like Wozmon-hex│                    cc65-data/  (cfgs + dev/lib includes)
 └─────────────────────┘
```

The cc65 tools are **separate** Emscripten modules (their own MEMFS), loaded by
JS on demand — NOT linked into POM1.wasm and NOT preloaded into POM1's MEMFS. The
C++ Bench hands the user source + target to a JS function (`cc65_wasm.js`), which
runs the pipeline against an in-memory copy of the needed cfg/include files and
returns the `.bin`; the Bench then loads it into emulated RAM exactly as it does
for a Wozmon-hex upload.

## Remaining integration work (the browser wiring)

1. **Bundle.** Ship `build-wasm/cc65/*.{js,wasm}`, `tools/cc65_wasm.js`, and a
   minimal `cc65-data/` (the linker `.cfg`s from `dev/cc65/` + the asm includes
   from `dev/lib/**` the targets need) next to `POM1.html`. In `CMakeLists.txt`
   under `if(EMSCRIPTEN)`: run `build_cc65_wasm.sh` (or expect a prebuilt
   `build-wasm/cc65/`) and `POST_BUILD`-copy these into the output. Add
   `.gitignore` exceptions (`!build-wasm/cc65/`, `!build-wasm/cc65_wasm.js`) or
   keep them build-only. The tool `.wasm` (~800 KB total) is a separate
   on-demand fetch, so it doesn't bloat the initial POM1 download.

2. **Page glue.** In `build-wasm/shell.html`, load `cc65_wasm.js` and define
   `window.POM1_cc65 = { loadFactory: (name) => import(`./cc65/${name}.js`).then(m=>m.default), dataUrl: './cc65-data/' }`
   so the C++ side can reach it.

3. **C++ seam (`src/Pom1BenchHost.cpp`, all under `#if POM1_IS_WASM`):**
   - `available()` / `defaultTargetIndex()`: stop hard-gating to target 8 — expose
     the asm (and, once the C runtime is bundled, C) targets, same as desktop.
   - `compile()`: replace the "Compile in the desktop build" early-return with a
     call into the glue. Because the cc65 modules are async JS, drive it with an
     `EM_ASM`/`EM_JS` that runs `CC65.buildAsmProgram(...)` and resolves to the
     `.bin`; surface the result to C++ via `emscripten_*` (a worker + a synchronous
     wait, or restructure `compile()` to be async/callback). The glue's `log` maps
     to the existing `parseErrorMarkers`/console plumbing.
   - `headerNote()`: drop the "desktop only" CTA on WASM (or note "asm/C compiled
     in-browser via WASM cc65").

4. **C path runtime libs + version pin.** The asm path needs no libraries. The C
   path links cc65's C runtime; the WASM `cc65` is **2.19 git master**, so bundle
   the matching 2.19 `.lib`/runtime (the `sp`→`c_sp` rename means 2.18 libs won't
   link). Pin `build_cc65_wasm.sh` to the cc65 revision whose libs you bundle.
   POM1's C targets use custom `apple1c`/`gen2c`/videocard libs built from source
   at upload time — those compile through the same WASM `cc65`/`ca65`, so the main
   external dependency is cc65's own crt0/libc for `-t none`.

5. **Verify in a browser.** Build (`emcmake cmake .. && emmake make`), open
   `POM1.html`, and confirm an asm sketch + a C sketch compile, load, and run —
   the piece this session could not exercise headlessly.

## Notes

- One module instance per tool invocation (the tools `exit()`; `EXIT_RUNTIME=1`).
  Instantiation is cheap; the glue creates a fresh instance per `callMain`.
- MEMFS is mounted at `/cc65` for the tools' compiled-in default search paths
  (`-DCA65_INC` etc.); the Bench passes explicit `-I`/`-C` so those defaults are
  only a fallback.
- Targets `web,worker,node` are enabled, so the same modules drive the Node
  self-test and the browser.
