// =============================================================================
// cc65_bench.js — POM1 web-Bench glue: compile 6502 asm in the browser using the
// WASM cc65 toolchain (build-wasm/cc65/*.js) against the dev/ libraries preloaded
// into POM1's MEMFS. Built on the generic orchestration in cc65_wasm.js.
//
// `createBench(env)` takes an injectable filesystem so the SAME logic is
// verifiable in Node and runs unchanged in the browser:
//   env.readFile(absPath) -> Uint8Array        (browser: Module.FS.readFile)
//   env.listDir(absDir)   -> [absPath, ...]     (recursive file list)
//   env.getFactory(name)  -> Emscripten factory (browser: window[name])
//   env.CC65              -> the cc65_wasm.js module
//
// In the browser, shell.html wires env to POM1's Module.FS (dev/ is preloaded at
// /dev) and the window.{ca65,ld65,cc65,ar65} factories the <script> tags define.
// See doc/CC65_WASM.md.
// =============================================================================
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.POM1cc65Bench = factory();
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  function createBench(env) {
    const { readFile, listDir, getFactory, CC65 } = env;

    // Gather every file under each dir into a {absPath: Uint8Array} map, to seed
    // the tool's MEMFS (so `.include` resolves against the dev/ tree).
    function gather(dirs) {
      const files = {};
      for (const d of dirs) for (const p of listDir(d)) files[p] = readFile(p);
      return files;
    }

    // Assemble + link one .s program (the Bench's asm targets).
    //   source   : the editor text (string)
    //   opts.cfg : absolute path to the ld65 linker config under /dev
    //   opts.incDirs : absolute -I dirs under /dev (their files are seeded)
    //   opts.asmArgs / opts.ldArgs : extra flags
    async function buildAsm(source, opts) {
      opts = opts || {};
      const incDirs = opts.incDirs || [];
      const includes = gather(incDirs);
      const a = await CC65.assemble(getFactory, source, 'prog.s', includes, incDirs, opts.asmArgs);
      if (a.code !== 0 || !a.obj) return { code: a.code || 1, bin: null, log: '[ca65]\n' + a.log };
      const cfgData = readFile(opts.cfg);
      const cfgName = opts.cfg.slice(opts.cfg.lastIndexOf('/') + 1);
      const l = await CC65.link(getFactory, { '/prog.o': a.obj }, cfgData, cfgName, opts.ldArgs);
      const log = (a.log ? '[ca65]\n' + a.log + '\n' : '') + (l.log ? '[ld65]\n' + l.log : '');
      return { code: l.code, bin: l.bin, log };
    }

    return { buildAsm };
  }

  // Recursively list files under a dir of an Emscripten FS (browser helper).
  function listEmscriptenDir(FS, dir, out) {
    out = out || [];
    let entries;
    try { entries = FS.readdir(dir); } catch (e) { return out; }
    for (const name of entries) {
      if (name === '.' || name === '..') continue;
      const p = dir.replace(/\/$/, '') + '/' + name;
      const m = FS.stat(p);
      if (FS.isDir(m.mode)) listEmscriptenDir(FS, p, out);
      else out.push(p);
    }
    return out;
  }

  return { createBench, listEmscriptenDir };
}));
