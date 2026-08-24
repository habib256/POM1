#!/usr/bin/env node
// Browser smoke test for the published WASM page.
//
//   node tools/wasm_smoke.mjs <site-dir> [page-path]     # default: build-wasm/POM1.html
//   node tools/wasm_smoke.mjs --self-test
//
// `Deploy Pages` used to upload the site without ever loading it: a JS
// exception, a WebGL2 init failure or a truncated POM1.data all deployed
// green. This script serves the assembled site directory over local HTTP,
// loads the page in headless Chromium (Playwright) and asserts three things:
//
//   1. the boot completes — #bootSplash gains .hidden, which only happens when
//      the C++ side calls pom1FirstFrameReady() after the first real
//      glfwSwapBuffers (a stronger signal than onRuntimeInitialized: the
//      Apple-1 screen has actually been painted);
//   2. nothing failed on the way — no uncaught page exception, no console
//      error, no failed asset request, and the #err overlay (Module.onAbort +
//      unhandledrejection) stayed hidden;
//   3. the canvas is not black — the composited page is screenshotted and at
//      least MIN_LIT_FRACTION of the canvas pixels must be lit.
//
// --self-test proves the checker can FAIL, not just pass (a checker that
// never fires reads as coverage while providing none — same philosophy as
// lock_order_smoke): it generates a healthy WebGL2 page, an all-black one, one
// that throws, and one that shows the #err overlay, and asserts PASS/FAIL/
// FAIL/FAIL. It needs no WASM build, so it also proves the CI browser can do
// WebGL2 at all — separating "the runner lost WebGL" from "POM1 broke".
//
// Requires `playwright` resolvable from the CWD (npm install --no-save
// playwright && npx playwright install --with-deps chromium).

import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { chromium } from 'playwright';

const MIN_LIT_FRACTION = 0.005; // >=0.5 % of canvas pixels brighter than near-black
const BOOT_TIMEOUT_MS = Number(process.env.POM1_SMOKE_TIMEOUT_MS || 180_000);
const SHOT_PATH = process.env.POM1_SMOKE_SHOT || 'wasm-smoke.png';

// ── tiny static server ──────────────────────────────────────────────────────
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm', // wrong MIME would silently disable streaming compile
  '.data': 'application/octet-stream',
  '.png': 'image/png',
  '.json': 'application/json',
};

function serveDir(rootDir) {
  const root = path.resolve(rootDir);
  const server = http.createServer((req, res) => {
    const urlPath = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const file = path.normalize(path.join(root, urlPath));
    if (!file.startsWith(root)) { res.writeHead(403); res.end(); return; }
    fs.readFile(file, (err, buf) => {
      if (err) { res.writeHead(404); res.end('not found: ' + urlPath); return; }
      res.writeHead(200, {
        'Content-Type': MIME[path.extname(file)] || 'application/octet-stream',
        'Content-Length': buf.length,
      });
      res.end(buf);
    });
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () =>
      resolve({ server, base: `http://127.0.0.1:${server.address().port}` }));
  });
}

// ── the check itself ────────────────────────────────────────────────────────
async function runCheck(browser, url, { shotPath } = {}) {
  const failures = [];
  const page = await browser.newPage();
  page.on('pageerror', (e) => failures.push(`uncaught page exception: ${e.message}`));
  page.on('console', (m) => {
    if (m.type() === 'error') failures.push(`console error: ${m.text()}`);
  });
  page.on('requestfailed', (r) => {
    const why = r.failure()?.errorText || 'unknown';
    if (why !== 'net::ERR_ABORTED') // aborts happen on teardown, not on breakage
      failures.push(`request failed: ${r.url()} (${why})`);
  });

  let stats = null;
  try {
    const resp = await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 60_000 });
    if (!resp || !resp.ok())
      failures.push(`page load HTTP ${resp ? resp.status() : 'no response'}`);

    // Boot completed (#bootSplash.hidden) — or died (#err visible)?
    let outcome = 'timeout';
    try {
      const h = await page.waitForFunction(() => {
        const err = document.getElementById('err');
        if (err && getComputedStyle(err).display !== 'none') return 'error';
        const splash = document.getElementById('bootSplash');
        if (splash && splash.classList.contains('hidden')) return 'ready';
        return false;
      }, undefined, { timeout: BOOT_TIMEOUT_MS });
      outcome = await h.jsonValue();
    } catch {
      failures.push(`boot did not complete within ${BOOT_TIMEOUT_MS} ms ` +
                    '(#bootSplash never gained .hidden)');
    }
    if (outcome === 'error') {
      const txt = await page.locator('#err').textContent();
      failures.push(`#err overlay shown: ${(txt || '').trim()}`);
    }

    if (outcome === 'ready') {
      await page.waitForTimeout(500); // let a couple more frames land
      // The canvas is WebGL without preserveDrawingBuffer, so read pixels from
      // a compositor screenshot fed back into a 2D canvas — not from the GL
      // context, which reads back black between frames.
      const shot = await page.locator('#canvas').screenshot();
      stats = await page.evaluate(async (b64) => {
        const img = new Image();
        img.src = 'data:image/png;base64,' + b64;
        await img.decode();
        const c = document.createElement('canvas');
        c.width = img.width; c.height = img.height;
        const ctx = c.getContext('2d');
        ctx.drawImage(img, 0, 0);
        const d = ctx.getImageData(0, 0, c.width, c.height).data;
        let lit = 0;
        for (let i = 0; i < d.length; i += 4)
          if (d[i] > 24 || d[i + 1] > 24 || d[i + 2] > 24) lit++;
        return { w: c.width, h: c.height, lit, total: d.length / 4 };
      }, shot.toString('base64'));
      if (stats.total === 0) {
        failures.push('canvas has zero size');
      } else if (stats.lit / stats.total < MIN_LIT_FRACTION) {
        failures.push(`canvas is black: ${stats.lit}/${stats.total} lit pixels ` +
                      `(${(100 * stats.lit / stats.total).toFixed(3)} % < ` +
                      `${100 * MIN_LIT_FRACTION} %)`);
      }
    }

    if (shotPath) {
      try { await page.screenshot({ path: shotPath, fullPage: false }); }
      catch { /* best effort — the shot is diagnostics, not an assertion */ }
    }
  } finally {
    await page.close();
  }
  return { ok: failures.length === 0, failures, stats };
}

// ── self-test fixtures ──────────────────────────────────────────────────────
// Minimal pages that mimic shell.html's observable contract: a #canvas, a
// #bootSplash that gains .hidden on "first frame", a #err overlay.
function fixture({ clearColor, throwError, showErr }) {
  return `<!doctype html><meta charset="utf-8">
<style>#err{display:none;background:#4a1a1a;color:#ffe}</style>
<div id="bootSplash">PLEASE WAIT</div>
<div id="err"></div>
<canvas id="canvas" width="320" height="200"></canvas>
<script>
  const gl = document.getElementById('canvas').getContext('webgl2');
  if (!gl) { document.getElementById('err').style.display = 'block';
             document.getElementById('err').textContent = 'no webgl2'; }
  else {
    gl.clearColor(${clearColor}); gl.clear(gl.COLOR_BUFFER_BIT);
    requestAnimationFrame(() => {
      gl.clear(gl.COLOR_BUFFER_BIT);
      ${showErr ? `document.getElementById('err').style.display = 'block';
      document.getElementById('err').textContent = 'POM1 failed while loading';`
                : `document.getElementById('bootSplash').classList.add('hidden');`}
      ${throwError ? `setTimeout(() => { throw new Error('boom'); }, 0);` : ''}
    });
  }
</script>`;
}

async function selfTest(browser) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pom1-smoke-'));
  const cases = [
    { name: 'good',    expectOk: true,  html: fixture({ clearColor: '0.9,0.5,0.1,1' }) },
    { name: 'black',   expectOk: false, html: fixture({ clearColor: '0,0,0,1' }) },
    { name: 'jserror', expectOk: false, html: fixture({ clearColor: '0.9,0.5,0.1,1', throwError: true }) },
    { name: 'aborted', expectOk: false, html: fixture({ clearColor: '0.9,0.5,0.1,1', showErr: true }) },
  ];
  for (const c of cases) fs.writeFileSync(path.join(dir, c.name + '.html'), c.html);

  const { server, base } = await serveDir(dir);
  let allGood = true;
  try {
    for (const c of cases) {
      const r = await runCheck(browser, `${base}/${c.name}.html`);
      const verdict = r.ok === c.expectOk ? 'OK' : 'SELF-TEST FAILURE';
      if (r.ok !== c.expectOk) allGood = false;
      console.log(`[smoke:self-test] ${c.name}: expected ${c.expectOk ? 'pass' : 'fail'}, ` +
                  `got ${r.ok ? 'pass' : 'fail'} → ${verdict}`);
      for (const f of r.failures) console.log(`    · ${f}`);
    }
  } finally {
    server.close();
    fs.rmSync(dir, { recursive: true, force: true });
  }
  return allGood;
}

// ── main ────────────────────────────────────────────────────────────────────
const args = process.argv.slice(2);
if (args.length === 0) {
  console.error('usage: node tools/wasm_smoke.mjs <site-dir> [page-path] | --self-test');
  process.exit(2);
}

const browser = await chromium.launch({
  // Recent Chrome refuses software WebGL without this flag; on a GPU-less CI
  // runner that would mean "no WebGL2" and a page that dies before POM1 runs.
  args: ['--enable-unsafe-swiftshader'],
});

let ok = false;
try {
  if (args[0] === '--self-test') {
    ok = await selfTest(browser);
    console.log(ok ? '[smoke] self-test passed — the checker both passes and fails'
                   : '[smoke] SELF-TEST FAILED — the checker is broken, fix it before trusting it');
  } else {
    const siteDir = args[0];
    const pagePath = args[1] || 'build-wasm/POM1.html';
    const { server, base } = await serveDir(siteDir);
    try {
      const url = `${base}/${pagePath}`;
      console.log(`[smoke] loading ${url} (timeout ${BOOT_TIMEOUT_MS} ms)`);
      const r = await runCheck(browser, url, { shotPath: SHOT_PATH });
      if (r.stats)
        console.log(`[smoke] canvas ${r.stats.w}x${r.stats.h}, ` +
                    `${r.stats.lit}/${r.stats.total} lit pixels ` +
                    `(${(100 * r.stats.lit / r.stats.total).toFixed(1)} %)`);
      for (const f of r.failures) console.log(`[smoke] FAIL: ${f}`);
      ok = r.ok;
      console.log(ok ? '[smoke] PASS — the page boots, paints, and threw nothing'
                     : `[smoke] FAILED — see above (screenshot: ${SHOT_PATH})`);
    } finally {
      server.close();
    }
  }
} finally {
  await browser.close();
}
process.exit(ok ? 0 : 1);
