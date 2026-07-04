// =============================================================================
// cc65_wasm.js — drive the WASM cc65 toolchain (ca65 / ld65 / cc65 / ar65) in
// the browser (or Node) so the POM1 DevBench can assemble + link 6502 programs
// without a subprocess. Built by tools/build_cc65_wasm.sh; see doc/CC65_WASM.md.
//
// Each tool is a MODULARIZEd Emscripten module with its own MEMFS. The flow:
//   1. instantiate a fresh module per invocation (EXIT_RUNTIME=1, one-shot),
//   2. write inputs into its MEMFS,
//   3. callMain([...argv]) (catch the ExitStatus the tool throws on exit),
//   4. read the output file(s) back out.
// Object files are carried between tools by reading them out of one module's FS
// and writing them into the next — verified byte-identical to native cc65.
//
// Environment-agnostic: the caller supplies `loadFactory(name)` returning a
// Promise<EmscriptenModuleFactory> for 'ca65'|'ld65'|'cc65'|'ar65'. In Node
// that's `require(`${dir}/${name}.js`)`; in the browser it's a dynamic import or
// a window global the page set up (the .js are served next to POM1.html).
// =============================================================================
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CC65 = factory();
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  function dirname(p) { const i = p.lastIndexOf('/'); return i <= 0 ? '/' : p.slice(0, i); }
  function mkdirp(FS, p) {
    let cur = '';
    for (const part of p.split('/')) { if (!part) continue; cur += '/' + part; try { FS.mkdir(cur); } catch (e) { /* exists */ } }
  }

  // Run one cc65 tool. inFiles: {path: Uint8Array|string}. outPaths: [path,...].
  // Returns {code, stdout, stderr, files:{path:Uint8Array|null}}.
  async function runTool(getFactory, name, argv, inFiles, outPaths, opts) {
    opts = opts || {};
    const factory = await getFactory(name);
    const out = [], err = [];
    const mod = await factory({
      noInitialRun: true,
      print: (s) => out.push(s),
      printErr: (s) => err.push(s),
    });
    const FS = mod.FS;
    for (const [path, data] of Object.entries(inFiles || {})) {
      mkdirp(FS, dirname(path));
      FS.writeFile(path, typeof data === 'string' ? data : new Uint8Array(data));
    }
    let code = 0;
    try { mod.callMain(argv); }
    catch (e) {
      if (e && e.name === 'ExitStatus') code = e.status;
      else if (typeof e === 'number') code = e;
      else { code = 1; err.push(String(e && e.message || e)); }
    }
    const files = {};
    for (const p of (outPaths || [])) {
      try { files[p] = FS.readFile(p); } catch (e) { files[p] = null; }
    }
    return { code, stdout: out.join('\n'), stderr: err.join('\n'), files };
  }

  // Assemble one .s -> .o.   includes: {'/inc/foo.inc': data, ...}; incDirs: ['/inc'].
  async function assemble(getFactory, src, srcName, includes, incDirs, extraArgs) {
    const inFiles = Object.assign({ ['/' + srcName]: src }, includes || {});
    const argv = [];
    for (const d of (incDirs || [])) argv.push('-I', d);
    for (const a of (extraArgs || [])) argv.push(a);
    argv.push('-o', '/out.o', '/' + srcName);
    const r = await runTool(getFactory, 'ca65', argv, inFiles, ['/out.o']);
    return { code: r.code, obj: r.files['/out.o'], log: r.stderr };
  }

  // Link .o[] + cfg -> .bin.  objs: {'/a.o':data}; cfg/cfgName the linker config.
  async function link(getFactory, objs, cfg, cfgName, extraArgs) {
    const inFiles = Object.assign({ ['/' + cfgName]: cfg }, objs);
    const argv = ['-C', '/' + cfgName];
    for (const a of (extraArgs || [])) argv.push(a);
    for (const p of Object.keys(objs)) argv.push(p);
    argv.push('-o', '/out.bin');
    const r = await runTool(getFactory, 'ld65', argv, inFiles, ['/out.bin']);
    return { code: r.code, bin: r.files['/out.bin'], log: r.stderr };
  }

  // Compile one .c -> .s (cc65). includes: {'/inc/foo.h':data}; incDirs.
  async function compileC(getFactory, src, srcName, includes, incDirs, extraArgs) {
    const inFiles = Object.assign({ ['/' + srcName]: src }, includes || {});
    const argv = ['-t', 'none'];
    for (const d of (incDirs || [])) argv.push('-I', d);
    for (const a of (extraArgs || [])) argv.push(a);
    argv.push('-o', '/out.s', '/' + srcName);
    const r = await runTool(getFactory, 'cc65', argv, inFiles, ['/out.s']);
    return { code: r.code, asm: r.files['/out.s'], log: r.stderr };
  }

  // High level: assemble a single .s and link it (the Bench's asm targets).
  // opts: {source, srcName, includes, incDirs, cfg, cfgName, asmArgs, ldArgs}
  async function buildAsmProgram(getFactory, opts) {
    const a = await assemble(getFactory, opts.source, opts.srcName || 'prog.s',
      opts.includes, opts.incDirs, opts.asmArgs);
    if (a.code !== 0 || !a.obj) return { code: a.code || 1, bin: null, log: '[ca65]\n' + a.log };
    const l = await link(getFactory, { '/prog.o': a.obj }, opts.cfg, opts.cfgName || 'prog.cfg', opts.ldArgs);
    return { code: l.code, bin: l.bin, log: (a.log ? '[ca65]\n' + a.log + '\n' : '') + (l.log ? '[ld65]\n' + l.log : '') };
  }

  return { runTool, assemble, link, compileC, buildAsmProgram };
}));
