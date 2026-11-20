// Runs tests/out/fptoint.html (tests/fptoint.c) on the tier LEGACY= picks (cdp.mjs): every float to integer cast
// clang emits as a non-trapping conversion (to int32, uint32, int64 and uint64, from f64 and f32), on values in range and
// near the limits, against the answer WebAssembly gives (what the first tier computes and these are compared with):
// the value truncated toward zero. Out of range is undefined in C: the first tier saturates (and NaN is 0), and the others
// are only required to answer (no throw, no hang), as docs/ie6-design.md ("Float to integer") says. Exit 1 on any failure.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep, TIER } from './cdp.mjs';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(d); }); }).listen(0);
const b = await launch();
let fails = 0;
const ok = (c, m, why = '') => { if (!c) { fails++; console.log('FAIL', m + (why ? ': ' + why : '')); } else console.log('ok  ', m); };
const E = b.evaluate;
// What WebAssembly's saturating conversion gives: the integer part, clamped to [lo, hi]; NaN is 0.
const sat = (v, lo, hi) => { if (Number.isNaN(v)) return 0n; if (v >= Number(hi)) return hi; if (v <= Number(lo)) return lo; return BigInt(Math.trunc(v)); };
const I64 = [-(1n << 63n), (1n << 63n) - 1n], U64 = [0n, (1n << 64n) - 1n], I32 = [-(1n << 31n), (1n << 31n) - 1n], U32 = [0n, (1n << 32n) - 1n];
const TWO63 = 2 ** 63, TWO64 = 2 ** 64;
const VALUES = [0, 0.5, 0.999, 1, 1.5, 2.5, -0.5, -1, -1.5, 255.9, 65535.5, 2147483646.9, 2147483647, 2147483647.5, 2147483648, 4294967295, 4294967295.9,
  4294967296, 4294967297, 1e10, 123456789012345, 2 ** 52 + 1, 2 ** 53, 2 ** 53 + 2, 1e18, 9007199254740993, 2 ** 62, TWO63 - 1024, TWO63, TWO64 - 2048,
  TWO64, 1e20, -2147483648, -2147483648.9, -2147483649, -4294967296, -1e10, -(2 ** 62), -TWO63, -TWO63 - 2048, -1e20, Infinity, -Infinity, NaN];
const pageUrl = `http://127.0.0.1:${srv.address().port}/fptoint.html`;
try {
  await b.send('Page.navigate', { url: pageUrl });
  for (let i = 0; i < 200 && !(await E('!!window.READY')); i++) await sleep(50);
  ok(await E('!!window.READY'), `the page started (tier ${TIER})`, JSON.stringify(await E('window.ERRORS')));
  const call = async (fn, ...args) => E(`X.${fn}(${args.map((a) => Number.isNaN(a) ? 'NaN' : a === Infinity ? 'Infinity' : a === -Infinity ? '-Infinity' : a).join(',')}) >>> 0`);
  const word = async (v) => E(`(function (v) { return [X.u64_lo(v) >>> 0, X.u64_hi(v) >>> 0, X.i64_lo(v) >>> 0, X.i64_hi(v) >>> 0, X.u32(v) >>> 0, X.i32(v) | 0]; })(${Number.isNaN(v) ? 'NaN' : v === Infinity ? 'Infinity' : v === -Infinity ? '-Infinity' : v})`);
  let bad = [], inRange = 0, outRange = 0;
  for (const v of VALUES) {
    const [ul, uh, il, ih, u32, i32] = await word(v);
    const u64 = BigInt(ul) | (BigInt(uh) << 32n), i64 = BigInt.asIntN(64, BigInt(il) | (BigInt(ih) << 32n));
    const kinds = [['u64', U64, u64], ['i64', I64, i64], ['u32', U32, BigInt(u32)], ['i32', I32, BigInt(i32)]];
    for (const [name, [lo, hi], got] of kinds) {
      const want = sat(v, lo, hi);
      // The integer part is inside the type: every tier gives it. Outside, C leaves it undefined: WebAssembly saturates.
      const inside = Number.isFinite(v) && BigInt(Math.trunc(v)) >= lo && BigInt(Math.trunc(v)) <= hi;
      if (inside) inRange++; else outRange++;
      if ((inside || TIER === 1) && got !== want) bad.push(`${name}(${v}): got ${got}, want ${want}`);
    }
  }
  ok(!bad.length, `${VALUES.length} values through u64, i64, u32 and i32: in range exact${TIER === 1 ? ', out of range saturated, NaN 0' : ''} (${inRange} in range, ${outRange} not)`, bad.slice(0, 60).join('; '));
  // The cast that stopped an app's build: (uint64_t)(v * (double)scale + 0.5).
  bad = [];
  for (const [v, scale] of [[1.5, 1000], [0.0004, 1000], [12345.678, 1000000], [4294967.295, 1000], [123456.789, 1000000000], [0, 1], [3, 7]]) {
    const [lo, hi] = await E(`[X.scaled_lo(${v}, ${scale}) >>> 0, X.scaled_hi(${v}, ${scale}) >>> 0]`);
    const want = BigInt(Math.trunc(v * scale + 0.5)), got = BigInt(lo) | (BigInt(hi) << 32n);
    if (got !== want) bad.push(`${v} * ${scale} + 0.5: got ${got}, want ${want}`);
  }
  ok(!bad.length, 'a fixed-point round, (uint64_t)(v * (double)scale + 0.5), exact', bad.join('; '));
  // From a float: the value is the float's own.
  const f32 = await E('[X.f32_u64_lo(3.75), X.f32_u64_lo(16777216), X.f32_i32(-7.9), X.f32_i32(2147483520), X.f32_i32(NaN), X.f32_i32(2147483648)].map(function (n) { return n | 0; })');
  ok(f32.slice(0, 4).join() === [3, 16777216, -7, 2147483520].join() && (TIER !== 1 ? true : f32[4] === 0 && f32[5] === 2147483647), 'from a float (f32): truncated' + (TIER === 1 ? ', saturated, NaN 0' : '') + ' (an f32 in range, 2147483520 the largest below 2^31, is exact)', JSON.stringify(f32));
  ok((await E('window.ERRORS')).length === 0, 'no page error');
} finally { await b.close(); srv.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
