// Op-stream conformance of the C sink against the shim, and hydration over the sink's HTML.
//  1. client: after cd_main and after every step, the shim's DOM equals the parse of the
//     sink's HTML for that step, node by node (empty text and the <!----> separators aside);
//  2. hydrate: the sink's first frame is parsed into the mount, the shim hydrates it with
//     verify on, then replays every step; after each, the DOM equals the client run's
//     exactly, empty text nodes included and no separator comment left.
// Usage: node tests/conform.mjs
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { launch, sleep } from '../tools/cdp.mjs';
const here = path.dirname(new URL(import.meta.url).pathname), out = path.join(here, 'out');
const CC = process.env.WASM_CC || '/opt/homebrew/opt/llvm/bin/clang';
fs.mkdirSync(out, { recursive: true });
execFileSync('nice', ['-n', '10', 'cc', '-O2', '-Wall', '-Wextra', '-Werror', '-o', path.join(out, 'conform_native'),
  path.join(here, 'conform.c'), path.join(here, '../src/cdom.c'), path.join(here, '../src/cdom_html.c')], { stdio: 'inherit' });
execFileSync('nice', ['-n', '10', CC, '--target=wasm32', '-O2', '-nostdlib', '-mbulk-memory', '-Wall', '-Wextra', '-Werror',
  '-Wl,--no-entry', '-o', path.join(out, 'conform.wasm'), path.join(here, 'conform.c'), path.join(here, '../src/cdom.c'),
  path.join(here, '../src/mini.c')], { stdio: 'inherit' });
fs.copyFileSync(path.join(here, 'conform.html'), path.join(out, 'conform.html'));
fs.copyFileSync(path.join(here, '../shim/cdom.js'), path.join(out, 'cdom.js'));
// The sink's frames: "<length>\n<html>" per step.
const raw = execFileSync(path.join(out, 'conform_native'), { maxBuffer: 1 << 28 });
const frames = [];
for (let o = 0; o < raw.length;) { const nl = raw.indexOf(10, o), n = +raw.subarray(o, nl).toString(); frames.push(raw.subarray(nl + 1, nl + 1 + n).toString()); o = nl + 1 + n; }
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(out, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else if (!m.startsWith('.')) console.log('ok  ', m); };
const firstDiff = (a, b) => { const x = a.split('\n'), y = b.split('\n'); for (let i = 0; ; i++) if (x[i] !== y[i]) return `line ${i}: ${x[i]} | ${y[i]}`; };
const b = await launch();
try {
  const E = b.evaluate, url = `http://127.0.0.1:${srv.address().port}/conform.html`;
  const load = async () => { await b.send('Page.navigate', { url }); for (let i = 0; i < 100 && !(await E('!!window.go')); i++) await sleep(50); };
  const steps = frames.length - 1;
  // 1. client run against the sink
  await load(); await E('go()');
  const strict = [];
  let bad = 0;
  for (let k = 0; k <= steps; k++) {
    if (k) await E(`x.step(${k})`);
    const dom = await E('canon(false)'), want = await E(`parsed(${JSON.stringify(frames[k])})`);
    strict.push(await E('canon(true)'));
    if (dom !== want && bad++ < 3) ok(false, `step ${k}: client DOM vs sink HTML: ${firstDiff(want, dom)}`);
  }
  ok(!bad, `client DOM equals the parsed sink HTML after cd_main and each of ${steps} steps`);
  ok(frames[0].includes('<!---->') && /""/.test(strict[0]), 'the first frame has separators and empty text nodes to hydrate');
  // 2. hydrate the sink's first frame, then replay
  await load();
  const err = await E(`go(${JSON.stringify(frames[0])}).then(() => '', e => String(e))`);
  ok(err === '', `hydration with verify: ${err || 'no mismatch'}`);
  const st = await E('JSON.stringify(cdom.stats)');
  const empties = (strict[0].match(/^""$/gm) || []).length;
  ok(JSON.parse(st).created === empties, `hydration created exactly the ${empties} empty text nodes (${st})`);
  bad = 0;
  for (let k = 0; k <= steps; k++) {
    if (k) await E(`x.step(${k})`);
    const dom = await E('canon(true)');
    if (dom !== strict[k] && bad++ < 3) ok(false, `step ${k}: hydrated DOM vs client DOM: ${firstDiff(strict[k], dom)}`);
  }
  ok(!bad, `hydrated DOM equals the client DOM exactly after hydration and each of ${steps} steps`);
  // 3. verify catches a page that is not what the C would render
  const f0 = frames[0], tamper = {
    'an attribute value': f0.replace('title="first"', 'title="frist"'),
    'a class': f0.replace('class="t odd"', 'class="t even"'),
    'a text': f0.replace('three', 'tree'),
    'a tag': f0.replace('<p>', '<q>').replace('</p>', '</q>'),
    'an extra node': f0 + '<b>extra</b>',
    'a missing node': f0.replace('<span></span><pre>', '<pre>'),
    'a merged text': f0.replace('one<!---->two', 'onetwo'),
  };
  for (const [what, html] of Object.entries(tamper)) {
    if (html === f0) { ok(false, `tamper ${what} did not apply`); continue; }
    await load();
    const e = await E(`go(${JSON.stringify(html)}).then(() => '', e => String(e))`);
    ok(e.includes('hydration mismatch'), `verify refuses ${what}: ${e || 'accepted'}`);
  }
} finally { await b.close(); srv.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
