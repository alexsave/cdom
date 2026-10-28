// Experiment: op counts by op code (entries built with -DCDOM_OPSTATS) and the flushes a direct op forced, per benchmark operation.
// Derived from tools/domcount.mjs: wraps the DOM methods and accessors an entry can use,
// replays the harness's warm-up clicks, zeroes the counters, clicks the measured button once and
// prints what that one click called. Counts only; nothing here is timed.
// Usage: node tools/domcount.mjs vanillajs,cdomp [bench filter, e.g. 01,07]
import { launch, sleep } from '../tools/cdp.mjs';
const fws = process.argv[2].split(',');
const only = process.argv[3] ? process.argv[3].split(',') : null;
const row = (i, c) => `tbody>tr:nth-of-type(${i})>td:nth-of-type(${c})`;
const W = 5;
const runClear = Array.from({ length: W }, () => ['#run', '#clear']).flat();
const B = {
  '01_run1k': [runClear, '#run'],
  '02_replace1k': [Array(W).fill('#run'), '#run'],
  '03_update10th1k': [['#run', ...Array(W).fill('#update')], '#update'],
  '04_select1k': [['#run', row(5, 2) + '>a'], row(2, 2) + '>a'],
  '05_swap1k': [['#run', ...Array(W + 1).fill('#swaprows')], '#swaprows'],
  '06_remove-one-1k': [['#run', ...Array.from({ length: W }, (_, i) => row(W - i + 4, 3) + '>a>span'), row(6, 3) + '>a>span'], row(4, 3) + '>a>span'],
  '07_create10k': [runClear, '#runlots'],
  '08_create1k-after1k': [[...runClear, '#run'], '#add'],
  '09_clear1k': [[...runClear, '#run'], '#clear'],
};
const hook = `(() => {
  const C = window.__dc = {}; let on = false; window.__dcOn = v => { on = v; for (const k in C) delete C[k]; };
  const bump = k => { if (on) C[k] = (C[k] || 0) + 1; };
  const wrapM = (P, n, tag) => { const f = P[n]; if (!f) return; P[n] = function (...a) { bump(tag || n); return f.apply(this, a); }; };
  const wrapA = (P, n, get, set) => { const d = Object.getOwnPropertyDescriptor(P, n); if (!d) return;
    Object.defineProperty(P, n, { configurable: true, enumerable: d.enumerable,
      get: d.get && function () { if (get) bump('get ' + n); return d.get.call(this); },
      set: d.set && function (v) { if (set) bump('set ' + n); d.set.call(this, v); } }); };
  for (const n of ['cloneNode', 'insertBefore', 'appendChild', 'removeChild', 'replaceChild', 'replaceChildren', 'contains']) wrapM(Node.prototype, n);
  for (const n of ['remove', 'append', 'prepend', 'before', 'after', 'replaceWith', 'setAttribute', 'removeAttribute', 'getAttribute', 'querySelector', 'querySelectorAll', 'matches', 'closest', 'addEventListener']) wrapM(Element.prototype, n);
  for (const n of ['createElement', 'createTextNode', 'createDocumentFragment', 'getElementById', 'querySelector', 'querySelectorAll']) wrapM(Document.prototype, n, 'document.' + n);
  for (const n of ['firstChild', 'lastChild', 'nextSibling', 'previousSibling', 'parentNode', 'childNodes']) wrapA(Node.prototype, n, true, false);
  for (const n of ['nodeValue', 'textContent']) wrapA(Node.prototype, n, true, true);
  for (const n of ['className', 'id']) wrapA(Element.prototype, n, true, true);
  for (const n of ['children', 'firstElementChild', 'nextElementSibling', 'previousElementSibling', 'tagName']) wrapA(Element.prototype, n, true, false);
  wrapA(Node.prototype, 'parentElement', true, false);
  wrapA(CharacterData.prototype, 'data', true, true);
})();`;
const b = await launch();
const res = {};
try {
  
  for (const [name, [init, target]] of Object.entries(B)) {
    if (only && !only.some(o => name.startsWith(o))) continue;
    for (const f of fws) {
      await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${f}/index.html` });
      for (let i = 0; i < 100 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(50);
      const c = await b.evaluate(`(async () => {
        const frame = () => new Promise(r => { const t = setTimeout(r, 200); requestAnimationFrame(() => { clearTimeout(t); setTimeout(r, 0); }); });
        for (const s of ${JSON.stringify(init)}) { document.querySelector(s).click(); await frame(); }
        const el = document.querySelector(${JSON.stringify(target)});
        await frame();
        const x = window.__x, st = x.cd_opstat ? new Uint32Array(x.memory.buffer, x.cd_opstat(), 32) : null;
        st && st.fill(0); x.cd_syncs();
        el.click();
        const out = { syncs: x.cd_syncs() }; if (st) new Uint32Array(x.memory.buffer, x.cd_opstat(), 32).forEach((v, i) => { if (v) out['op' + i] = v; }); return out;
      })()`);
      res[`${name} ${f}`] = c;
      const tot = Object.values(c).reduce((s, x) => s + x, 0);
      console.log(`${name.padEnd(20)} ${f.padEnd(10)} total ${String(tot).padStart(6)}  ` + Object.entries(c).sort((x, y) => y[1] - x[1]).map(([k, v]) => `${k}:${v}`).join(' '));
    }
  }
} finally { await b.close(); }
