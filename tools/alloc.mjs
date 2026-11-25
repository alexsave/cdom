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
// Each case twice: at cdom's cap (-DCDOM_HEAP_PAGES_MAX=40), and on a memory of 40 pages at most, where
// memory.grow itself refuses (its -1 must read as a refusal, not as a size).
for (const [mod, at] of [['alloc_try.wasm', 'at the cap'], ['alloc_try_max.wasm', 'when memory.grow refuses']]) {
    {
        const x = await fresh(mod);
        const n = call(() => x.fill()), p = x.pages();
        check(`cd_alloc_try ${at}: blocks until the heap has no room, then 0 (never a grow past the cap), and a freed block comes back`, n > 20 && p <= 40, `${n} blocks, ${p} pages`);
    }
    {
        const x = await fresh(mod);
        const g = call(() => x.grow()), h = call(() => x.huge());
        check(`cd_realloc_try ${at}: a grow past the cap answers 0 and leaves the block as it was, its bytes kept; a grow that fits moves them`, g === 1, 'grow answered ' + g);
        check(`cd_alloc_try ${at}: a size past what a size can be answers 0`, h === 1, 'answered ' + h);
    }
    for (const [f, what] of [['malloc_full', 'malloc'], ['alloc_full', 'cd_alloc'], ['realloc_full', 'realloc']]) {
        const x = await fresh(mod);
        const r = traps(() => x[f]());
        check(`${what} still traps ${at} (it never answers 0 for code that would write there)`, r === 'trap', r);
    }
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
// cd_file_got takes cd_file_room's block (src/cdom_file.c) the same way.
for (const [name, answer, room] of [['cd_net_msg', (x, p, n) => x.cd_net_msg(3, p, n)], ['cd_encoded', (x, p, n) => x.cd_encoded(1, p, n)],
  ['cd_store_got', (x, p, n) => x.cd_store_got(1, p, n)], ['cd_url_changed', (x, p, n) => x.cd_url_changed(p, n)],
  ['cd_file_got', (x, p, n) => x.cd_file_got(1, 0, p, n), 'cd_file_room']]) {
    const x = await fresh('alloc_mem.wasm');
    const p0 = x.pages(), n = 20000000;
    const r = call(() => { for (let i = 0; i < 2; i++) answer(x, x[room || 'cd_bytes_buf'](n), n); return x.pages() - p0; });
    check(`${name} frees the bytes it was handed once its hook returned: two 20 MB deliveries grow the memory 305 pages`, r === 305, `${r} pages`);
}
// Raceclub's photo path (tests/alloc_mem.c, P): a photo read with cd_file_read as the shim answers it, a
// decode buffer and an output taken and freed in the callback, twice. The memory never shrinks, so the room
// is given back when the second photo grows it by nothing. Predictions: docs/file-read-design.md.
const photo = (file, w, h, wasm = 'alloc_mem.wasm') => {
    let token = -1;
    const mod = new WebAssembly.Module(fs.readFileSync(path.join(out, wasm)));
    const imports = {};
    for (const { module } of WebAssembly.Module.imports(mod)) imports[module] = new Proxy({}, { get: (_, k) => k === 'file_read' ? (t) => { token = t; } : () => 0 });
    const x = new WebAssembly.Instance(mod, imports).exports;
    const p0 = x.pages(), grown = [];
    for (let i = 0; i < 2; i++) {
        const before = x.pages();
        x.photo_ask(w, h);
        const ptr = x.cd_file_room(file);
        new Uint8Array(x.memory.buffer, ptr, file).fill(7);
        x.cd_file_got(token, 0, ptr, file);
        grown.push(x.pages() - before);
    }
    return { first: grown[0], second: grown[1], peak: x.memory.buffer.byteLength, pages: x.pages(), start: p0, bad: x.photo_bad() };
};
for (const [name, file, w, h, first] of [['12 MP', 5000000, 4032, 3024, 857], ['24 MP', 10000000, 5664, 4248, 1658]]) {
    const r = call(() => photo(file, w, h));
    check(`a ${name} photo read with cd_file_read, decoded and scaled in its callback, grows the memory ${first} pages (each block its size), and a second photo reuses all of it: 0 pages`,
      r.bad === 0 && r.first === first && r.second === 0, JSON.stringify(r));
    if (process.env.ALLOC_PHOTO) console.log('     ' + name + ': ' + JSON.stringify(r));
}
// Blocks of every size taken, grown and freed in a seeded order: none ever holds another's bytes.
{
    const x = await fresh('alloc_mem.wasm');
    x.cd_start(20261118);
    const r = call(() => x.churn(20000));
    check('20,000 seeded steps of small and large blocks taken, grown, shrunk and freed: every block keeps its own bytes', r === 0, typeof r === 'number' ? `step ${r} found another block's bytes` : r);
}
// The image module's way out of a libjpeg error leaves the heap as it was: tests/image/wasm.c (built by
// tests/image/wasm.mk, each CDOM_IMAGE_SJLJ way) given a JPEG with a bad DQT (a fatal error deep in libjpeg) 100 times
// in a row answers CORRUPT each time and grows the memory by 0 pages after the first (jpeg_destroy_decompress and
// every cd_alloc_try block freed on the throw's path), and a good JPEG still decodes after. The input is the image
// oracle's (tests/image/run.sh makes tests/out/image/inputs); this case runs once it is there.
{
    const img = path.join(out, 'image'), bad = path.join(img, 'inputs/dqt-bad.jpg'), good = path.join(img, 'inputs/s420.jpg');
    if (!fs.existsSync(bad)) check('the image module\'s error path (needs tests/image/run.sh first)', false, `no ${bad}`);
    else for (const sjlj of ['js', 'eh']) {
        const { execFileSync } = await import('node:child_process');
        execFileSync('make', ['-s', '-f', path.join(out, '../image/wasm.mk'), `OUT=${img}`, `SJLJ=${sjlj}`, path.join(img, `wasm_${sjlj}.wasm`)], { stdio: 'inherit' });
        const mod = new WebAssembly.Module(fs.readFileSync(path.join(img, `wasm_${sjlj}.wasm`)));
        const THROW = {};
        let x;
        // the shim's image_call and image_throw (shim/cdom.js), the rest answering 0
        const own = { image_call: (body, ctx) => { try { x.cd_image_run(body, ctx); return 0; } catch (e) { if (e !== THROW) throw e; return 1; } },
                                    image_throw: () => { throw THROW; } };
        x = new WebAssembly.Instance(mod, { cdom: new Proxy({}, { get: (_, k) => own[k] || (() => 0) }) }).exports;
        x.cd_start(1);
        const run = (bytes) => {
            const p = x.room(bytes.length);
            new Uint8Array(x.memory.buffer, p, bytes.length).set(bytes);
            const st = x.resize(p, bytes.length, 300, 300);
            x.drop(new Uint32Array(x.memory.buffer, x.out(), 3)[2]);
            x.drop(p);
            return st;
        };
        const b = fs.readFileSync(bad), statuses = new Set();
        statuses.add(call(() => run(b)));
        const after1 = x.memory.buffer.byteLength >> 16;
        for (let i = 1; i < 100; i++) statuses.add(call(() => run(b)));
        const grew = (x.memory.buffer.byteLength >> 16) - after1, g = call(() => run(fs.readFileSync(good)));
        check(`the image module (${sjlj}): 100 fatal libjpeg errors in a row answer CORRUPT each time and grow the memory 0 pages after the first, and a good JPEG decodes after`,
          statuses.size === 1 && statuses.has(4) && grew === 0 && g === 0, `statuses ${[...statuses]}, +${grew} pages, then ${g}`);
    }
}
for (const p of passed) console.log('ok  ', p);
for (const e of errors) console.log('FAIL', e);
console.log(`${passed.length} passed, ${errors.length} failed`);
process.exit(errors.length ? 1 : 0);
