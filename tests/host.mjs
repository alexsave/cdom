// The HOST op end to end (cdom.h): the chart demo (apps/chart, port 8705) with Chart.js from the
// CDN on a C-owned canvas, resumed from the server's page, and the cdv decoders held to one
// rendering. The CDN is required: if Chart.js does not load, the test fails, it does not skip.
//  - the server's page: the <ul> lists the server's points, the checkbox carries `checked` (PROP
//    in the C sink), the canvas is empty, the page's only inline JavaScript is the shim and the
//    one cdom.resume call C wrote (no app code), and nothing fetches app.wasm until a click;
//  - Add point: the wasm loads, the deferred MOUNT makes the canvas's $h a Chart (the one
//    Chart.getChart finds) and then the click lands: the chart's data, labels and the <ul> all
//    hold N + 1 points, the same values;
//  - Recolor sets the dataset's borderColor; Toggle smooth flips the checkbox through PROP (only
//    C sets it) and the tension through SET; a click on the checkbox itself agrees with C;
//  - Destroy: Chart.getChart(canvas) is undefined and $h is gone; a point added meanwhile shows
//    in the <ul>; Remount: a new Chart with the current data, colour and tension;
//  - a synthetic CustomEvent('cdping') on the ping paragraph is counted by C (CD_EV_CUSTOM with
//    the name's id), one on another node is not;
//  - ?eager=1: the page loads the wasm with no interaction and the chart appears;
//  - cdv: tests/cdv.c built for wasm emits every case through host into a window.CdvProbe class;
//    the probe's rendering of the decoded value equals `tests/out/cdv --list`, the strings the C
//    sink's decoder is held to natively (tests/cdv.sh), so the two decoders agree on every tag.
// Usage: node tests/host.mjs  (builds apps/chart and the cdv wasm; serves on 8705)
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from '../tools/cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const PORT = 8705, BASE = `http://127.0.0.1:${PORT}`;
const CDN = 'https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js';
const CC = process.env.WASM_CC || '/opt/homebrew/opt/llvm/bin/clang';
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };

execFileSync(path.join(top, 'apps/chart/build.sh'), { stdio: 'inherit' });
fs.mkdirSync(path.join(top, 'tests/out'), { recursive: true });
execFileSync('nice', ['-n', '10', 'cc', '-O2', '-Wall', '-Wextra', '-Werror', '-o', path.join(top, 'tests/out/cdv'),
  path.join(top, 'tests/cdv.c'), path.join(top, 'src/cdom.c'), path.join(top, 'src/cdom_html.c')], { stdio: 'inherit' });
execFileSync('nice', ['-n', '10', CC, '--target=wasm32', '-O2', '-nostdlib', '-mbulk-memory', '-Wall', '-Wextra', '-Werror',
  '-Wl,--no-entry', '-Wl,--strip-all', '-o', path.join(top, 'tests/out/cdv.wasm'),
  path.join(top, 'tests/cdv.c'), path.join(top, 'src/cdom.c'), path.join(top, 'src/mini.c')], { stdio: 'inherit' });
const want = execFileSync(path.join(top, 'tests/out/cdv'), ['--list']).toString().trim().split('\n').map(l => l.split('\t'));

// the CDN first: a failure here is the test failing, loudly
const cdn = await fetch(CDN).catch(e => ({ ok: false, status: String(e) }));
if (!cdn.ok) { console.log(`FAIL Chart.js could not be fetched from ${CDN}: ${cdn.status}`); process.exit(1); }
ok((await cdn.arrayBuffer()).byteLength === 200807, 'Chart.js 4.4.1 fetched from the CDN (200807 bytes)');

const srv = spawn(path.join(top, 'apps/chart/out/server'), ['--port', String(PORT)], { stdio: ['ignore', 'ignore', 'inherit'] });
console.log(`server pid ${srv.pid}`);
const HOOK = `
window.__err = null;
addEventListener('error', e => { window.__err = String(e.message || e.target && e.target.src); }, true);
addEventListener('unhandledrejection', e => { window.__err = String(e.reason && e.reason.message || e.reason); });
`;
const b = await launch();
const wasmReqs = [], cdnFail = [];
b.onEvent = (d) => {
  if (d.method === 'Network.requestWillBeSent' && /app\.wasm/.test(d.params.request.url)) wasmReqs.push(d.params.request.url);
  if (d.method === 'Network.loadingFailed') cdnFail.push(d.params.errorText);
};
const E = b.evaluate;
const until = async (expr, tries = 400) => { for (let i = 0; i < tries; i++) { if (await E(expr)) return true; await sleep(25); } return false; };
// waits for cond (or a page error, which ends the wait early) and answers whether cond holds
const holds = async (cond) => { await until(`(${cond}) || !!window.__err`); return !!await E(cond); };
const C = 'document.querySelector("canvas")';
const CH = `Chart.getChart(${C})`;
const lis = () => E('document.querySelectorAll("#points li").length');
const ys = 'JSON.stringify([...document.querySelectorAll("#points li")].map(l => +l.textContent.split("y ")[1]))';
const xs = 'JSON.stringify([...document.querySelectorAll("#points li")].map(l => +l.textContent.split(":")[0].slice(2)))';
const click = (id) => E(`document.getElementById(${JSON.stringify(id)}).click()`);
const noErr = async (m) => { const e = await E('window.__err'); ok(!e, `${m}: no page error${e ? ': ' + e : ''}`); };
const open = async (url) => {
  wasmReqs.length = 0; cdnFail.length = 0;
  await b.send('Page.navigate', { url });
  await until('document.readyState === "complete"');
  if (!await E('typeof Chart === "function"')) { console.log(`FAIL Chart.js did not load in the page (${cdnFail.join(', ') || 'no error reported'})`); throw new Error('no Chart.js'); }
};
try {
  for (let i = 0; i < 50; i++) { try { await fetch(`${BASE}/?n=0`); break; } catch { await sleep(100); } }
  await b.send('Network.enable'); await b.send('Network.setCacheDisabled', { cacheDisabled: true });
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });

  // ---- the server's page, before any interaction ----
  const page = await (await fetch(`${BASE}/?n=5`)).text();
  ok(page.includes('<input type="checkbox" id="smooth" checked="">'), 'the server HTML has the checkbox checked (PROP written as the attribute by the C sink)');
  ok(page.includes('<canvas aria-label="points" role="img"></canvas>'), 'the server HTML has the canvas, empty');
  const inline = [...page.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
  const shim = fs.readFileSync(path.join(top, 'apps/chart/out/cdom.min.js'), 'utf8');
  ok(inline.length === 2 && inline[0] === shim && inline[1] === "cdom.resume('app.wasm', 'main', 'cdom-state', ['click','change'],['cdping']);",
    `the page's inline JavaScript is the shim and the resume call C wrote: 0 lines of app JavaScript (${inline.length} inline scripts; last: ${inline[inline.length - 1]})`);
  ok(page.indexOf(`<script src="${CDN}"`) < page.indexOf('<script>"use strict";var cdom'), 'Chart.js comes before the shim');
  await open(`${BASE}/?n=5`);
  await sleep(1000);
  ok(wasmReqs.length === 0 && await E('cdom.loaded') === false, `no app.wasm request after load and a second (${wasmReqs.length})`);
  ok(await lis() === 5 && await E(ys) === '[46,35,96,86,47]', `the <ul> shows the server's 5 points (${await E(ys)})`);
  ok(await E(`${CH} === undefined && ${C}.$h === undefined`), 'no chart before the first interaction');
  ok(await E('document.getElementById("smooth").checked === true'), 'the checkbox is checked from the server HTML');

  // ---- the first click loads the wasm; the deferred MOUNT runs, then the click lands ----
  await click('add');
  ok(await holds(`cdom.loaded && document.querySelectorAll("#points li").length === 6 && !!${CH}`), 'Add point: the wasm loads and the click is delivered');
  await noErr('Add point');
  ok(wasmReqs.length === 1, `one app.wasm request (${wasmReqs.length})`);
  ok(await E(`${C}.$h instanceof Chart && ${C}.$h === ${CH}`), "the canvas's $h is the Chart instance Chart.getChart finds");
  const chartState = `JSON.stringify((c => ({ n: c.data.datasets[0].data.length, y: [...c.data.datasets[0].data], x: c.data.labels, color: c.data.datasets[0].borderColor, tension: c.data.datasets[0].tension, type: c.config.type, anim: c.options.animation }))(${CH}))`;
  let s = JSON.parse(await E(chartState));
  ok(s.n === 6 && s.n === await lis(), `the chart holds N+1 = 6 points, as many as the <ul> (${s.n})`);
  ok(JSON.stringify(s.y) === await E(ys) && JSON.stringify(s.x) === await E(xs), `the chart's data and labels are the <ul>'s values (${JSON.stringify(s.y)})`);
  ok(s.type === 'line' && s.anim === false && s.color === '#d33' && s.tension === 0.4, `a line chart, animation false, colour #d33, tension 0.4 (${s.type} ${s.anim} ${s.color} ${s.tension})`);
  const first = 'window.__first = ' + CH;
  await E(first);

  await click('add');
  s = JSON.parse(await E(chartState));
  ok(s.n === 7 && await lis() === 7 && JSON.stringify(s.y) === await E(ys) && await E(`${CH} === window.__first`), `Add point again: the same instance updated to 7 points (${s.n})`);

  // ---- Recolor, smoothing (SET), the checkbox (PROP) ----
  await click('recolor');
  s = JSON.parse(await E(chartState));
  ok(s.color === '#36c', `Recolor sets borderColor (${s.color})`);
  await click('toggle');
  s = JSON.parse(await E(chartState));
  ok(await E('document.getElementById("smooth").checked') === false && s.tension === 0, `Toggle smooth: the checkbox follows the state through PROP (checked false) and tension is 0 (${s.tension})`);
  await click('toggle');
  ok(await E('document.getElementById("smooth").checked') === true && JSON.parse(await E(chartState)).tension === 0.4, 'Toggle smooth again: checked true, tension 0.4');
  await click('smooth'); // the box toggles itself and C flips the state; PROP sets it to the state
  ok(await E('document.getElementById("smooth").checked') === false && JSON.parse(await E(chartState)).tension === 0, 'a click on the checkbox itself: unchecked, tension 0');

  // ---- Destroy, Remount ----
  await click('destroy');
  ok(await E(`${CH} === undefined && ${C}.$h === undefined && !("$h" in ${C})`), 'Destroy: Chart.getChart(canvas) is undefined and $h is gone');
  await click('add');
  ok(await lis() === 8 && await E(`${CH} === undefined`), 'a point added while destroyed shows in the <ul> only');
  await click('remount');
  s = JSON.parse(await E(chartState));
  ok(await E(`${C}.$h instanceof Chart && ${C}.$h === ${CH} && ${CH} !== window.__first`), 'Remount: a new Chart instance on the canvas');
  ok(s.n === 8 && JSON.stringify(s.y) === await E(ys) && JSON.stringify(s.x) === await E(xs) && s.color === '#36c' && s.tension === 0,
    `with the current data, colour and tension (${s.n} points, ${s.color}, ${s.tension})`);
  await noErr('Destroy and Remount');

  // ---- a custom event, by name ----
  await E('document.getElementById("ping").dispatchEvent(new CustomEvent("cdping", { bubbles: true }))');
  ok(await E('document.getElementById("ping").textContent') === 'pings: 1', `a cdping CustomEvent on the paragraph is counted by C (${await E('document.getElementById("ping").textContent')})`);
  await E('document.getElementById("points").dispatchEvent(new CustomEvent("cdping", { bubbles: true })); document.getElementById("ping").dispatchEvent(new CustomEvent("cdpong", { bubbles: true }))');
  await E('document.getElementById("ping").dispatchEvent(new CustomEvent("cdping", { bubbles: false }))');
  ok(await E('document.getElementById("ping").textContent') === 'pings: 2', `one on another node and another name are not; a non-bubbling one on it is (${await E('document.getElementById("ping").textContent')})`);
  await noErr('custom events');

  // ---- eager: the chart without an interaction ----
  await open(`${BASE}/?n=5&eager=1`);
  ok(await holds(`cdom.loaded && !!${CH}`), 'eager: the chart appears with no interaction');
  await noErr('eager');
  s = JSON.parse(await E(chartState));
  ok(wasmReqs.length === 1 && s.n === 5 && JSON.stringify(s.y) === '[46,35,96,86,47]' && s.color === '#d33', `eager: one app.wasm request, the server's 5 points (${wasmReqs.length}, ${JSON.stringify(s.y)})`);
  await click('add');
  ok(JSON.parse(await E(chartState)).n === 6 && await lis() === 6, 'eager: Add point updates the chart');

  // ---- cdv: the shim's decoder against the C sink's, every tag ----
  await b.send('Page.navigate', { url: `${BASE}/missing-page` });
  await until('document.readyState === "complete"');
  await E(fs.readFileSync(path.join(top, 'shim/cdom.js'), 'utf8') + '\n;window.cdom = cdom; 1');
  await E(`(() => {
    const r = (v) => v === null ? 'null' : typeof v === 'boolean' || typeof v === 'number' ? String(v)
      : typeof v === 'string' ? '"' + v.replace(/[\\\\"]/g, (c) => '\\\\' + c) + '"'
      : v instanceof Float64Array ? 'f64[' + [...v].map(String).join(',') + ']'
      : Array.isArray(v) ? '[' + v.map(r).join(',') + ']'
      : '{' + Object.entries(v).map(([k, x]) => k + ':' + r(x)).join(',') + '}';
    window.CdvProbe = class { constructor(el, v) { window.__probe = r(v); window.__el = el; } };
    return 1; })()`);
  const wasm = fs.readFileSync(path.join(top, 'tests/out/cdv.wasm')).toString('base64');
  await E(`window.__cdv = cdom.start('data:application/wasm;base64,${wasm}', document.body).then(x => (window.__x = x, 1))`);
  const ncases = await E('__x.ncases()');
  ok(ncases === want.length + 1, `cdv: ${ncases} cases in the wasm build, ${want.length} with a rendering in --list and the big one`);
  for (const [i, w] of want) {
    const got = await E(`(__x.emit(${i}), window.__probe)`);
    ok(got === w, `cdv case ${i}: the shim decodes what the C sink decodes: ${w.length > 60 ? w.slice(0, 60) + '...' : w}${got === w ? '' : ' (got ' + got + ')'}`);
  }
  const big = '{n:f64[' + Array.from({ length: 16000 }, (_, i) => String(i + 0.5)).join(',') + ']}';
  ok(await E(`(__x.emit(${ncases - 1}), window.__probe)`) === big, 'cdv: 16000 doubles in one call decode whole');
  ok(await E('window.__el && window.__el.parentNode === document.body && window.__el.$h instanceof CdvProbe'), 'cdv: MOUNT passed the node and set its $h');
} catch (e) { ok(false, `threw: ${String(e).slice(0, 300)}`); }
finally { await b.close(); srv.kill('SIGTERM'); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
