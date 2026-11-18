// Runs micro-s.html for each configuration, one fresh navigation each, in one headless Chrome; TSV out:
// median ns per cd_text and the quartiles of 21 samples. Usage: node bench/strings/micro-s.mjs [filter]
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const filter = new RegExp(process.argv[2] || '.');
const configs = [];
for (const test of ['lit8', 'ternary8', 'helper8', 'table8']) configs.push({ mode: 'resolve', test });
for (const kind of ['ascii', 'utf8'])
  for (const len of [1, 9, 12, 13, 30, 64, 200]) {
    if (kind === 'utf8' && len < 2) continue;
    configs.push({ mode: 'resolve', test: 'tmp', len, kind });
    if (len >= 9 && len <= 30) configs.push({ mode: 'resolve', test: 'ahead', len, kind });
  }
for (const test of ['lit8', 'ternary8', 'helper8']) configs.push({ mode: 'resolve', test, split: 1 });
for (const len of [9, 30]) configs.push({ mode: 'resolve', test: 'tmp', len, kind: 'ascii', split: 1 });
for (const test of ['lit8', 'ternary8']) configs.push({ mode: 'dom', test });
for (const len of [9, 30]) configs.push({ mode: 'dom', test: 'tmp', len, kind: 'ascii' }, { mode: 'dom', test: 'ahead', len, kind: 'ascii' });
const srv = serve();
const b = await launch();
try {
  console.log(`# ${b.version}`);
  console.log(['mode', 'test', 'kind', 'len', 'ns', 'q1', 'q3', 'n'].join('\t'));
  for (const c of configs) {
    const key = Object.entries(c).map(([k, v]) => `${k}=${v}`).join('&');
    if (!filter.test(key)) continue;
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/strings/micro-s.html?${key}` });
    let r;
    for (let i = 0; i < 600 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
    if (!r || r.error) { console.log('#ERROR', key, r && r.error); continue; }
    console.log([c.mode + (c.split ? '/split' : ''), c.test, c.kind || '', c.len || '', r.ns.toFixed(2), r.lo.toFixed(2), r.hi.toFixed(2), r.n].join('\t'));
  }
} finally { await b.close(); srv.close(); }
