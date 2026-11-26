// The no-script fallback's two images (docs/noscript-design.md), the build step after tools/cdpage when the page has the
// /*CDOM_NOSCRIPT*/ block (cdom.mk runs it): a screenshot of the built page in headless Chrome, with script, at the
// shot's size, and the snowman, U+2603 rendered in the same Chrome; each made a GIF by tools/noscript_gif.py. Both are
// written beside the page, under the names the page's block gives them.
//
//   node tools/noscript.mjs page.html shot.gif=WxH snowman.gif=WxH
//
// The page is served from its own folder on 127.0.0.1 (so a module beside it loads too), at the shot's size and a
// device pixel ratio of 1, preferring reduced motion and a light scheme, its animations held at their start, and shot once it is loaded, its fonts are ready, and nothing in it has changed for 300 ms
// (or 5 s have passed: an app that animates is shot as it is then). A request for either GIF fails the build: a browser
// with script must never fetch them.
// Content stamps, in the style of tools/legacy/build.mjs: page.html.noscript holds what the two GIFs were made from
// (these tools' bytes, the Chrome binary, Python's and Pillow's versions, the page's bytes, every file the page
// fetched, by name) and the GIFs' own hashes; when all of it is the same, nothing is launched. So the same inputs give
// the same GIF bytes without a Chrome, and an edit to any of them makes them again.
// CHROME names the browser (else Chrome or Chromium on the PATH, else tools/cdp.mjs's default); PYTHON the Python with
// Pillow (python3 unless set).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const die = (m) => { console.error(`noscript.mjs: ${m}`); process.exit(1); };
const [pageArg, shotArg, snowArg] = process.argv.slice(2);
if (!snowArg) die('usage: node tools/noscript.mjs page.html shot.gif=WxH snowman.gif=WxH');
const img = (a) => {
    const m = /^([^=/]+)=(\d+)x(\d+)$/.exec(a);
    if (!m) die(`${a} is not name.gif=WxH`);
    return { name: m[1], w: +m[2], h: +m[3] };
};
const shot = img(shotArg), snow = img(snowArg);
const page = path.resolve(pageArg), dir = path.dirname(page), stampFile = `${page}.noscript`;
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const shaFile = (f) => { try { return sha(fs.readFileSync(f)); } catch { return 'none'; } };
const html = fs.readFileSync(page);
for (const m of [shot, snow])
    if (!html.includes(`<img src="${m.name}" width="${m.w}" height="${m.h}"`)) die(`${pageArg} does not show ${m.name} at ${m.w}x${m.h} (its /*CDOM_NOSCRIPT*/ block)`);

// The browser: CHROME, else one on the PATH, else cdp.mjs's own default (the macOS app). tools/cdp.mjs reads LEGACY when
// it loads, and an old browser's shot is not the app's: the build is shot in Chrome as it is.
if (!process.env.CHROME) {
    for (const c of ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser']) {
        const r = spawnSync('sh', ['-c', `command -v ${c}`], { encoding: 'utf8' });
        if (r.status === 0 && r.stdout.trim()) { process.env.CHROME = r.stdout.trim(); break; }
    }
}
delete process.env.LEGACY;
const chrome = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
let chromeId = 'none';
try { const real = fs.realpathSync(chrome), st = fs.statSync(real); chromeId = `${real} ${st.size} ${st.mtimeMs}`; } catch {}
const python = process.env.PYTHON || 'python3', gifTool = path.join(here, 'noscript_gif.py');
const pv = spawnSync(python, [gifTool, '--version'], { encoding: 'utf8' });
if (pv.status !== 0) die(`${python} ${gifTool} --version failed: it needs Python 3 with Pillow (python3 -m pip install pillow), or set CDOM_NOSCRIPT := 0\n${pv.stderr || pv.error || ''}`);

// The stamp's lines, but for the files the page fetched, which only a shot can tell, and the outputs.
const head = [`noscript 1 ${shot.name}=${shot.w}x${shot.h} ${snow.name}=${snow.w}x${snow.h}`,
  `tools ${sha(Buffer.concat(['noscript.mjs', 'noscript_gif.py', 'cdp.mjs'].map((f) => fs.readFileSync(path.join(here, f)))))}`,
  `chrome ${chromeId}`, `python ${pv.stdout.trim()}`, `page ${sha(html)}`];
const outs = () => [shot, snow].map((m) => `out ${m.name} ${shaFile(path.join(dir, m.name))}`);
try {
    const old = fs.readFileSync(stampFile, 'utf8').trimEnd().split('\n');
    const files = old.filter((l) => l.startsWith('file ')).map((l) => { const [, n] = l.split(' '); return `file ${n} ${shaFile(path.join(dir, n))}`; });
    if (old.join('\n') === [...head, ...files, ...outs()].join('\n')) process.exit(0); // up to date: no Chrome
} catch {}
if (chromeId === 'none') die(`no Chrome at ${chrome}: set CHROME to a Chrome or Chromium, or set CDOM_NOSCRIPT := 0`);

const { launch, sleep } = await import('./cdp.mjs');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css',
  '.gif': 'image/gif', '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml', '.json': 'application/json', '.txt': 'text/plain' };
const fetched = new Set();
const server = http.createServer((req, res) => {
    const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/^\/+/, '');
    const f = path.resolve(dir, rel);
    if (!f.startsWith(dir + path.sep) || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
    if (f !== page) fetched.add(rel);
    res.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream', 'cache-control': 'no-store' });
    res.end(fs.readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-noscript-'));
const b = await launch(['--hide-scrollbars', '--force-device-scale-factor=1', '--force-color-profile=srgb', '--font-render-hinting=none']);
try {
    const requested = [];
    b.onEvent = (d) => { if (d.method === 'Network.requestWillBeSent') requested.push(d.params.request.url); };
    await b.send('Network.enable');
    await b.send('Network.setCacheDisabled', { cacheDisabled: true });
    // A still picture of the app, the same every time: the reader prefers reduced motion and a light scheme, and every
    // animation and transition is held at its start (the Animation domain's playback rate 0).
    await b.send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }, { name: 'prefers-color-scheme', value: 'light' }] });
    await b.send('Animation.enable');
    await b.send('Animation.setPlaybackRate', { playbackRate: 0 });
    const size = async (m) => b.send('Emulation.setDeviceMetricsOverride', { width: m.w, height: m.h, deviceScaleFactor: 1, mobile: false });
    const capture = async (m, file) => {
        const r = await b.send('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width: m.w, height: m.h, scale: 1 } });
        if (!r.result?.data) die(`Chrome took no screenshot: ${JSON.stringify(r.error || r)}`);
        fs.writeFileSync(file, Buffer.from(r.result.data, 'base64'));
    };
    const settle = `new Promise((done) => {
    let last = performance.now();
    const seen = new MutationObserver(() => { last = performance.now(); });
    seen.observe(document, { subtree: true, childList: true, attributes: true, characterData: true });
    const end = performance.now() + 5000;
    const tick = () => {
      const now = performance.now();
      if (document.readyState === 'complete' && (now - last >= 300 || now >= end)) {
        seen.disconnect();
        requestAnimationFrame(() => requestAnimationFrame(() => done(now >= end ? 'busy' : 'quiet')));
      } else setTimeout(tick, 50);
    };
    document.fonts.ready.then(() => setTimeout(tick, 50));
  })`;
    // The app, with script.
    await size(shot);
    const port = server.address().port;
    await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/${encodeURIComponent(path.basename(page))}` });
    let state = '';
    for (let i = 0; i < 200 && !state; i++) {
        try { state = await b.evaluate(`location.port === "${port}" && document.readyState !== "loading" ? ${settle} : ""`); } catch { state = ''; }
        if (!state) await sleep(50);
    }
    if (!state) die(`${pageArg} did not load in Chrome`);
    if (state === 'busy') console.error(`noscript.mjs: ${pageArg} was still changing after 5 s; shot as it was`);
    if (b.exceptions.length) console.error(`noscript.mjs: ${pageArg} threw in Chrome: ${b.exceptions.join('; ')}`);
    const gifs = requested.filter((u) => [shot.name, snow.name].some((n) => u.split('?')[0].endsWith(`/${n}`)));
    if (gifs.length) die(`Chrome with script requested ${gifs.join(' ')}: a browser with script must never fetch the fallback's images`);
    await capture(shot, path.join(tmp, 'shot.png'));
    // The snowman, U+2603, black on white, in the first font that has it.
    await size(snow);
    const px = Math.round(snow.h * 0.84);
    const doc = `<!doctype html><meta charset="utf-8"><body style="margin:0;background:#fff"><div style="width:${snow.w}px;height:${snow.h}px;`
      + `font:${px}px/${snow.h}px 'DejaVu Sans','Noto Sans Symbols','Apple Symbols','Segoe UI Symbol',sans-serif;text-align:center;color:#000">\u2603</div>`;
    await b.send('Page.navigate', { url: `data:text/html;charset=utf-8,${encodeURIComponent(doc)}` });
    state = '';
    for (let i = 0; i < 200 && !state; i++) {
        try { state = await b.evaluate(`location.protocol === "data:" && document.readyState === "complete" ? document.fonts.ready.then(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(() => r("ok"))))) : ""`); } catch { state = ''; }
        if (!state) await sleep(50);
    }
    if (!state) die('the snowman page did not load in Chrome');
    await capture(snow, path.join(tmp, 'snow.png'));
} finally {
    await b.close();
    server.close();
}
for (const [m, png] of [[shot, 'shot.png'], [snow, 'snow.png']]) {
    const out = path.join(dir, m.name), part = `${out}.tmp${process.pid}`;
    const r = spawnSync(python, [gifTool, path.join(tmp, png), part], { encoding: 'utf8' });
    if (r.status !== 0) { try { fs.rmSync(part); } catch {} die(`${gifTool} failed on ${m.name}:\n${r.stderr}`); }
    fs.renameSync(part, out);
}
fs.rmSync(tmp, { recursive: true, force: true });
const files = [...fetched].sort().map((n) => `file ${n} ${shaFile(path.join(dir, n))}`);
fs.writeFileSync(stampFile, [...head, ...files, ...outs()].join('\n') + '\n');
