// The literal table's carrier at scale: writes out/table/<form>-<N>.html for N unique strings (3 to 30
// bytes, one in ten with non-ASCII text, from one seeded xorshift32) and 1.25 N slots, in each form:
//   a   the design: one string joined by \0, split at load, and a slot map string split and resolved
//   b   an array literal of the unique strings, and the same slot map
//   c   one string kept whole; offsets found at install (indexOf), each slot sliced on first use and cached
//   d   dense ids: the joined string split at load, no map (the build numbers unique strings)
//   e   dense ids as an array literal: no install at all, the parser builds the table
//   f   dense ids, lazy: c's string and offsets with no map
//   g   dense ids, two joined strings: the ASCII strings (numbered first) and the rest, split at load
// Each page: script 1 marks t0, script 2 is the table, script 3 marks t1 (so t1 - t0 is the table
// script's parse, compile and run), installs (t2), and window.useAll() reads every slot once.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const out = path.join(here, 'out', 'table');
fs.mkdirSync(out, { recursive: true });
let seed = 0x7ab1e5;
const rnd = () => { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return seed; };
const WORDS = ['div', 'span', 'button', 'row', 'col', 'card', 'title', 'Hello', 'Create', 'rows', 'remove', 'danger',
  'primary', 'block', 'small', 'large', 'menu', 'item', 'label', 'value', 'header', 'footer', 'link', 'image'];
const INTL = ['Привет', 'Grüße', 'こんにちは', 'café', 'naïve', 'Ελλάδα', '你好', 'Ünïcödé'];
export function strings(n) {
  const s = [], seen = new Set();
  while (s.length < n) {
    const want = 3 + (rnd() % 28); // target bytes, 3 to 30
    let v = rnd() % 10 === 0 ? INTL[rnd() % INTL.length] : WORDS[rnd() % WORDS.length];
    while (Buffer.byteLength(v) < want - 4) v += (rnd() & 1 ? ' ' : '-') + WORDS[rnd() % WORDS.length];
    v += ' ' + (s.length).toString(36);
    if (!seen.has(v)) { seen.add(v); s.push(v); }
  }
  return s;
}
const js = (v) => '"' + v.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/</g, '\\x3c') + '"';
const INSTALL = {
  a: `const u = T[0].split("\\0"), m = T[1].split(","); for (let i = 0; i < m.length; i++) m[i] = u[m[i]]; window.lits = m;
      window.useAll = () => { let n = 0; for (let i = 0; i < lits.length; i++) n += lits[i].length; return n; };`,
  b: `const u = T[0], m = T[1].split(","); for (let i = 0; i < m.length; i++) m[i] = u[m[i]]; window.lits = m;
      window.useAll = () => { let n = 0; for (let i = 0; i < lits.length; i++) n += lits[i].length; return n; };`,
  c: `const all = T[0], m = T[1].split(","), k = new Int32Array(m.length);
      for (let i = 0; i < m.length; i++) k[i] = +m[i];
      const off = []; for (let at = 0; at <= all.length; ) { off.push(at); const z = all.indexOf("\\0", at); at = z < 0 ? all.length + 1 : z + 1; }
      const cache = [];
      window.lit = (i) => { const u = k[i]; return cache[u] ??= all.slice(off[u], off[u + 1] - 1); };
      window.nslots = m.length;
      window.useAll = () => { let n = 0; for (let i = 0; i < nslots; i++) n += lit(i).length; return n; };`,
  d: `window.lits = T.split("\\0");
      window.useAll = () => { let n = 0; for (let i = 0; i < lits.length; i++) n += lits[i].length; return n; };`,
  e: `window.lits = T;
      window.useAll = () => { let n = 0; for (let i = 0; i < lits.length; i++) n += lits[i].length; return n; };`,
  g: `window.lits = T[0].split("\\0").concat(T[1].split("\\0"));
      window.useAll = () => { let n = 0; for (let i = 0; i < lits.length; i++) n += lits[i].length; return n; };`,
  f: `const all = T, off = []; for (let at = 0; at <= all.length; ) { off.push(at); const z = all.indexOf("\\0", at); at = z < 0 ? all.length + 1 : z + 1; }
      const cache = []; window.nslots = off.length;
      window.lit = (u) => cache[u] ??= all.slice(off[u], off[u + 1] - 1);
      window.useAll = () => { let n = 0; for (let i = 0; i < nslots; i++) n += lit(i).length; return n; };`,
};
// z: no table, the heap and time floor of the page itself
fs.writeFileSync(path.join(out, 'z-0.html'), `<!doctype html><html><head><meta charset="utf-8"><title>table z</title></head><body><p>table</p>
<script>performance.mark("t0");</script>
<script>var T=0;</script>
<script>performance.mark("t1"); window.useAll = () => 0; performance.mark("t2");</script>
</body></html>\n`);
for (const N of [10, 1000, 10000, 100000]) {
  seed = 0x7ab1e5 + N;
  const u = strings(N);
  const S = Math.round(N * 1.25);
  const map = []; for (let i = 0; i < S; i++) map.push(i < N ? i : rnd() % N);
  const joined = js(u.join('\0')).replace(/\0/g, '\\x00');
  const arr = '[' + u.map(js).join(',') + ']';
  const tables = {
    a: `[${joined},"${map.join(',')}"]`, b: `[${arr},"${map.join(',')}"]`, c: `[${joined},"${map.join(',')}"]`,
    d: joined, e: arr, f: joined,
    // g: dense ids numbered ASCII first, so the ASCII strings come from a one-byte string
    g: '[' + [u.filter((v) => !/[^\x00-\x7f]/.test(v)), u.filter((v) => /[^\x00-\x7f]/.test(v))].map((p) => js(p.join('\0')).replace(/\0/g, '\\x00')).join(',') + ']',
  };
  for (const f of Object.keys(INSTALL)) {
    const page = `<!doctype html><html><head><meta charset="utf-8"><title>table ${f} ${N}</title></head><body><p>table</p>
<script>performance.mark("t0");</script>
<script>var T=${tables[f]};</script>
<script>performance.mark("t1"); { ${INSTALL[f]} } T = undefined; performance.mark("t2");</script>
</body></html>\n`;
    fs.writeFileSync(path.join(out, `${f}-${N}.html`), page);
  }
}
