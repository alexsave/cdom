// The SIMD pick (cdom.mk's APP_SIMD, shim/cdom.js start()), end to end in headless Chrome on the pages tests/pick/run.sh
// builds: bench/image's app with its scalar module (app.wasm) and its SIMD twin (app.simd.wasm), bundled and as separate
// files, each with the readable shim and with the minified one apps inline. For every page:
//   - the real Chrome (which has wasm SIMD) fetches and compiles the SIMD module, and only it;
//   - the same page with WebAssembly.validate answering false for the probe (a browser without wasm SIMD) gets the
//     scalar module, and only it.
// "Which module" is the SHA-256 of the bytes the page fetched for it (a wrapper of fetch set before the page's first
// script, so the page is the one apps ship), and, separate, the files its resource timing lists. Then the two picks give
// byte-identical pixels on bench/image's four inputs (bench/image/resize.mjs, as bench/image/equal.mjs compares the two
// builds), and the probe's bytes in cdom.js are what binaryen makes of tools/simd-probe.wat.
// Usage: node tools/pick.mjs [dir=tests/out/pick]   (dir/inputs holds bench/image/inputs.py's files). Exit 1 on any failure.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { launch, sleep, FETCHED } from './cdp.mjs';
import { resizeHash } from '../bench/image/resize.mjs';

const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const dir = path.resolve(process.argv[2] || path.join(top, 'tests/out/pick'));
const types = { '.html': 'text/html', '.wasm': 'application/wasm' };
const srv = http.createServer((q, r) => {
    const f = path.join(dir, decodeURIComponent(q.url.split(/[?#]/)[0]));
    fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' }); r.end(b); });
}).listen(0);
const base = `http://127.0.0.1:${srv.address().port}`;
const sha = (f) => crypto.createHash('sha256').update(fs.readFileSync(path.join(dir, f))).digest('hex');
const want = { scalar: sha('app.wasm'), simd: sha('app.simd.wasm') };
const name = (h) => h === want.scalar ? 'app.wasm' : h === want.simd ? 'app.simd.wasm' : `bytes of neither (${h.slice(0, 12)})`;
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
setTimeout(() => { console.log('FAIL no result within 300 s'); process.exit(1); }, 300000).unref();

// Before the page's first script: every fetch of a module records the SHA-256 of its bytes; with ?nosimd in the URL,
// WebAssembly.validate answers false for a module under 64 bytes (the probe is 29), as a browser without wasm SIMD does.
const INIT = `(() => {
  const hex = (d) => [...new Uint8Array(d)].map((v) => v.toString(16).padStart(2, '0')).join('');
  const f = window.fetch;
  window.__mods = [];
  window.fetch = function (u) {
    const p = f.apply(this, arguments), s = String(u);
    if (/\\.wasm$|^data:application\\/wasm/.test(s)) __mods.push(p.then((r) => r.clone().arrayBuffer()).then((b) => crypto.subtle.digest('SHA-256', b)).then(hex));
    return p;
  };
  if (/[?&]nosimd/.test(location.search)) {
    const v = WebAssembly.validate;
    WebAssembly.validate = (b) => (b.byteLength < 64 ? false : v.call(WebAssembly, b));
  }
})();`;

const b = await launch();
try {
    await b.send('Page.addScriptToEvaluateOnNewDocument', { source: INIT });
    // The probe in the readable shim (the minified one is checked against it by tools/minshim.mjs check).
    const shim = fs.readFileSync(path.join(top, 'shim/cdom.js'), 'utf8');
    // The bytes are simdProbe's (their own function, so the IE 9 build can allow that typed array alone), and
    // WebAssembly.validate takes them.
    const lit = /WebAssembly\.validate\(simdProbe\(\)\)/.test(shim) && shim.match(/function simdProbe\(\) \{\s*return new Uint8Array\(\[([\d, ]+)\]\);/);
    let made = null;
    try { made = [...execFileSync('wasm-as', ['-all', path.join(top, 'tools/simd-probe.wat'), '-o', '-'])].join(','); } catch {}
    ok(lit && made && lit[1].replace(/ /g, '') === made, `the probe in shim/cdom.js is tools/simd-probe.wat, assembled by wasm-as (${made ? made.split(',').length + ' bytes' : 'no wasm-as'})`);

    for (const page of ['bundled', 'separate', 'bundled-min', 'separate-min']) {
        const what = `${page.replace('-min', '')} page, ${page.endsWith('-min') ? 'minified' : 'readable'} shim`;
        for (const simd of [true, false]) {
            await b.send('Page.navigate', { url: `${base}/${page}.html${simd ? '' : '?nosimd'}` });
            for (let i = 0; i < 400 && !(await b.evaluate('!!window.READY')); i++) await sleep(25);
            const ready = await b.evaluate('!!window.READY');
            const mods = ready ? await b.evaluate('Promise.all(__mods)') : [];
            const got = mods.map(name).join(' ') || 'none';
            const goal = simd ? 'app.simd.wasm' : 'app.wasm';
            ok(ready && mods.length === 1 && got === goal,
              `${what}: ${simd ? 'the real Chrome gets the SIMD module' : 'with no wasm SIMD (validate stubbed false) the page gets the scalar module'} (fetched: ${got}${ready ? '' : ', and the app never started'})`);
            if (page.startsWith('separate')) {
                const files = (await b.evaluate(FETCHED)).filter((f) => f.endsWith('.wasm'));
                ok(files.join(' ') === goal, `${what}: the page requested ${files.join(' ') || 'no module'} (wanted ${goal} alone)`);
            }
        }
    }

    // The pixels: the SIMD pick and the scalar pick of one page, on bench/image's inputs and equal.mjs's boxes.
    for (const input of ['p12.jpg', 'p12p.jpg', 'p48.jpg', 'p12.png']) for (const box of [900, 300, 1500, 2500, 4000]) {
        const src = `${base}/inputs/${input}`;
        const s = await resizeHash(b, `${base}/bundled.html`, src, box), c = await resizeHash(b, `${base}/bundled.html?nosimd`, src, box);
        ok(s.status === 0 && s.status === c.status && s.w === c.w && s.h === c.h && s.hash === c.hash,
          `${input} box ${box}: the two picks give the same pixels: status ${s.status}, ${s.w} x ${s.h}, sha256 ${s.hash.slice(0, 16)}${s.hash === c.hash ? '' : ` vs ${c.hash.slice(0, 16)} (status ${c.status}, ${c.w} x ${c.h})`}`);
    }
} finally { await b.close(); srv.close(); }
process.exit(fails ? 1 : 0);
