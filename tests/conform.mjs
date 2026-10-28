// Conformance of the C sink against the shim, resuming over the sink's HTML, and events.
//  1. client: after cd_main and after every step, the shim's DOM equals the parse of the
//     sink's HTML for that step, node by node (empty text and the <!----> separators aside);
//  2. resume: the sink's first frame is parsed into the mount, the shim resumes over it (the
//     first render virtual, nodes resolved by path when C first touches them), real events
//     reach the nodes they reached in the client run, then every step is replayed; after each
//     the DOM equals the client run's exactly, an untouched empty text node's comment read as
//     that node (canon in conform.html).
// Usage: node tests/conform.mjs
// Another C program can be driven the same way: CONFORM_SRCS (space separated sources with cd_main,
// step(k) and steps()), CONFORM_INC (include dirs), CONFORM_NAME (output name). Its native output may
// start with a line "S<base64>" holding the state blob that part 2 resumes with, and a line
// "E<types>, <names>" (cd_html_resume_events) with the event types the page listens to at once. The event checks
// use conform.c's own ids, so they run only for the default program.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { launch, sleep } from '../tools/cdp.mjs';
const here = path.dirname(new URL(import.meta.url).pathname), out = path.join(here, 'out');
const CC = process.env.WASM_CC || '/opt/homebrew/opt/llvm/bin/clang';
fs.mkdirSync(out, { recursive: true });
const custom = !!process.env.CONFORM_SRCS, name = process.env.CONFORM_NAME || 'conform';
const srcs = custom ? process.env.CONFORM_SRCS.split(' ').filter(Boolean) : [path.join(here, 'conform.c')];
const inc = (process.env.CONFORM_INC || '').split(' ').filter(Boolean).flatMap(d => ['-I', d]);
execFileSync('nice', ['-n', '10', 'cc', '-O2', '-Wall', '-Wextra', '-Werror', ...inc, '-o', path.join(out, name + '_native'),
  ...srcs, path.join(here, '../src/cdom.c'), path.join(here, '../src/cdom_html.c')], { stdio: 'inherit' });
execFileSync('nice', ['-n', '10', CC, '--target=wasm32', '-O2', '-nostdlib', '-mbulk-memory', '-Wall', '-Wextra', '-Werror', ...inc,
  '-Wl,--no-entry', '-o', path.join(out, name + '.wasm'), ...srcs, path.join(here, '../src/cdom.c'),
  path.join(here, '../src/mini.c')], { stdio: 'inherit' });
fs.copyFileSync(path.join(here, 'conform.html'), path.join(out, 'conform.html'));
fs.copyFileSync(path.join(here, '../shim/cdom.js'), path.join(out, 'cdom.js'));
// The sink's frames: "<length>\n<html>" per step.
const raw = execFileSync(path.join(out, name + '_native'), { maxBuffer: 1 << 30, stdio: ['ignore', 'pipe', 'inherit'] });
const frames = [];
let blob = '', o0 = 0, events = '[], []';
for (;;) { // "S<base64>\n": the state blob; "E<types>, <names>\n": the resume call's event types
  if (raw[o0] === 83) { const nl = raw.indexOf(10, o0); blob = raw.subarray(o0 + 1, nl).toString(); o0 = nl + 1; }
  else if (raw[o0] === 69) { const nl = raw.indexOf(10, o0); events = raw.subarray(o0 + 1, nl).toString(); o0 = nl + 1; }
  else break;
}
for (let o = o0; o < raw.length;) { const nl = raw.indexOf(10, o), n = +raw.subarray(o, nl).toString(); frames.push(raw.subarray(nl + 1, nl + 1 + n).toString()); o = nl + 1 + n; }
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(out, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else if (!m.startsWith('.')) console.log('ok  ', m); };
const firstDiff = (a, b) => { const x = a.split('\n'), y = b.split('\n'); for (let i = 0; ; i++) if (x[i] !== y[i]) return `line ${i}: ${x[i]} | ${y[i]}`; };
const b = await launch();
try {
  const E = b.evaluate, url = `http://127.0.0.1:${srv.address().port}/conform.html?w=${name}`;
  const load = async () => { await b.send('Page.navigate', { url }); for (let i = 0; i < 100 && !(await E('!!window.go')); i++) await sleep(50); };
  const steps = frames.length - 1;
  // 1. client run against the sink
  await load(); await E('go()');
  if (!custom) { // events: real DOM events reach C through the mount's delegated listeners
    const ev = () => E('[0,1,5,7,8,9].map(k => x.ev(k)).join(" ")'), id = (k) => E(`x.ids(${k})`);
    const [root, item1, ping] = [await id(0), await id(1), await id(2)];
    await E('document.querySelectorAll("#m ul li")[1].firstChild.click()');
    ok(await ev() === `0 ${item1} 0 0 1 1`, `a click on a listening row's child: click, its id, path 0 depth 1 (${await ev()})`);
    await E('document.querySelector("#m pre").dispatchEvent(new CustomEvent("cdping", { bubbles: false }))');
    ok(await ev() === `12 ${root} ${ping} 5 1 2`, `a named event that does not bubble, from a child: CD_EV_CUSTOM, the listener, its name, path 5 (${await ev()})`);
    await E('document.querySelector("#m pre").dispatchEvent(new CustomEvent("cdpang", { bubbles: true }))');
    await E('document.querySelector("#m pre").click()');
    ok((await ev()).endsWith(' 2'), `an event no node on the path asked for reaches no one (${await ev()})`);
  }
  const strict = [];
  let bad = 0;
  for (let k = 0; k <= steps; k++) {
    if (k) await E(`x.step(${k})`);
    const dom = await E('canon(false)'), want = await E(`parsed(${JSON.stringify(frames[k])})`);
    strict.push(await E('canon(true)'));
    if (dom !== want && bad++ < 3) ok(false, `step ${k}: client DOM vs sink HTML: ${firstDiff(want, dom)}`);
  }
  ok(!bad, `client DOM equals the parsed sink HTML after cd_main and each of ${steps} steps`);
  if (!custom) { // after the run: freed ids handed out again
    const n0 = await E('x.ev(9)'), b = await E('x.stale()');
    ok(b > 0, 'both freed ids were handed out again');
    const click = (k) => E(`document.querySelectorAll("#m > b, #m > u")[${k}].click()`); // old a, new a, old b, new b
    await click(0); await click(1); await click(2);
    ok(await E('x.ev(9)') === n0, `a click on a node whose id was freed, or on the id's new node that takes another type, reaches no one`);
    await click(3);
    ok(await E('x.ev(9)') === n0 + 1 && await E('x.ev(1)') === b, `a click on the id's new node that takes clicks reaches that id once`);
  }
  if (!custom) ok(frames[0].includes('<!---->') && /""/.test(strict[0]), 'the first frame has separators and empty text nodes to resume over');
  // 2. resume over the sink's first frame, then replay
  await load();
  const err = await E(`go(${JSON.stringify(frames[0])}, ${JSON.stringify(blob)}, ${events}).then(() => '', e => String(e))`);
  ok(err === '', `resume: ${err || 'started'}`);
  ok(await E('canon(true)') === strict[0], 'the resumed page before any step is the client DOM');
  if (!custom && !err) { // events before anything is resolved: C finds the pending listener on the path
    const ev = () => E('[0,1,5,7,8,9].map(k => x.ev(k)).join(" ")'), id = (k) => E(`x.ids(${k})`);
    const [root, item1, ping, ul] = [await id(0), await id(1), await id(2), await id(3)];
    await E('document.querySelector("#m ul").click()');
    ok(await ev() === `0 ${ul} 0 0 0 1`, `resumed: a click on the unresolved list itself reaches it (${await ev()})`);
    await E('document.querySelectorAll("#m ul li")[1].firstChild.click()');
    ok(await ev() === `0 ${item1} 0 0 1 2`, `resumed: a click on a row's child reaches the unresolved row, not the resolved list above it, path 0 depth 1 (${await ev()})`);
    await E('document.querySelectorAll("#m ul li")[1].firstChild.click()');
    ok(await ev() === `0 ${item1} 0 0 1 3`, `resumed: again, through the resolved row's JS id (${await ev()})`);
    await E('document.querySelector("#m pre").dispatchEvent(new CustomEvent("cdping", { bubbles: false }))');
    ok(await ev() === `12 ${root} ${ping} 5 1 4`, `resumed: a named event that does not bubble reaches the unresolved listener above (${await ev()})`);
    await E('document.querySelector("#m pre").click()');
    ok((await ev()).endsWith(' 4'), `resumed: an event no node on the path asked for reaches no one (${await ev()})`);
    ok(await E('canon(true)') === strict[0], 'resolving for events changed nothing in the DOM');
    ok(await E('x.cd_resume_stats(0)') === 3, `resolving for events resolved the three listeners only (${await E('x.cd_resume_stats(0)')})`);
  }
  bad = err ? 1 : 0; // a failed start leaves nothing to replay
  for (let k = 1; k <= steps && !err; k++) {
    const thrown = await E(`(() => { try { x.step(${k}); return ''; } catch (e) { return String(e); } })()`);
    if (thrown) { bad++; ok(false, `step ${k}: the resumed run threw: ${thrown}`); break; }
    const dom = await E('canon(true)');
    if (dom !== strict[k] && bad++ < 3) ok(false, `step ${k}: resumed DOM vs client DOM: ${firstDiff(strict[k], dom)}`);
  }
  ok(!bad, `resumed DOM equals the client DOM exactly before and after each of ${steps} steps`);
  if (!err) ok(await E('x.cd_resume_stats(0)') > 0, `nodes were resolved lazily (${await E('x.cd_resume_stats(0)')} ids in ${await E('x.cd_resume_stats(1)')} resolutions)`);
} finally { await b.close(); srv.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
