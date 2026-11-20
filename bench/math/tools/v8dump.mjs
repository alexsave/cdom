// The browser's results for the accuracy sweep (docs/math-design.md, "Accuracy"): the js1 module in node (V8's
// Math.*, as Chrome's) evaluates each entry on the accuracy inputs (gen.h, the same stream accuracy.c draws) and
// writes them as raw doubles, <dir>/<entry>-<domain>.f64, for build/native/accuracy to read as the arm v8.
// Usage: node bench/math/tools/v8dump.mjs dir [n=1000000] [entries]
import fs from 'node:fs';
import path from 'node:path';

const here = path.dirname(new URL(import.meta.url).pathname);
const [dir, n = '1000000', list] = process.argv.slice(2);
if (!dir) { console.error('usage: v8dump.mjs dir [n] [entries]'); process.exit(2); }
await import('../glue.js');
const mb = globalThis.mathBench;
const inst = await mb.instantiate('js1', fs.readFileSync(path.join(here, '../build/js1.wasm')));
const x = inst.exports, want = list && list !== 'all' ? new Set(list.split(',')) : null;
fs.mkdirSync(dir, { recursive: true });
for (let e = 0; e < x.bm_n(); e++)
  for (const [d, dn] of [[1, 'small'], [2, 'typical'], [4, 'large']]) {
    const name = mb.cstr(inst, x.bm_name(e));
    if (!(x.bm_doms(e) & d) || (want && !want.has(name))) continue;
    const fd = fs.openSync(path.join(dir, `${name}-${dn}.f64`), 'w');
    x.bm_dump_begin(e, d);
    for (let done = 0; done < +n; ) {
      const k = Math.min(65536, +n - done), p = x.bm_dump_next(k);
      fs.writeSync(fd, new Uint8Array(x.memory.buffer, p, k * 8));
      done += k;
    }
    fs.closeSync(fd);
  }
