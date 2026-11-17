// Runs micro.html for every configuration in one headless Chrome, one fresh navigation each, and
// prints one TSV line per configuration: the median ns per cd_text and the quartiles of the 21 samples.
// Usage: node bench/strings/micro.mjs [filter regexp]   (bench/strings/micro.sh builds the modules first)
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const filter = new RegExp(process.argv[2] || '.');
const LENS = [1, 9, 12, 13, 30, 200];
const configs = [];
for (const mode of ['resolve', 'dom']) {
  configs.push({ mode, build: 'a', test: 'lit8' }, { mode, build: 'b', test: 'lit8' }, { mode, build: 'd', test: 'lit8' });
  if (mode === 'resolve') configs.push({ mode, build: 'b', test: 'dense8' });
  for (const kind of ['ascii', 'utf8'])
    for (const len of LENS) {
      if (kind === 'utf8' && len < 2) continue;
      configs.push({ mode, build: 'a', test: 'tmp', len, kind }, { mode, build: 'b', test: 'tmp', len, kind, read: 'loop12' });
      if (mode === 'resolve') {
        configs.push({ mode, build: 'a', test: 'intern_hit', len, kind });
        if (len >= 9) configs.push({ mode, build: 'a', test: 'intern_new', len, kind });
        for (const read of ['td', 'fcc', 'latin1']) configs.push({ mode, build: 'b', test: 'tmp', len, kind, read });
        for (const short of [16, 24, 32]) configs.push({ mode, build: 'b', test: 'tmp', len, kind, read: 'loop12', short });
      }
    }
}
// ROUND=2: the readers found after round 1 (fromCharCode with arguments, the ASCII test in C, the view
// check by length), with the baseline and the design's read again in the same pass.
if (process.env.ROUND === '2') {
  configs.length = 0;
  configs.push({ mode: 'resolve', build: 'a', test: 'lit8' }, { mode: 'resolve', build: 'b', test: 'lit8' });
  for (const kind of ['ascii', 'utf8'])
    for (const len of [1, 9, 12, 13, 30, 64, 200]) {
      if (kind === 'utf8' && len < 2) continue;
      const m = { mode: 'resolve', test: 'tmp', len, kind };
      configs.push({ ...m, build: 'a' }, { ...m, build: 'b', read: 'loop12' }, { ...m, build: 'b', read: 'loop12', chk: 'length' });
      for (const short of [32, 64]) {
        configs.push({ ...m, build: 'b', read: 'args', short }, { ...m, build: 'b2', read: 'argsC', short },
          { ...m, build: 'b2', read: 'argsC', short, chk: 'length' });
      }
    }
  for (const kind of ['ascii', 'utf8'])
    for (const len of [9, 30, 200]) {
      const m = { mode: 'dom', test: 'tmp', len, kind };
      configs.push({ ...m, build: 'a' }, { ...m, build: 'b', read: 'loop12' }, { ...m, build: 'b2', read: 'argsC', short: 64, chk: 'length' });
    }
}
const srv = serve();
const b = await launch();
try {
  console.log(`# ${b.version}`);
  console.log(['mode', 'build', 'test', 'read', 'short', 'kind', 'len', 'ns', 'q1', 'q3', 'n'].join('\t'));
  for (const c of configs) {
    const key = Object.entries(c).map(([k, v]) => `${k}=${v}`).join('&');
    if (!filter.test(key)) continue;
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/strings/micro.html?${key}` });
    let r;
    for (let i = 0; i < 600 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
    if (!r || r.error) { console.log('#ERROR', key, r && r.error); continue; }
    console.log([c.mode, c.build, c.test, (c.read || '') + (c.chk ? '/' + c.chk : ''), c.short || '', c.kind || '', c.len || '', r.ns.toFixed(2), r.lo.toFixed(2), r.hi.toFixed(2), r.n].join('\t'));
  }
} finally { await b.close(); srv.close(); }
