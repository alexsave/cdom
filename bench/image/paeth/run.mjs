// The unfilter microbench (bench.c): p12.png's filtered rows (inflated here, by node's zlib) unfiltered by each loop in
// each module, interleaved rounds, the median ms of each. Every arm's result must be the same bytes (a byte sum).
// Usage: node bench/image/paeth/run.mjs [rounds=9] [input=bench/image/inputs/p12.png]
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
const here = path.dirname(new URL(import.meta.url).pathname);
const rounds = +(process.argv[2] || 9);
const png = fs.readFileSync(process.argv[3] || path.join(here, '../inputs/p12.png'));
let at = 8, w = 0, h = 0, ct = 0;
const idat = [];
while (at < png.length) {
  const n = png.readUInt32BE(at), t = png.toString('latin1', at + 4, at + 8), d = png.subarray(at + 8, at + 8 + n);
  if (t === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; }
  if (t === 'IDAT') idat.push(d);
  at += 12 + n;
}
const bpp = ct === 6 ? 4 : 3, rows = zlib.inflateSync(Buffer.concat(idat));
const paeth = [...Array(h).keys()].filter((r) => rows[r * (w * bpp + 1)] === 4).length;
console.log(`# ${w} x ${h}, ${bpp} bytes a pixel, ${paeth} of ${h} rows Paeth; node ${process.version}`);
const mods = {};
for (const m of ['scalar', 'simd-lag2', 'simd-lag1']) {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(path.join(here, 'out', m + '.wasm')), {});
  const x = instance.exports, p = x.setup(w, h, bpp);
  new Uint8Array(x.memory.buffer, p, rows.length).set(rows);
  mods[m] = x;
}
// arm: [label, module, loop, most]
const arms = [
  ['scalar, a row at a time', 'scalar', 0, 1],
  ['simd, a row at a time (main)', 'simd-lag2', 1, 1], ['simd wavefront lag 2, runs of 4', 'simd-lag2', 3, 4],
  ['simd wavefront lag 1, runs of 4', 'simd-lag1', 3, 4], ['simd wavefront lag 2, runs of 3', 'simd-lag2', 3, 3],
  ['simd wavefront lag 2, runs of 2', 'simd-lag2', 3, 2], ['simd lag 2, runs of 2 then 1', 'simd-lag2', 3, 0],
];
const t = arms.map(() => []), sums = new Set();
for (let r = 0; r < rounds + 1; r++)
  for (let k = 0; k < arms.length; k++) {
    const i = (k + r) % arms.length, [, m, loop, most] = arms[i], x = mods[m];
    x.reset();
    const t0 = performance.now(), s = x.unfilter(loop, most), ms = performance.now() - t0;
    sums.add(s >>> 0);
    if (r) t[i].push(ms);   // round 0 warms up (tier-up)
  }
const med = (v) => { const s = [...v].sort((a, b) => a - b); return s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
const base = med(t[1]), sbase = med(t[0]);
for (let i = 0; i < arms.length; i++)
  console.log(`${med(t[i]).toFixed(1).padStart(7)} ms  ${(med(t[i]) / base).toFixed(3)} of simd row  ${(med(t[i]) / sbase).toFixed(3)} of scalar row  (${Math.min(...t[i]).toFixed(1)} to ${Math.max(...t[i]).toFixed(1)})  ${arms[i][0]}`);
console.log(sums.size === 1 ? 'every arm the same bytes' : `FAIL the arms differ: ${sums.size} sums`);
process.exit(sums.size === 1 ? 0 : 1);
