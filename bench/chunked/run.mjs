// The chunked-read sweep (docs/chunked-file-read-design.md, section 7's predictions; the Mac takes the real numbers):
// a real file picked from disk, decoded to fit 900 x 900 on a fresh page per run, by four arms (bench/chunked/app.c):
//   whole   today's whole read (cd_file_read_cb) and cd_image_resize
//   file    cd_image_file_cb on the main thread
//   pieces  cd_job_pieces_cb with the image job (tests/image/job.c)
//   job     cd_job_file_cb, today's job (the file read whole into the job's memory), for T5
// on p12.jpg, p48.jpg, p12p.jpg, p12.png (bench/image/inputs.py) and p64.jpg (bench/chunked/p64.py), with the page set
// built at CD_IMAGE_PIECE 16, 64, 256 KiB and 1 MiB (bench/chunked/bench.mk, one set per size), in interleaved rounds
// (each round every size, input and arm once, the order turned each round).
// Per run: the wasm memory after the decode (the page's memory.buffer.byteLength, its peak), the job's memory at its end
// (the job arms), the renderer's memory (Linux: its peak resident set, VmHWM, and its private dirty memory after;
// macOS: tools/footprint.c's footprint after), the time from the pick (the input's change event) to the pixels (the
// app's callback, cd_now), and the longest main-thread task (CDP tracing, devtools.timeline, as tools/shrink.mjs).
// Chrome through tools/cdp.mjs; BROWSER=safari through tools/safari.mjs, which has no CDP: the file is fetched and put
// in the input as a File through a DataTransfer (in memory, not from disk), and there is no trace and no renderer
// memory (those columns are empty), only the page's own numbers (time, wasm memory, the job's memory).
// Usage: node bench/chunked/run.mjs [rounds=3]      BROWSER=chrome|safari, SIZES=16384,..., INPUTS=p12.jpg,..., ARMS=whole,...
// Writes bench/chunked/results/sweep-<browser>.tsv (OUT= another path). Timings here are indicative only.
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { sleep } from '../../tools/cdp.mjs';

const here = path.dirname(new URL(import.meta.url).pathname), top = path.resolve(here, '../..');
const browser = process.env.BROWSER || 'chrome';
const rounds = +(process.argv[2] || 3);
const SIZES = (process.env.SIZES || '16384,65536,262144,1048576').split(',').map(Number);
const INPUTS = (process.env.INPUTS || 'p12.jpg,p48.jpg,p12p.jpg,p12.png,p64.jpg').split(',');
const ARMS = (process.env.ARMS || 'whole,file,pieces,job').split(',');
const ARM = { whole: 0, file: 1, pieces: 2, job: 3 };
const inputs = path.join(top, 'bench/image/inputs'), build = path.join(here, 'build');
const OUT = process.env.OUT || path.join(here, 'results', `sweep-${browser}.tsv`);
for (const i of INPUTS) if (!fs.existsSync(path.join(inputs, i))) {
    console.error(`no bench/image/inputs/${i}: python3 -I bench/image/inputs.py bench/image/inputs && python3 -I bench/chunked/p64.py bench/image bench/image/inputs`);
    process.exit(1);
}
for (const s of SIZES) execFileSync('make', ['-s', '-f', path.join(here, 'bench.mk'), `OUT=${path.join(build, 'p' + s)}`, `PIECE=${s}`, 'all'], { stdio: 'inherit' });

const types = { '.html': 'text/html' };
const srv = http.createServer((q, r) => {
    const u = decodeURIComponent(q.url.split(/[?#]/)[0]);
    const f = u.startsWith('/inputs/') ? path.join(inputs, u.slice(8)) : path.join(build, u);
    if (!f.startsWith(inputs) && !f.startsWith(build)) { r.writeHead(403); r.end(); return; }
    fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' }); r.end(b); });
}).listen(0);
const base = () => `http://127.0.0.1:${srv.address().port}`;

// The renderer's memory (Chrome): Linux from /proc, macOS from tools/footprint.c (as tools/shrink.mjs).
const MAC = os.platform() === 'darwin', MIB = 1 << 20;
const FOOT = path.join(top, 'tools/out/footprint');
if (MAC && browser === 'chrome' && !fs.existsSync(FOOT)) { fs.mkdirSync(path.dirname(FOOT), { recursive: true }); execFileSync('cc', ['-O2', '-o', FOOT, path.join(top, 'tools/footprint.c')]); }
function meter(pid) {
    try {
        if (MAC) { const [fp] = execFileSync(FOOT, [String(pid)]).toString().trim().split(' ').map(Number); return { peak: '', now: fp / MIB }; }
        const st = fs.readFileSync(`/proc/${pid}/status`, 'utf8'), roll = fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8');
        const kb = (t, k) => +new RegExp(`^${k}:\\s+(\\d+) kB`, 'm').exec(t)[1];
        return { peak: kb(st, 'VmHWM') / 1024, now: (kb(roll, 'Private_Dirty') + kb(roll, 'Swap')) / 1024 };
    } catch { return { peak: '', now: '' }; }
}
async function rendererPid(b) {
    const info = async () => (await b.sendBrowser('SystemInfo.getProcessInfo')).result.processInfo.filter((p) => p.type === 'renderer');
    const a = await info();
    await b.evaluate('(() => { const t = performance.now(); while (performance.now() - t < 150); })()');
    const c = await info();
    return c.map((p) => ({ id: p.id, d: p.cpuTime - (a.find((q) => q.id === p.id)?.cpuTime ?? 0) })).sort((x, y) => y.d - x.d)[0].id;
}
async function traceStart(b) {
    const ev = [];
    b.onEvent = (d) => { if (d.method === 'Tracing.dataCollected') ev.push(...d.params.value); };
    await b.sendBrowser('Tracing.start', { traceConfig: { recordMode: 'recordContinuously', includedCategories: ['devtools.timeline', 'disabled-by-default-devtools.timeline', '__metadata'] } });
    return ev;
}
async function traceEnd(b, ev, pid) {
    const done = new Promise((r) => { const prev = b.onEvent; b.onEvent = (d) => { prev(d); if (d.method === 'Tracing.tracingComplete') r(); }; });
    await b.sendBrowser('Tracing.end');
    await done;
    const main = ev.find((e) => e.ph === 'M' && e.name === 'thread_name' && e.pid === pid && e.args.name === 'CrRendererMain');
    const tasks = ev.filter((e) => e.ph === 'X' && e.name === 'RunTask' && e.pid === pid && main && e.tid === main.tid);
    return { longest: Math.max(0, ...tasks.map((e) => e.dur / 1000)), tasks: tasks.length };
}

const { launch } = browser === 'safari' ? await import('../../tools/safari.mjs') : await import('../../tools/cdp.mjs');
const COLS = ['browser', 'round', 'piece', 'input', 'arm', 'status', 'w', 'h', 'total_ms', 'wasm_mib', 'job_mib', 'renderer_peak_mib', 'renderer_after_mib', 'longest_task_ms', 'tasks'];
fs.mkdirSync(path.dirname(OUT), { recursive: true });
fs.writeFileSync(OUT, COLS.join('\t') + '\n');
const runs = [];
for (let r = 1; r <= rounds; r++)
    for (const s of SIZES) for (const i of INPUTS) for (const a of ARMS) runs.push([r, s, i, a]);
// interleaved: each round in its own order, turned by the round (no random source)
const ordered = [];
for (let r = 1; r <= rounds; r++) {
    const these = runs.filter((x) => x[0] === r);
    const k = ((r - 1) * 7) % these.length;
    ordered.push(...these.slice(k), ...these.slice(0, k));
}
let version = '';
for (const [round, piece, input, arm] of ordered) {
    const b = await launch();
    version = b.version || version;
    try {
        const go = async () => {
            if (b.navigate) await b.navigate(`${base()}/p${piece}/bench.html`);
            else await b.send('Page.navigate', { url: `${base()}/p${piece}/bench.html` });
            for (let t = 0; t < 400 && !(await b.evaluate('!!window.READY')); t++) await sleep(25);
            if ((await b.evaluate('X.piece()')) !== piece) throw new Error('the page set is not of its piece size');
            await b.evaluate(`window.ARM = ${ARM[arm]}`);
            const chrome = browser === 'chrome';
            const pid = chrome ? await rendererPid(b) : 0;
            const ev = chrome ? await traceStart(b) : null;
            if (chrome) {
                const { result: { result: { objectId } } } = await b.send('Runtime.evaluate', { expression: 'document.getElementById("pick")' });
                await b.send('DOM.setFileInputFiles', { files: [path.join(inputs, input)], objectId });
            } else {
                await b.evaluate(`(async () => { const d = await (await fetch('/inputs/${input}')).blob(); const t = new DataTransfer();
          t.items.add(new File([d], '${input}')); const p = document.getElementById('pick'); p.files = t.files; p.dispatchEvent(new Event('change')); })()`);
            }
            for (let t = 0; t < 12000 && (await b.evaluate('X.rec(4)')) === 0; t++) await sleep(10);
            await sleep(100);
            const tr = chrome ? await traceEnd(b, ev, pid) : { longest: '', tasks: '' };
            const v = await b.evaluate('[0, 1, 2, 3, 4].map((f) => X.rec(f)).concat([T0, X.memory.buffer.byteLength])');
            const m = chrome ? meter(pid) : { peak: '', now: '' };
            const row = [browser, round, piece, input, arm, v[0], v[1], v[2], (v[4] - v[5]).toFixed(1), (v[6] / MIB).toFixed(2), arm === 'pieces' || arm === 'job' ? (v[3] / 16).toFixed(2) : '',
              m.peak === '' ? '' : m.peak.toFixed(1), m.now === '' ? '' : m.now.toFixed(1), tr.longest === '' ? '' : tr.longest.toFixed(1), tr.tasks];
            fs.appendFileSync(OUT, row.join('\t') + '\n');
            console.log(row.join('\t'));
        };
        await go();
    } catch (e) {
        console.log(`FAIL ${round} ${piece} ${input} ${arm}: ${e.stack || e}`);
        fs.appendFileSync(OUT, [browser, round, piece, input, arm, 'error'].join('\t') + '\n');
    } finally {
        await b.close();
    }
}
srv.close();
console.log(`${version}: ${ordered.length} runs into ${path.relative(top, OUT)}`);
