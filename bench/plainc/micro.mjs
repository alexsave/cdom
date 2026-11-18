// Runs bench/plainc/micro.html for every configuration in one headless Chrome, one fresh navigation each;
// one TSV line per configuration: median ns per cd_text and the quartiles of 21 samples.
// Usage: bench/plainc/build.sh && node bench/plainc/micro.mjs [filter regexp]
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const filter = new RegExp(process.argv[2] || '.');
const configs = [];
const BASE = ['today', 'val', 'shimro', 'ptr'], RO = ['romap', 'roarr', 'roobj', 'rodm'];
for (const test of ['lit8', 'terns8', 'tern8', 'help8']) {
  for (const opt of BASE) configs.push({ build: 'base', opt, test });
  for (const opt of RO) configs.push({ build: 'ro', opt, test });
}
for (const test of ['buf1', 'bufnew'])
  for (const len of [5, 12, 15]) {
    for (const opt of ['today', 'val', 'shimro']) configs.push({ build: 'base', opt, test, len });
    configs.push({ build: 'ro', opt: 'romap', test, len });
  }
const srv = serve();
const b = await launch();
const seen = {};
try {
  console.log(`# ${b.version}`);
  console.log(['build', 'opt', 'test', 'len', 'ns', 'q1', 'q3', 'n', 'cached', 'same'].join('\t'));
  for (const c of configs) {
    const key = Object.entries(c).map(([k, v]) => `${k}=${v}`).join('&');
    if (!filter.test(key)) continue;
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/plainc/micro.html?${key}` });
    let r;
    for (let i = 0; i < 600 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
    if (!r || r.error) { console.log('#ERROR', key, r && r.error); continue; }
    const k = c.test + (c.len || '');
    const same = c.test === 'bufnew' && c.opt === 'ptr' ? 'n/a' : (seen[k] ??= r.seen) === r.seen ? 'yes' : 'NO';
    console.log([c.build, c.opt, c.test, c.len || '', r.ns.toFixed(2), r.lo.toFixed(2), r.hi.toFixed(2), r.n, r.cached, same].join('\t'));
  }
} finally { await b.close(); srv.close(); }
