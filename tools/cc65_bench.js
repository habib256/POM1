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

    // Compile + link a C program (the Bench's C targets): cc65 each .c -> .s,
    // ca65 each .s (+ each hand-written .s) -> .o, ld65 all .o + none.lib + cfg.
    //   source   : the editor text (the main .c)
    //   opts.cSources  : [{path,name}] extra .c to compile (lib runtime, /dev)
    //   opts.asmSources: [{path,name}] hand-written .s to assemble (/dev)
    //   opts.incDirs   : -I dirs (project headers), under /dev
    //   opts.cfg       : ld65 linker config (/dev)
    //   opts.runtimeLib: the -t none runtime (default /cc65/lib/none.lib)
    // Needs the cc65 runtime preloaded at /cc65 (asminc for ca65 .macpack,
    // include for cc65 system headers, lib/none.lib for the link).
    async function buildC(source, opts) {
      opts = opts || {};
      const incDirs = opts.incDirs || [];
      const incFiles = gather(incDirs);                  // project headers (/dev)
      const asminc = gather(['/cc65/asminc']);           // ca65 .macpack files
      const sysInc = gather(['/cc65/include']);          // cc65 system headers
      let log = '';
      const objs = {};

      async function compileC(name, data) {
        const argv = ['-t', 'none', '-O'];
        for (const d of (opts.defines || [])) argv.push('-D', d);
        for (const d of incDirs) argv.push('-I', d);
        const sName = '/' + name.replace(/\.c$/, '.s');
        argv.push('-o', sName, '/' + name);
        const inF = Object.assign({ ['/' + name]: data }, incFiles, sysInc);
        const r = await CC65.runTool(getFactory, 'cc65', argv, inF, [sName]);
        return { code: r.code, s: r.files[sName], sName, log: r.stderr };
      }
      async function assembleS(name, data) {
        const oName = '/' + name.replace(/\.s$/, '.o');
        const inF = Object.assign({ ['/' + name]: data }, asminc, incFiles);
        const r = await CC65.runTool(getFactory, 'ca65', ['-o', oName, '/' + name], inF, [oName]);
        return { code: r.code, o: r.files[oName], oName, log: r.stderr };
      }

      // user source + extra .c: compile then assemble
      const cList = [{ name: opts.srcName || 'main.c', data: source }]
        .concat((opts.cSources || []).map(s => ({ name: s.name, data: readFile(s.path) })));
      for (const c of cList) {
        const cr = await compileC(c.name, c.data);
        if (cr.code !== 0 || !cr.s) return { code: cr.code || 1, bin: null, log: log + '[cc65 ' + c.name + ']\n' + cr.log };
        if (cr.log) log += '[cc65 ' + c.name + ']\n' + cr.log + '\n';
        const ar = await assembleS(c.name.replace(/\.c$/, '.s'), cr.s);
        if (ar.code !== 0 || !ar.o) return { code: ar.code || 1, bin: null, log: log + '[ca65 ' + c.name + ']\n' + ar.log };
        objs[ar.oName] = ar.o;
      }
      // hand-written .s sources
      for (const a of (opts.asmSources || [])) {
        const ar = await assembleS(a.name, readFile(a.path));
        if (ar.code !== 0 || !ar.o) return { code: ar.code || 1, bin: null, log: log + '[ca65 ' + a.name + ']\n' + ar.log };
        objs[ar.oName] = ar.o;
      }
      // link
      const cfgName = opts.cfg.slice(opts.cfg.lastIndexOf('/') + 1);
      const inF = Object.assign(
        { ['/' + cfgName]: readFile(opts.cfg), '/none.lib': readFile(opts.runtimeLib || '/cc65/lib/none.lib') },
        objs);
      const argv = ['-C', '/' + cfgName].concat(Object.keys(objs)).concat(['/none.lib', '-o', '/out.bin']);
      const l = await CC65.runTool(getFactory, 'ld65', argv, inF, ['/out.bin']);
      log += (l.stderr ? '[ld65]\n' + l.stderr + '\n' : '') + (l.code === 0 ? '[ok] compiled + linked (web cc65 C)\n' : '');
      return { code: l.code, bin: l.files['/out.bin'], log };
    }

    return { buildAsm, buildC };
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
