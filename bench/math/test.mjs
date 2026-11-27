// The math bench's tests (docs/math-design.md, "Tests"), in node: needs make -C bench/math first.
//   1. The special-value test (special.c) of every arm's wasm module, the browser's arm (js1) with node's Math.*,
//      and of every native twin and glibc (build/native/bench special).
//   2. The wasm-equals-native check: each arm's wasm module and its native twin hash their results on the same
//      inputs (bm_digest: N per entry and domain, default 100,000, NaNs as one NaN); every hash must be the same.
// Usage: node bench/math/test.mjs [n=100000]   Prints each failure, then a summary line; exits 1 on any failure.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const here = path.dirname(new URL(import.meta.url).pathname);
const n = +(process.argv[2] || 100000);
await import('./glue.js');
const mb = globalThis.mathBench;
let bad = 0;
const fail = (m) => { bad++; console.log(`FAIL ${m}`); };
const DOMS = [[1, 'small'], [2, 'typical'], [4, 'large']];

// 1. special values
const wasm = {};
for (const arm of ['musl', 'msun', 'core', 'ours', 'js1']) {
  const inst = await mb.instantiate(arm, fs.readFileSync(path.join(here, 'build', `${arm}.wasm`)));
  if (arm !== 'js1') wasm[arm] = inst;
  const f = inst.exports.sp_run();
  const text = mb.cstr(inst, inst.exports.sp_text());
  console.log(`special ${arm} (wasm): ${inst.exports.sp_count()} cases, ${f} failure${f === 1 ? '' : 's'}`);
  for (const line of text.split('\n').filter(Boolean)) fail(`special ${arm} (wasm): ${line}`);
  if (f && !text) fail(`special ${arm} (wasm): ${f} failures and no report`);
}
let native = '';
try { native = execFileSync(path.join(here, 'build/native/bench'), ['special'], { encoding: 'utf8' }); }
catch (e) { native = e.stdout || ''; if (!native) fail(`special (native): ${e.message}`); }
let arm = '';
for (const line of native.split('\n').filter(Boolean)) {
  const m = /^(\w+): (\d+) failures?$/.exec(line);
  if (m) { arm = m[1]; console.log(`special ${arm} (native): ${m[2]} failure${m[2] === '1' ? '' : 's'}`); continue; }
  fail(`special ${arm} (native): ${line}`);
}

// 2. wasm equals native
const want = new Map();
const digests = execFileSync(path.join(here, 'build/native/bench'), ['digest', String(n)], { encoding: 'utf8', maxBuffer: 1 << 26 });
for (const line of digests.split('\n').filter(Boolean)) {
  const [a, entry, dom, hex] = line.split('\t');
  want.set(`${a} ${entry} ${dom}`, hex);
}
let compared = 0;
for (const [a, inst] of Object.entries(wasm)) {
  const x = inst.exports, diff = [];
  for (let e = 0; e < x.bm_n(); e++)
    for (const [d, dn] of DOMS) {
      if (!(x.bm_doms(e) & d)) continue;
      const name = mb.cstr(inst, x.bm_name(e)), key = `${a} ${name} ${dn}`;
      const got = BigInt.asUintN(64, x.bm_digest(e, d, n)).toString(16).padStart(16, '0');
      compared++;
      if (!want.has(key)) fail(`twin ${key}: no native digest`);
      else if (want.get(key) !== got) diff.push(`${name} ${dn}`);
    }
  if (diff.length) fail(`twin ${a}: wasm differs from native in ${diff.join(', ')}`);
  console.log(`twin ${a}: ${diff.length ? `${diff.length} differ` : 'all identical'} (${n} inputs per entry and domain)`);
}
console.log(bad ? `math bench tests: ${bad} failure${bad === 1 ? '' : 's'}` : `math bench tests: all pass (${compared} digests compared)`);
process.exit(bad ? 1 : 0);
