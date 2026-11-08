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
ok(x.own_libc() === 1, "an app's own memcmp, strlen, memcpy, memmove and memset link next to mini.c");
for (const f of ['cdom', 'freestanding']) {
  x = await load(`${dir}/libc_header_${f}.wasm`);
  ok(x.libc_header() === 1, `string.h (${f} flags): memcpy, memmove (overlapping), memset, memcmp and strlen give the right bytes`);
  x = await load(`${dir}/libc_addr_${f}.wasm`);
  ok(x.libc_addr() === 1, `string.h (${f} flags): memcpy, memmove and memset called through their address give the right bytes`);
}
x = await load(`${dir}/on_only.wasm`);
x.cd_start(0);
const inbox = new Uint32Array(x.memory.buffer, x.cd_layout(), 3)[2] >> 2, rec = new Int32Array(x.memory.buffer);
const event = (type, id) => { rec[inbox] = type; rec[inbox + 1] = id; x.cd_event(); };
event(0, x.node()); // CD_EV_CLICK: the handler
event(0, x.node() + 1); // no handler: cdom's empty cd_on_event
event(2, x.node()); // another type: the same
ok(x.heard() === 1, `an app that uses cd_on alone links with no cd_on_event, and only its handler's event reaches it (heard ${x.heard()})`);
process.exit(bad ? 1 : 0);
