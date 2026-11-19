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
ok(x.app_mallocs() - before > 1500, `the app's malloc served the interleaved blocks (${x.app_mallocs() - before} calls)`);
const grown = x.app_mallocs(); x.grow_tables();
ok(x.app_mallocs() - grown > 0, `cdom's own tables grew through the app's malloc (${x.app_mallocs() - grown} calls)`);
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
  x = await load(`${dir}/libc_printf_${f}.wasm`);
  const line = x.libc_printf();
  ok(line === 0, `stdio.h (${f} flags): snprintf and vsnprintf write every case of tests/link/libc_printf.c${line ? ` (wrong at line ${line})` : ''}`);
  x = await load(`${dir}/libc_stdlib_${f}.wasm`);
  const at = x.libc_stdlib();
  ok(at === 0, `stdlib.h (${f} flags): abs, labs (through their address too), malloc, calloc, realloc and free give the right values${at ? ` (wrong at line ${at})` : ''}`);
}
x = await load(`${dir}/ro_range.wasm`);
ok(x.ro_span() > 0, `the read-only range is not empty with this toolchain (__rodata_end - __rodata_start is ${x.ro_span()}; wasm-ld before LLVM 22 leaves it empty and every string slow)`);
ok(x.ro_id(0) === x.ro_ptr(0) && x.ro_id(1) === x.ro_ptr(1) && x.ro_ptr(0) !== x.ro_ptr(1), 'cd_tmpz of a literal the macro cannot see is the literal\'s own address');
ok(x.mut_id() < 0 || x.mut_id() >= 0x80000000, 'cd_tmpz of mutable bytes is a runtime string (the top bit set), not an address');
x = await load(`${dir}/on_only.wasm`);
x.cd_start(0);
// cd_layout's block (src/cdom.c, CD_LAYOUT): its count, then the runtime strings' 7 entries, the record, and
// the offsets of its type and id.
const layout = new Uint32Array(x.memory.buffer, x.cd_layout(), 11), rec = new Int32Array(x.memory.buffer);
const typeAt = (layout[8] + layout[9]) >> 2, idAt = (layout[8] + layout[10]) >> 2;
const event = (type, id) => { rec[typeAt] = type; rec[idAt] = id; x.cd_event(); };
event(0, x.node()); // CD_EV_CLICK: the handler
event(0, x.node() + 1); // no handler: cdom's empty cd_on_event
event(2, x.node()); // another type: the same
ok(x.heard() === 1, `an app that uses cd_on alone links with no cd_on_event, and only its handler's event reaches it (heard ${x.heard()})`);
process.exit(bad ? 1 : 0);
