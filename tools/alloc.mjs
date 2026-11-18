// Runs tests/out/alloc_try.wasm and alloc_own.wasm (tests/alloc_try.c, built by tests/build.sh) in Node:
// the _try forms answer 0 at the heap's cap (-DCDOM_HEAP_PAGES_MAX=40) where malloc, cd_alloc and realloc
// still trap, a refused grow keeps its block, and on an app's own allocator they answer what it does.
// Each case gets a fresh instance: a trap leaves the heap as it was mid-call.
import fs from 'node:fs';
import path from 'node:path';
const out = path.join(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const errors = [], passed = [];
// The module imports cdom's shim calls it never makes here: each a function that answers 0.
const stubs = new Proxy({}, { get: () => () => 0 });
const fresh = async (name) => {
  const mod = new WebAssembly.Module(fs.readFileSync(path.join(out, name)));
  const imports = {};
  for (const { module } of WebAssembly.Module.imports(mod)) imports[module] = stubs;
  return new WebAssembly.Instance(mod, imports).exports;
};
const traps = (f) => { try { f(); return 'no trap'; } catch (e) { return e instanceof WebAssembly.RuntimeError ? 'trap' : String(e); } };
const check = (name, ok, why) => (ok ? passed : errors).push(ok ? name : `${name}: ${why}`);
// A call that traps where it must not fails its own case, by name, instead of ending the run.
const call = (f) => { try { return f(); } catch (e) { return 'threw ' + e.message; } };
{
  const x = await fresh('alloc_try.wasm');
  const n = call(() => x.fill()), p = x.pages();
  check('cd_alloc_try: blocks until the heap has no room, then 0 (never a grow past the cap), and a freed block comes back', n > 20 && p <= 40, `${n} blocks, ${p} pages`);
}
{
  const x = await fresh('alloc_try.wasm');
  const g = call(() => x.grow()), h = call(() => x.huge());
  check('cd_realloc_try: a grow past the cap answers 0 and leaves the block as it was, its bytes kept; a grow that fits moves them', g === 1, 'grow answered ' + g);
  check('cd_alloc_try: a size past what a size can be answers 0', h === 1, 'answered ' + h);
}
for (const [f, what] of [['malloc_full', 'malloc'], ['alloc_full', 'cd_alloc'], ['realloc_full', 'realloc']]) {
  const x = await fresh('alloc_try.wasm');
  const r = traps(() => x[f]());
  check(`${what} still traps when the heap has no room (it never answers 0 for code that would write there)`, r === 'trap', r);
}
{
  const x = await fresh('alloc_own.wasm');
  const o = call(() => x.own());
  check('cd_alloc_try and cd_realloc_try on an app\'s own allocator answer what its malloc and realloc do', o === 1, 'own answered ' + o);
}
// The heap's large blocks (tests/alloc_mem.c, docs/file-read-design.md for the predictions): the pages each
// case took. 36,578,304 bytes is 558 pages (with the 4 KiB grain and the room free at the start); a power of
// two would be 1,024.
{
  const pagesOf = async (name) => { const x = await fresh('alloc_mem.wasm'); return call(() => x[name]() >>> 0); };
  const [s1, s2, s3, s4, s5, s6, s7, s8] = await Promise.all(['s1', 's2', 's3', 's4', 's5', 's6', 's7', 's8'].map(pagesOf));
  check('a large block costs its size rounded up to 4 KiB, and a freed one serves the next: 36.6 MB taken and freed twice grows the memory 558 pages, not 1,024', s1 === 558, `${s1} pages`);
  check('a freed large block serves a smaller large one: 36.6 MB, 20 MB, 36.6 MB, each freed first, take 558 pages', s2 === 558, `${s2} pages`);
  check('a large block freed below the top serves the next large request that fits, at its address, and the rest of it comes back with that block', s3 === 558, `${s3 & 0x7fffffff} pages${s3 >>> 31 ? ', not at its address' : ''}`);
  check('a block at the top grows where it is: cd_realloc doubling from 1 MiB to 64 MiB takes 1,024 pages, not 4,064', s4 === 1024, `${s4} pages`);
  check('two freed large blocks side by side merge: one of both their sizes takes their place', s5 === 610, `${s5 & 0x7fffffff} pages${s5 >>> 31 ? ', not at their place' : ''}`);
  check('a freed large block merges with free neighbours on both sides: three of 20 MB, the middle one freed last, hold one of 60 MB', s7 === 915, `${s7 & 0x7fffffff} pages${s7 >>> 31 ? ', not at their place' : ''}`);
  check('a free run under the top goes back with the top block freed after it: 100,000 small blocks fit in their room', s8 === 0, `${s8} pages`);
  check('a large block freed at the top gives its room back: 100,000 small blocks after it grow nothing', s6 === 0, `${s6} pages`);
}
// An answer's bytes, as the shim delivers them (cd_bytes_buf, then cd_net_http): freed once the callback
// returned, so two 20 MB answers and then 20 MB of the app's own take the room of one.
{
  let token = -1;
  const mod = new WebAssembly.Module(fs.readFileSync(path.join(out, 'alloc_mem.wasm')));
  const imports = {};
  for (const { module } of WebAssembly.Module.imports(mod)) imports[module] = new Proxy({}, { get: (_, k) => k === 'http' ? (t) => { token = t; } : () => 0 });
  const x = new WebAssembly.Instance(mod, imports).exports;
  const p0 = x.pages(), n = 20000000;
  for (let i = 0; i < 2; i++) {
    x.ask();
    const ptr = x.cd_bytes_buf(n);
    new Uint8Array(x.memory.buffer, ptr, n).fill(7);
    x.cd_net_http(token, 200, ptr, n);
  }
  const answers = x.pages() - p0;
  x.take20();
  const all = x.pages() - p0;
  check('an answer\'s bytes are freed once its callback returned: two 20 MB answers, then 20 MB of the app\'s, grow the memory 305 pages in all', x.answered() === 2 && answers === 305 && all === 305,
    `${x.answered()} answered, ${answers} pages after the answers, ${all} after the app's block`);
}
// The same for every other export the shim hands cd_bytes_buf's block to: two 20 MB deliveries take one
// block's room (305 pages; 610 if the first were kept).
// cd_file_got and cd_file_fitted take cd_file_room's block (src/cdom_file.c) the same way.
for (const [name, answer, room] of [['cd_net_msg', (x, p, n) => x.cd_net_msg(3, p, n)], ['cd_encoded', (x, p, n) => x.cd_encoded(1, p, n)],
  ['cd_store_got', (x, p, n) => x.cd_store_got(1, p, n)], ['cd_url_changed', (x, p, n) => x.cd_url_changed(p, n)],
  ['cd_file_got', (x, p, n) => x.cd_file_got(1, 0, p, n), 'cd_file_room'], ['cd_file_fitted', (x, p, n) => x.cd_file_fitted(1, 0, p, n, 9, 9, 0), 'cd_file_room']]) {
  const x = await fresh('alloc_mem.wasm');
  const p0 = x.pages(), n = 20000000;
  const r = call(() => { for (let i = 0; i < 2; i++) answer(x, x[room || 'cd_bytes_buf'](n), n); return x.pages() - p0; });
  check(`${name} frees the bytes it was handed once its hook returned: two 20 MB deliveries grow the memory 305 pages`, r === 305, `${r} pages`);
}
// Blocks of every size taken, grown and freed in a seeded order: none ever holds another's bytes.
{
  const x = await fresh('alloc_mem.wasm');
  x.cd_start(20261118);
  const r = call(() => x.churn(20000));
  check('20,000 seeded steps of small and large blocks taken, grown, shrunk and freed: every block keeps its own bytes', r === 0, typeof r === 'number' ? `step ${r} found another block's bytes` : r);
}
for (const p of passed) console.log('ok  ', p);
for (const e of errors) console.log('FAIL', e);
console.log(`${passed.length} passed, ${errors.length} failed`);
process.exit(errors.length ? 1 : 0);
