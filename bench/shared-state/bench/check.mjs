// The untimed runs: (1) the DOM check: every entry runs page.html's fixed script of every op, and the
// mount's innerHTML hash must be the same in all (a mismatch is a bug in an entry; exit 1); (2) the DOM
// crossings: the wasm imports one operation calls, per (scenario, entry), split into the DOM-writing
// imports and the rest, written to ../results/crossings.tsv.
// Usage: bench/shared-state/bench/build.sh && node bench/shared-state/bench/check.mjs [dom|count]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { launch, sleep } from '../../../tools/cdp.mjs';
import { serve } from '../../strings/serve.mjs';
import { SCENARIOS } from './scenarios.mjs';
const here = path.dirname(fileURLToPath(import.meta.url));
const what = process.argv[2] || 'all';
const WRITES = new Set(['open', 'close', 'text', 'text_num', 'attr', 'class', 'value', 'value_num', 'clone', 'tpl_text',
  'tpl_text_num', 'set_text', 'set_text_num', 'set_attr', 'set_class', 'style', 'style_num', 'remove', 'move', 'clear',
  'detach', 'attach', 'set', 'call', 'blit', 'image_bytes', 'encode', 'walk_value', 'walk_value_num']);
const srv = serve();
const b = await launch();
let bad = 0;
async function page(entry, sc, mode, limit = 120) {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/shared-state/bench/page.html?entry=${entry}&sc=${sc}&mode=${mode}` });
  let r;
  for (let i = 0; i < limit * 10 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
  return r || { error: 'TIMEOUT' };
}
try {
  if (what === 'all' || what === 'dom') {
    const seen = {};
    for (const e of ['A', 'B', 'C', 'CL', 'E', 'L', 'EI', 'LI']) {
      const r = await page(e, 'S1', 'dom');
      console.log('dom', e, r.error || `final ${r.hash} steps ${r.steps} (${r.nsteps}) len=${r.len} rows=${r.rows} mirror=${r.mirror} first=${r.first} invariants broken: ${r.bad.length ? r.bad.join('; ') : 'none'}`);
      if (r.error) { bad++; continue; }
      if (r.bad.length) bad++;
      seen[r.hash + '/' + r.steps] = (seen[r.hash + '/' + r.steps] || []).concat(e);
    }
    const n = Object.keys(seen).length;
    console.log(n === 1 ? 'ok   every entry leaves the same DOM' : `FAIL the entries leave ${n} different DOMs: ${JSON.stringify(seen)}`);
    if (n !== 1) bad++;
  }
  if (what === 'all' || what === 'count') {
    const lines = [['scenario', 'entry', 'op', 'crossings', 'dom_writes', 'other', 'by_import'].join('\t')];
    for (const s of SCENARIOS) for (const e of s.entries) {
      const r = await page(e, s.sc, 'count');
      if (r.error) { console.log('count', s.sc, e, r.error); lines.push([s.sc, e, '', 'ERROR', '', '', r.error.split('\n')[0]].join('\t')); continue; }
      const one = (label, calls) => {
        const all = Object.values(calls).reduce((a, c) => a + c, 0);
        const w = Object.entries(calls).filter(([k]) => WRITES.has(k)).reduce((a, [, c]) => a + c, 0);
        const by = Object.entries(calls).sort((p, q) => q[1] - p[1]).map(([k, c]) => `${k}:${c}`).join(' ');
        lines.push([s.sc, e, label, all, w, all - w, by].join('\t'));
        console.log(s.sc, e, label, all, w, all - w, by);
      };
      if (r.calls2) { one('build', r.calls); one('clear', r.calls2); } else one('op', r.calls);
    }
    fs.mkdirSync(path.join(here, '../results'), { recursive: true });
    fs.writeFileSync(path.join(here, '../results/crossings.tsv'), lines.join('\n') + '\n');
  }
} finally { await b.close(); srv.close(); }
process.exit(bad ? 1 : 0);
