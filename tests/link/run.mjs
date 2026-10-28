// Runs the allocator modules tests/link/run.sh built, with every cdom import stubbed (no DOM).
import fs from 'node:fs';
const stub = new Proxy({}, { get: () => () => 0 });
const load = async (f) => (await WebAssembly.instantiate(fs.readFileSync(f), { cdom: stub, env: stub })).instance.exports;
const dir = process.argv[2];
let bad = 0;
const ok = (c, m) => { console.log(c ? 'ok  ' : 'FAIL', m); if (!c) bad++; };
let x = await load(`${dir}/alloc_cdom.wasm`);
const r0 = x.run();
ok(r0 === 0, `cdom's heap: cd_alloc, malloc and calloc interleaved never overlap (run ${r0})`);
x = await load(`${dir}/alloc_app.wasm`);
const before = x.app_mallocs(), r = x.run();
ok(r === 0, `the app's heap: the same, with cdom allocating from the app's malloc (run ${r})`);
ok(x.app_mallocs() - before > 3000, `cdom's cd_alloc went through the app's malloc (${x.app_mallocs() - before} calls)`);
x = await load(`${dir}/alloc_half.wasm`);
let trapped = false;
try { x.free_one(); } catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
ok(trapped, 'an app that replaces malloc alone traps at the first free, not silently');
x = await load(`${dir}/own_libc.wasm`);
ok(x.own_libc() === 1, "an app's own memcmp and strlen link next to mini.c");
process.exit(bad ? 1 : 0);
