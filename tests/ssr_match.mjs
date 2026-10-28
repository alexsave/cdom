// The server's HTML is the DOM the shim builds. With Math.random pinned so cd_start's seed is
// the server's SSR_SEED, the plain client-rendered page (/csr/) after clicking nothing, #run
// and #runlots must serialise (#main.innerHTML) to exactly the bytes the server wrote inside
// <div id="main"> for n = 0, 1000 and 10000; a node-by-node walk of the parsed server page is
// compared too, so a mismatch names its first differing node.
// Usage: node tests/ssr_match.mjs  (expects ssr/rows/out/server on 8701)
import { launch, sleep } from '../tools/cdp.mjs';
const SSR_SEED = 0x2545f491; // ssr/rows/server.c
const BASE = `http://127.0.0.1:${process.env.SSR_PORT || 8701}`;
const cases = [[0, null], [1000, 'run'], [10000, 'runlots']];
const CANON = `window.__canon = (root) => { const out = [];
  const w = (n) => { for (let c = n.firstChild; c; c = c.nextSibling) {
    if (c.nodeType === 8 || (c.nodeType === 3 && c.nodeValue === '')) continue;
    if (c.nodeType === 3) { out.push('"' + c.nodeValue + '"'); continue; }
    out.push('<' + c.localName + [...c.attributes].map(a => ' ' + a.name + '=' + a.value).join('') + '>'); w(c); out.push('</>'); } };
  w(root); return out; };`;
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const b = await launch();
try {
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: `Math.random = () => ${SSR_SEED} / 4294967296;` + CANON });
  const E = b.evaluate;
  for (const [n, button] of cases) {
    const page = await (await fetch(`${BASE}/?n=${n}`)).text();
    const a = page.indexOf('<div id="main">') + 15, z = page.indexOf('</div><script type="application/octet-stream"');
    const server = page.slice(a, z);
    await b.send('Page.navigate', { url: `${BASE}/csr/` });
    for (let i = 0; i < 100 && !(await E('!!document.getElementById("run")')); i++) await sleep(50);
    if (button) await E(`document.getElementById(${JSON.stringify(button)}).click()`);
    const csr = await E('document.getElementById("main").innerHTML');
    ok(csr === server, `n=${n}: client DOM innerHTML (${csr.length} chars) === server bytes (${server.length})`);
    const diff = await E(`(() => { const s = new DOMParser().parseFromString(${JSON.stringify(page)}, 'text/html').getElementById('main');
      const x = __canon(s), y = __canon(document.getElementById('main'));
      for (let i = 0; i < Math.max(x.length, y.length); i++) if (x[i] !== y[i]) return i + ': server ' + x[i] + ' client ' + y[i];
      return 'same ' + x.length; })()`);
    ok(diff.startsWith('same'), `n=${n}: parsed server page and client DOM node by node: ${diff}`);
  }
} finally { await b.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
