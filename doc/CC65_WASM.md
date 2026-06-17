# cc65 in WebAssembly — running the DevBench asm/C path in the browser

**Status (2026-06-17): cc65 is bundled + callable in the web build; the Bench *button* wiring (async UI) is the last step.**

The WASM build now ships the cc65 toolchain and exposes it in-page as
**`window.POM1cc65`** — `await POM1cc65.buildAsm(src, {cfg, incDirs})` assembles +
links a 6502 program in the browser, byte-identical to native cc65 (verified). The
tools + the `dev/` library tree are bundled (below). What is NOT yet wired is the
ImGui Bench **Upload button** calling this — that needs the Bench's synchronous
`upload()` to become async (the cc65 modules instantiate via Promises, and POM1 is
a main-loop app, so a sync `EM_ASM`-await is unsafe). See "Remaining" below.

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

- **Bundled + page-wired (this turn).** `tools/build_cc65_wasm.sh --out build-wasm/cc65`
  builds the tools into the web output; `CMakeLists.txt` (under `if(EMSCRIPTEN)`)
  preloads the `dev/` toolchain (`dev/cc65`, `dev/lib`, `dev/apple1-videocard-lib`,
  ~1.8 MB) into POM1's MEMFS, exports `FS`, and POST_BUILD-copies
  `tools/cc65_wasm.js` + `tools/cc65_bench.js` next to `POM1.html`. `shell.html`
  loads them + the four tool `.js` and defines **`window.POM1cc65`**. Confirmed in
  the built bundle: 293 `/dev/*` files in the MEMFS manifest, `Module["FS"]`
  exported, glue served.
- **`tools/cc65_bench.js` glue verified byte-identical (Node).** `createBench({readFile,
  listDir,getFactory,CC65})` mirrors the preloaded `dev/` tree into a tool's MEMFS
  and runs `ca65|ld65` — the SAME code the browser runs (browser injects
  `Module.FS`; Node injects `node:fs`) — produced a `.bin` byte-for-byte equal to
  native cc65.

Reproduce: `tools/build_cc65_wasm.sh --out build-wasm/cc65`, then rebuild WASM
(`emcmake cmake .. && emmake make`). In the page console:
`await POM1cc65.buildAsm(src, {cfg:'/dev/cc65/apple1_4k.cfg', incDirs:['/dev/lib/apple1']})`.

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

1. ~~**Bundle.**~~ **Done.** `build-wasm/cc65/*.{js,wasm}` (built by
   `build_cc65_wasm.sh`), `cc65_wasm.js` + `cc65_bench.js` (POST_BUILD-copied), and
   the `dev/` toolchain (preloaded into MEMFS) all ship next to `POM1.html`. These
   are gitignored build outputs (regenerated); the hand-written sources live in
   `tools/`. `CMakeLists.txt` warns if `build-wasm/cc65/` is missing. The tool
   `.wasm` (~800 KB) is fetched lazily on first compile.

2. ~~**Page glue.**~~ **Done.** `shell.html` loads `cc65_wasm.js` + `cc65_bench.js`
   + the four `cc65/<tool>.js` (which set `window.{ca65,ld65,cc65,ar65}`) and defines
   **`window.POM1cc65`** with `available()` and `async buildAsm(src,{cfg,incDirs})`,
   reading the preloaded `dev/` out of `Module.FS`. Callable from the page/console
   today — this is the "cc65 access in WASM mode."

3. ~~**C++ seam — the Bench *button*.**~~ **Done (asm).** Instead of ASYNCIFY
   (which would add cost to the whole binary + risk the GLFW main loop), the async
   is handled by a **poll/state-machine** — lowest blast radius, desktop stays
   fully synchronous:
   - `BuildResult` gained a `pending` flag; `IBenchHost` gained `pollBuild()`
     (default no-op). `CodeBench` remembers `verify()`/`upload()` returning
     `pending=true` and calls `pollBuild()` every frame until it resolves, then
     applies the final result. On desktop `pending` is never set → inert.
   - `Pom1BenchHost` (WASM): `available()` exposes the asm targets (dual-4k / GEN2
     HGR / GEN2 TXT) + hex/raw; `build()` kicks off `window.POM1cc65.buildAsm` via
     `EM_ASM` (writing `.bin`/`.log` to MEMFS + a `Module.__benchJob` state) and
     returns `pending=true`; `pollBuild()` reads `Module.__benchJob`, and on
     completion reads the `.bin`/`.log` out of MEMFS, routes the log through the
     existing `parseErrorMarkers`/`humanizeCc65`, and `loadBinary`+runs at the cfg
     entry. `toolchainReady`/`Hint`/`headerNote` updated.
   - *(EM_ASM gotcha: no top-level commas in the JS body — only `()` protects them
     from the C preprocessor, not `{}`/`[]`.)*
   - **Remaining here: C targets.** `build()` returns "desktop-only" for `mode==3`
     and `available()` doesn't expose them (need item 4). The **TMS9918 asm** target
     (ROM-flash path) is also still desktop-only.

4. **C path runtime libs + version pin.** The asm path needs no libraries. The C
   path links cc65's C runtime; the WASM `cc65` is **2.19 git master**, so bundle
   the matching 2.19 `.lib`/runtime (the `sp`→`c_sp` rename means 2.18 libs won't
   link). Pin `build_cc65_wasm.sh` to the cc65 revision whose libs you bundle.
   POM1's C targets use custom `apple1c`/`gen2c`/videocard libs built from source
   at upload time — those compile through the same WASM `cc65`/`ca65`, so the main
   external dependency is cc65's own crt0/libc for `-t none`.

5. **Verify in a browser.** Build (`tools/build_cc65_wasm.sh --out build-wasm/cc65`,
   then `emcmake cmake .. && emmake make`), open `POM1.html`, pick an asm target in
   the DevBench, and hit Run — confirm the toolbar shows "Building (web cc65)…"
   then the program loads + runs. (Desktop + WASM both compile clean; the glue is
   Node-verified byte-identical; only this in-browser click-through is unexercised
   headlessly.) The C path + the in-browser run are the last unverified bits.

## Notes

- One module instance per tool invocation (the tools `exit()`; `EXIT_RUNTIME=1`).
  Instantiation is cheap; the glue creates a fresh instance per `callMain`.
- MEMFS is mounted at `/cc65` for the tools' compiled-in default search paths
  (`-DCA65_INC` etc.); the Bench passes explicit `-I`/`-C` so those defaults are
  only a fallback.
- Targets `web,worker,node` are enabled, so the same modules drive the Node
  self-test and the browser.
