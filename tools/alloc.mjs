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
for (const p of passed) console.log('ok  ', p);
for (const e of errors) console.log('FAIL', e);
console.log(`${passed.length} passed, ${errors.length} failed`);
process.exit(errors.length ? 1 : 0);
