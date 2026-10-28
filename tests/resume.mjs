// Resuming the server-rendered rows page in headless Chrome, for n = 0, 1, 1000, 10000:
//  - after load and a second's wait no app.wasm request was made, cdom.loaded is false, #main
//    is byte for byte the server's and no document node was created;
//  - the first click loads the wasm and is delivered; only the nodes C touched are resolved
//    (selecting row 5 resolves that row and nothing else: not the tbody's other rows) and no
//    node in #main is new;
//  - then: removing row 3 and selecting row 4 (the structural rule: an index taken after a
//    remove would name row 5), appending 1000 rows (the tbody's rows resolve in one call),
//    selecting, swapping (the same nodes move), removing (the next row is the same node) and
//    updating labels all act on the server's nodes and the blob's model, and the final #main
//    equals the client-rendered page seeded alike that ran the same clicks (n = 0, 1000, 10000);
//  - two clicks fired while the wasm loads are both delivered, in order.
// Usage: node tests/resume.mjs [n...]  (builds ssr/rows, serves it on 8701)
//        SSR_APP=tpl node tests/resume.mjs  (the templated app: ssr/rows-tpl on 8704)
import { spawn, execFileSync } from 'node:child_process';
import path from 'node:path';
import { launch, sleep } from '../tools/cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const counts = process.argv.slice(2).length ? process.argv.slice(2).map(Number) : [0, 1, 1000, 10000];
const tpl = process.env.SSR_APP === 'tpl', PORT = tpl ? 8704 : 8701, BASE = `http://127.0.0.1:${PORT}`;
const SSR_SEED = 0x2545f491; // ssr/rows/server.c: the CSR page draws the same rows with Math.random pinned to it
execFileSync(path.join(top, 'ssr/rows/build.sh'), { stdio: 'inherit', env: process.env });
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
Math.random = () => ${SSR_SEED} / 4294967296;
`;
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const b = await launch();
const wasmReqs = [];
b.onEvent = (d) => { if (d.method === 'Network.requestWillBeSent' && /app\.wasm/.test(d.params.request.url)) wasmReqs.push(d.params.request.url); };
const E = b.evaluate;
const STATS = 'cdom.boot().then(x => x.cd_resume_stats(0) + " ids in " + x.cd_resume_stats(1) + " resolutions")';
const until = async (expr, tries = 200) => { for (let i = 0; i < tries; i++) { if (await E(expr)) return true; await sleep(25); } return false; };
const rows = () => E('document.querySelectorAll("tbody tr").length');
const label = (i) => `document.querySelector("tbody tr:nth-child(${i}) td:nth-child(2) a")`;
const removeIcon = (i) => `document.querySelector("tbody tr:nth-child(${i}) td:nth-child(3) span")`;
// The clicks after the first, the same on the resumed and the client-rendered page. Each returns
// what it checks; the CSR run ignores the checks.
async function rest(n, rows0, check) { // n names the page; rows0 is how many rows it has now
  if (rows0 >= 5) {
    await E(`window.__r4 = document.querySelectorAll("tbody tr")[3]; ${removeIcon(3)}.click()`);
    check(await rows() === rows0 - 1, `n=${n} remove row 3`);
    await E(`${label(3)}.click()`); // row 4 now sits third
    check(await E(`document.querySelectorAll("tbody tr")[2] === window.__r4 && window.__r4.className === "danger" && document.querySelectorAll("tbody tr.danger").length === 1`),
      `n=${n} select row 4 after removing row 3: the row with id 4 is selected (its index was taken before the remove)`);
  }
  const before = await rows();
  await E('window.__first = document.querySelector("tbody tr"); document.getElementById("add").click()');
  check(await rows() === before + 1000, `n=${n} add -> ${before + 1000} rows`);
  check(await E(`document.querySelectorAll("tbody tr")[${before}].children[0].textContent`) === String(rows0 + 1), `n=${n} appended ids continue from the blob's next_id`);
  check(await E('window.__first === null || document.querySelector("tbody tr") === window.__first'), `n=${n} the first row is still the server's node`);
  await E(`${label(10)}.click()`);
  check(await E('document.querySelectorAll("tbody tr.danger").length === 1 && document.querySelectorAll("tbody tr")[9].className === "danger"'), `n=${n} select row 10`);
  await E('window.__r1 = document.querySelectorAll("tbody tr")[1]; window.__r998 = document.querySelectorAll("tbody tr")[998]; document.getElementById("swaprows").click()');
  check(await E('document.querySelectorAll("tbody tr")[1] === window.__r998 && document.querySelectorAll("tbody tr")[998] === window.__r1'), `n=${n} swap moved the same nodes`);
  await E(`window.__next = document.querySelectorAll("tbody tr")[20]; ${removeIcon(20)}.click()`);
  check(await E('document.querySelectorAll("tbody tr")[19] === window.__next'), `n=${n} remove row 20 kept the next row's node`);
  await E('window.__labels = [...document.querySelectorAll("tbody tr td:nth-child(2) a")].map(a => a.textContent); document.getElementById("update").click()');
  check(await E('[...document.querySelectorAll("tbody tr td:nth-child(2) a")].every((a, i) => a.textContent === (i % 10 ? window.__labels[i] : window.__labels[i] + " !!!"))'),
    `n=${n} update appends to the model's labels`);
}
try {
  for (let i = 0; i < 50; i++) { try { await fetch(`${BASE}/?n=0`); break; } catch { await sleep(100); } }
  await b.send('Network.enable'); await b.send('Network.setCacheDisabled', { cacheDisabled: true });
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });
  for (const n of counts) try {
    const page = await (await fetch(`${BASE}/?n=${n}`)).text();
    const server = page.slice(page.indexOf('<div id="main">') + 15, page.indexOf('</div><script type="application/octet-stream"'));
    wasmReqs.length = 0;
    await b.send('Page.navigate', { url: `${BASE}/?n=${n}` });
    await until('document.readyState === "complete"');
    await sleep(1000);
    ok(wasmReqs.length === 0 && await E('cdom.loaded') === false, `n=${n} no app.wasm request after load and a second (${wasmReqs.length}), cdom.loaded false`);
    ok(await E('document.getElementById("main").innerHTML') === server, `n=${n} #main is the server's bytes`);
    ok(await E('Object.values(window.__c).every(v => v === 0)'), `n=${n} no node created (${await E('JSON.stringify(window.__c)')})`);
    ok(await rows() === n, `n=${n} ${n} rows`);
    const ALL = '(() => { const a = [], w = (n) => { for (let c = n.firstChild; c; c = c.nextSibling) { a.push(c); w(c); } }; w(document.getElementById("main")); return a; })()';
    await E(`for (const c of ${ALL}) c.__s = 1`); // the server's nodes
    const sel = Math.min(5, n);
    if (sel) { // the first click: select a row
      await E(`window.__row = document.querySelectorAll("tbody tr")[${sel - 1}]; ${label(sel)}.click()`);
      ok(await until('cdom.loaded && document.querySelectorAll("tbody tr.danger").length > 0 || !!window.__err'), `n=${n} the first click loads the wasm`);
      ok(!await E('window.__err'), `n=${n} no error${await E('window.__err') ? ': ' + await E('window.__err') : ''}`);
      ok(wasmReqs.length === 1, `n=${n} one app.wasm request`);
      ok(await E(`window.__row.className === "danger" && document.querySelectorAll("tbody tr.danger").length === 1 && document.querySelectorAll("tbody tr")[${sel - 1}] === window.__row`),
        `n=${n} the click was delivered: row ${sel} (the server's node) is selected`);
      const st = await E(STATS);
      ok(st === '1 ids in 1 resolutions', `n=${n} one id resolved, once (${st})`);
      ok(await E('[...document.querySelectorAll("#main *")].filter(e => e.$i !== undefined).length === 1 && window.__row.$i !== undefined'),
        `n=${n} the one node with a JS id is the clicked row: none of the tbody's other ${n - 1} rows`);
      ok(await E(`${ALL}.every(c => c.__s)`), `n=${n} no node in #main is new`);
    } else { // no rows: the first click is "Append 1,000 rows"
      await E('document.getElementById("add").click()');
      ok(await until('cdom.loaded && document.querySelectorAll("tbody tr").length === 1000 || !!window.__err'), `n=${n} the first click loads the wasm and appends 1000 rows`);
      ok(!await E('window.__err'), `n=${n} no error${await E('window.__err') ? ': ' + await E('window.__err') : ''}`);
      const st = await E(STATS);
      ok(st === '2 ids in 2 resolutions', `n=${n} the button, then the tbody resolved (${st})`);
    }
    await rest(n, sel ? n : 1000, ok);
    if (n !== 1) { // the same clicks on the client-rendered page, seeded alike (n=1 has no twin: its buttons make 1000 or 10000 rows)
      const resumed = await E('document.getElementById("main").innerHTML');
      await b.send('Page.navigate', { url: `${BASE}/csr/` });
      await until('!!document.getElementById("run")');
      if (n) await E(`document.getElementById(${JSON.stringify(n === 1000 ? 'run' : 'runlots')}).click()`);
      if (sel) await E(`${label(sel)}.click()`); else await E('document.getElementById("add").click()');
      await rest(n, sel ? n : 1000, () => {});
      const csr = await E('document.getElementById("main").innerHTML'), res = resumed;
      let i = 0; while (i < csr.length && csr[i] === res[i]) i++;
      ok(csr === res, `n=${n} after the same clicks #main equals the client-rendered page's (${res.length} chars)${csr === res ? '' : ' first difference at ' + i + ': ' + res.slice(i, i + 80) + ' | ' + csr.slice(i, i + 80)}`);
    }
    if (n >= 5) { // two clicks while the wasm loads
      await b.send('Page.navigate', { url: `${BASE}/?n=${n}` });
      await until('document.readyState === "complete"');
      await E(`${label(5)}.click(); document.getElementById("add").click()`);
      ok(await until(`document.querySelectorAll("tbody tr").length === ${n + 1000} || !!window.__err`), `n=${n} the second click, fired during the load, was delivered (${await rows()} rows)`);
      ok(await E('document.querySelectorAll("tbody tr")[4].className === "danger"'), `n=${n} and the first, before it`);
    }
  } catch (e) { ok(false, `n=${n} threw: ${String(e).slice(0, 300)}`); }
} finally { await b.close(); srv.kill('SIGTERM'); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
