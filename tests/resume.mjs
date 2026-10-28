// Hydration of the server-rendered rows page in headless Chrome, for n = 0, 1, 1000, 10000:
// no document node is created (template nodes are counted apart), every server node is
// adopted, verify finds no mismatch, #main's HTML is unchanged by hydration, and the page
// is live (add, select, swap, update all act on the hydrated model and nodes).
// Usage: node tests/hydrate.mjs [n...]  (builds ssr/rows, serves it on 8701)
//        SSR_APP=tpl node tests/hydrate.mjs  (the templated app: ssr/rows-tpl on 8704)
import { spawn, execFileSync } from 'node:child_process';
import path from 'node:path';
import { launch, sleep } from '../tools/cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const counts = process.argv.slice(2).length ? process.argv.slice(2).map(Number) : [0, 1, 1000, 10000];
const tpl = process.env.SSR_APP === 'tpl', PORT = tpl ? 8704 : 8701;
execFileSync(path.join(top, 'ssr/rows/build.sh'), { stdio: 'inherit' });
const srv = spawn(path.join(top, tpl ? 'ssr/rows-tpl/out/server' : 'ssr/rows/out/server'), ['--port', String(PORT)], { stdio: ['ignore', 'ignore', 'inherit'] });
const HOOK = `
window.__c = { createElement: 0, createElementNS: 0, createTextNode: 0, cloneNode: 0 };
for (const [o, k] of [[Document.prototype, 'createElement'], [Document.prototype, 'createElementNS'],
                      [Document.prototype, 'createTextNode'], [Node.prototype, 'cloneNode']]) {
  const f = o[k]; o[k] = function (...a) { window.__c[k]++; return f.apply(this, a); };
}
window.__err = null;
addEventListener('error', e => { window.__err = String(e.message); });
addEventListener('unhandledrejection', e => { window.__err = String(e.reason && e.reason.message || e.reason); });
// Snapshot #main as the server wrote it: the shim's start reaches instantiate before any op.
const snap = () => { if (window.__before) return; const m = document.getElementById('main');
  let k = 0; const w = n => { for (let c = n.firstChild; c; c = c.nextSibling) { if (c.nodeType !== 8) k++; w(c); } }; w(m);
  window.__before = m.outerHTML; window.__serverNodes = k; window.__c0 = { ...window.__c }; };
for (const k of ['instantiateStreaming', 'instantiate']) { const f = WebAssembly[k]; WebAssembly[k] = function (...a) { snap(); return f.apply(this, a); }; }
`;
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const b = await launch();
try {
  for (let i = 0; i < 50; i++) { try { await fetch(`http://127.0.0.1:${PORT}/?n=0`); break; } catch { await sleep(100); } }
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });
  const E = b.evaluate;
  for (const n of counts) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${PORT}/?n=${n}&verify=1` });
    let ready = false;
    for (let i = 0; i < 200 && !ready; i++) { await sleep(50); ready = await E('!!(window.cdom && cdom.stats) || !!window.__err'); }
    const st = await E('JSON.stringify({ stats: cdom.stats, c: window.__c, c0: window.__c0, nodes: window.__serverNodes, err: window.__err })');
    const { stats, c, c0, nodes, err } = JSON.parse(st);
    const made = k => c[k] - c0[k];
    console.log(`n=${n}: server nodes ${nodes}, stats ${JSON.stringify(stats)}, calls ${JSON.stringify(Object.fromEntries(Object.keys(c).map(k => [k, made(k)])))}`);
    ok(!err, `n=${n} no error or verify mismatch${err ? ': ' + err : ''}`);
    ok(stats && stats.created === 0, `n=${n} no document node created`);
    ok(stats && made('createElement') + made('createElementNS') + made('createTextNode') + made('cloneNode') === stats.templateNodes,
      `n=${n} every creation call is a template node`);
    ok(made('cloneNode') === 0, `n=${n} no cloneNode`);
    ok(stats && stats.adopted === nodes, `n=${n} adopted ${stats && stats.adopted} = server nodes ${nodes}`);
    ok(await E('document.getElementById("main").outerHTML === window.__before'), `n=${n} #main unchanged by hydration`);
    const rows = () => E('document.querySelectorAll("tbody tr").length');
    ok(await rows() === n, `n=${n} ${n} rows`);
    await E('window.__first = document.querySelector("tbody tr")');
    await E('document.getElementById("add").click()');
    ok(await rows() === n + 1000, `n=${n} add -> ${n + 1000} rows`);
    ok(await E(`document.querySelectorAll("tbody tr")[${n}].children[0].textContent`) === String(n + 1), `n=${n} appended ids continue from the blob's next_id`);
    ok(await E('window.__first === null || document.querySelector("tbody tr") === window.__first'), `n=${n} the first row is still the server's node`);
    await E('document.querySelector("tbody tr:nth-child(5) td:nth-child(2) a").click()');
    ok(await E('document.querySelectorAll("tbody tr.danger").length === 1 && document.querySelectorAll("tbody tr")[4].className === "danger"'), `n=${n} select row 5`);
    await E('window.__r1 = document.querySelectorAll("tbody tr")[1]; window.__r998 = document.querySelectorAll("tbody tr")[998]');
    await E('document.getElementById("swaprows").click()');
    ok(await E('document.querySelectorAll("tbody tr")[1] === window.__r998 && document.querySelectorAll("tbody tr")[998] === window.__r1'), `n=${n} swap moved the same nodes`);
    await E('window.__labels = [...document.querySelectorAll("tbody tr td:nth-child(2) a")].map(a => a.textContent)');
    await E('document.getElementById("update").click()');
    ok(await E('[...document.querySelectorAll("tbody tr td:nth-child(2) a")].every((a, i) => a.textContent === (i % 10 ? window.__labels[i] : window.__labels[i] + " !!!"))'),
      `n=${n} update appends to the hydrated model's labels`);
  }
} finally { await b.close(); srv.kill('SIGTERM'); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
