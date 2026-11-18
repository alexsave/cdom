// Runs the pages tablegen.mjs wrote, one fresh headless Chrome per load, R rounds interleaved; per load:
// the table script (t1 - t0: parse, compile, run), the install (t2 - t1), the first use of every slot,
// the JS heap after a GC after the install and after the use, and the page's brotli bytes.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const here = path.dirname(fileURLToPath(import.meta.url));
const R = +(process.env.R || 7);
const files = fs.readdirSync(path.join(here, 'out', 'table')).filter((f) => f.endsWith('.html') && new RegExp(process.env.FILTER || '.').test(f)).sort();
const srv = serve();
const br = Object.fromEntries(files.map((f) => [f, execFileSync('brotli', ['-q', '11', '-c', path.join(here, 'out', 'table', f)]).length]));
const rows = [];
console.log('page\tround\ttableMs\tinstallMs\tuseMs\theapKB\theapUsedKB\tbrotli');
for (let r = 0; r < R; r++)
  for (const f of files) {
    const b = await launch();
    try {
      await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/strings/out/table/${f}` });
      for (let i = 0; i < 300 && !(await b.evaluate('performance.getEntriesByName("t2").length > 0')); i++) await sleep(20);
      const [t0, t1, t2] = await b.evaluate('["t0","t1","t2"].map(n => performance.getEntriesByName(n)[0].startTime)');
      await b.send('HeapProfiler.collectGarbage');
      const heap = (await b.send('Runtime.getHeapUsage')).result.usedSize / 1024;
      const use = await b.evaluate('(() => { const t = performance.now(); window.__n = useAll(); return performance.now() - t; })()');
      await b.send('HeapProfiler.collectGarbage');
      const heap2 = (await b.send('Runtime.getHeapUsage')).result.usedSize / 1024;
      const row = [f.replace('.html', ''), r, t1 - t0, t2 - t1, use, heap, heap2, br[f]];
      rows.push(row);
      console.log(row.map((x) => (typeof x === 'number' ? +x.toFixed(3) : x)).join('\t'));
    } finally { await b.close(); }
  }
const med = (a) => { const s = [...a].sort((x, y) => x - y); return s[s.length >> 1]; };
console.log('# medians');
for (const f of files) {
  const g = rows.filter((x) => x[0] === f.replace('.html', ''));
  console.log([g[0][0], g.length, ...[2, 3, 4, 5, 6].map((k) => +med(g.map((x) => x[k])).toFixed(3)), g[0][7]].join('\t'));
}
srv.close();
