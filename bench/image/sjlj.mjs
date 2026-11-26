// The setjmp spike (docs/image-design.md, "The spike"): the time of cd_image_resize on one input, the three ways out
// of a libjpeg error side by side, in Node's V8 (the engine of Chrome): js (bench/image/build-js, the shim's try, its
// two functions copied here), eh (build-eh, wasm exception handling) and ems (bench/image/sjlj-ems, emscripten's
// lowering: an invoke_vi through the exported table). Each round runs every way once, in an order that rotates, each
// on a fresh instance; every run waits for the 1-minute load to be under LOADMAX (default 4). It also times a fatal
// error (inputs/dqt-bad.jpg when it is there) and checks every way answers CORRUPT for it.
// Usage: node bench/image/sjlj.mjs [rounds=21] [input=inputs/p12.jpg] [bad=../../tests/out/image/inputs/dqt-bad.jpg]
// ADLER=1 compares the PNG adler32 check instead (A1): js against bench/image/adler's build, on inputs/p12.png.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const here = path.dirname(new URL(import.meta.url).pathname);
const rounds = +(process.argv[2] || 21);
const input = path.resolve(here, process.argv[3] || 'inputs/p12.jpg');
const badFile = path.resolve(here, process.argv[4] || '../../tests/out/image/inputs/dqt-bad.jpg');
const LOADMAX = +(process.env.LOADMAX || 4);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const ways = process.env.ADLER ? {   // A1: the adler32 checked (js) against the stream's end not read (bench/image/adler)
    js: path.join(here, 'build-js/app.wasm'),
    nocheck: path.join(here, 'adler/out/app.wasm'),
} : {
    js: path.join(here, 'build-js/app.wasm'),
    eh: path.join(here, 'build-eh/app.wasm'),
    ems: path.join(here, 'sjlj-ems/out/app.wasm'),
};
const modules = Object.fromEntries(Object.entries(ways).map(([k, f]) => [k, new WebAssembly.Module(fs.readFileSync(f))]));
const file = fs.readFileSync(input), bad = fs.existsSync(badFile) ? fs.readFileSync(badFile) : null;

function instance(way) {
    let x;
    const THROW = {};
    const stub = new Proxy({}, { get: (_, k) => ({
        image_call: (body, ctx) => { try { x.cd_image_run(body, ctx); return 0; } catch (e) { if (e !== THROW) throw e; return 1; } },
        image_throw: () => { throw THROW; },
    })[k] || (() => 0) });
    const env = {
        invoke_vi: (i, a) => { try { x.__indirect_function_table.get(i)(a); } catch (e) { if (e !== THROW) throw e; } },
        ems_throw: () => { throw THROW; },
    };
    x = new WebAssembly.Instance(modules[way], { cdom: stub, env }).exports;
    x.cd_start(1);
    return x;
}
function resize(x, bytes) {
    const p = x.room(bytes.length);
    new Uint8Array(x.memory.buffer, p, bytes.length).set(bytes);
    const t0 = performance.now();
    const st = x.resize(p, bytes.length, 900, 900);
    const ms = performance.now() - t0;
    const o = new Uint32Array(x.memory.buffer, x.out(), 3);
    x.drop(o[2]); x.drop(p);
    return { ms, st };
}
const med = (v) => { const s = [...v].sort((a, b) => a - b); return s[s.length >> 1]; };
const times = {}, errs = {};
for (const w of Object.keys(ways)) { times[w] = []; errs[w] = []; }
const names = Object.keys(ways);
for (let r = 0; r < rounds; r++) {
    for (let k = 0; k < names.length; k++) {
        const way = names[(k + r) % names.length];
        for (let w = 0; w < 1200 && os.loadavg()[0] >= LOADMAX; w += 10) await sleep(10000);
        const x = instance(way);
        const { ms, st } = resize(x, file);
        if (st !== 0) { console.error(`${way}: status ${st}`); process.exit(1); }
        times[way].push(ms);
        if (bad) {
            const e = resize(x, bad);
            if (e.st !== 4) { console.error(`${way}: a bad DQT answered ${e.st}, not CORRUPT`); process.exit(1); }
            errs[way].push(e.ms);
        }
        console.log(`round ${r} ${way}: ${ms.toFixed(1)} ms${bad ? `, error ${errs[way].at(-1).toFixed(2)} ms` : ''} (load ${os.loadavg()[0].toFixed(2)})`);
    }
}
console.log(`\nway\tmedian ms\tmin\tmax\terror median ms\t(${rounds} rounds, ${path.basename(input)}, Node ${process.version})`);
for (const w of names) console.log(`${w}\t${med(times[w]).toFixed(1)}\t${Math.min(...times[w]).toFixed(1)}\t${Math.max(...times[w]).toFixed(1)}\t${bad ? med(errs[w]).toFixed(2) : ''}`);
