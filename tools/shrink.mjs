// The measurements of docs/job-worker.md: M0 (the meter), M1 and M3 (today's cost, and cd_job), on tests/out/shrink.html.
// The meter is the renderer process's /proc/<pid>/smaps_rollup, Private_Dirty plus Swap (VmRSS beside it as the cross-check;
// on macOS, phys_footprint and the resident size, tools/footprint.c),
// read every 20 ms. The renderer's pid comes from CDP SystemInfo.getProcessInfo on the browser target; with more than one
// renderer, the one whose CPU time grows during a 200 ms spin of the page. Each run is a fresh Chrome.
//   node tools/shrink.mjs m0 [runs]            the meter check: touch 100 MB, then grow 100 MB without writing
//   node tools/shrink.mjs m1m3 [runs]          arms nothing (touch 42 MB in the app, free it) and worker (cd_job), interleaved
// Writes tab-separated rows (experiment, run, arm, metric, value, unit) to the file in OUT (default
// docs/job-worker-results.tsv, appended), after '#' lines naming the machine. LOADMAX (default 4): a run waits until the 1-minute
// load is under it. MB (42), PASSES (8), IDLE_S (60: the nothing arm's idle before its collection).
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync, spawn } from 'node:child_process';
import { launch, sleep } from './cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const root = path.join(top, 'tests/out');
const OUT = process.env.OUT || path.join(top, 'docs/job-worker-results.tsv');
const EXP = process.env.EXP || 'M13';
const MB = +(process.env.MB || 42), PASSES = +(process.env.PASSES || 8), IDLE_S = +(process.env.IDLE_S || 60), LOADMAX = +(process.env.LOADMAX || 4);
const mode = process.argv[2], runs = +(process.argv[3] || 5);
const types = { '.html': 'text/html', '.wasm': 'application/wasm' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
const MIB = 1024 * 1024;
const rows = [];
const emit = (exp, run, arm, metric, value, unit) => { rows.push([exp, run, arm, metric, typeof value === 'number' ? +value.toFixed(3) : value, unit].join('\t')); };

// The meter: Private_Dirty plus Swap of /proc/<pid>/smaps_rollup, and VmRSS, in MiB. On macOS: phys_footprint and the
// resident size (tools/footprint.c), in the same two fields.
const MAC = os.platform() === 'darwin';
const FOOT = path.join(top, 'tools/out/footprint');
if (MAC && !fs.existsSync(FOOT)) { fs.mkdirSync(path.dirname(FOOT), { recursive: true }); execFileSync('cc', ['-O2', '-o', FOOT, path.join(top, 'tools/footprint.c')]); }
function meter(pid) {
  if (MAC) { const [fp, rss] = execFileSync(FOOT, [String(pid)]).toString().trim().split(' ').map(Number); return { priv: fp / MIB, rss: rss / MIB }; }
  const roll = fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8'), st = fs.readFileSync(`/proc/${pid}/status`, 'utf8');
  const kb = (t, k) => +new RegExp(`^${k}:\\s+(\\d+) kB`, 'm').exec(t)[1];
  return { priv: (kb(roll, 'Private_Dirty') + kb(roll, 'Swap')) / 1024, rss: kb(st, 'VmRSS') / 1024 };
}
const load1 = () => os.loadavg()[0];
async function gate() { while (load1() >= LOADMAX) await sleep(2000); }
async function rendererPid(b) {
  const info = async () => (await b.sendBrowser('SystemInfo.getProcessInfo')).result.processInfo.filter((p) => p.type === 'renderer');
  const a = await info();
  await b.evaluate('(() => { const t = performance.now(); while (performance.now() - t < 200); })()');
  const c = await info();
  const grow = c.map((p) => ({ id: p.id, d: p.cpuTime - (a.find((q) => q.id === p.id)?.cpuTime ?? 0) })).sort((x, y) => y.d - x.d);
  return { pid: grow[0].id, renderers: c.length };
}
// A sampler of the meter every 20 ms; its samples are [ms, priv, rss].
function sampler(pid) {
  if (MAC) {   // one footprint process reading every 20 ms, not a process started per sample
    const s = [], p = spawn(FOOT, [String(pid), '20'], { stdio: ['ignore', 'pipe', 'ignore'] });
    let rest = '';
    p.stdout.on('data', (d) => { const l = (rest + d).split('\n'); rest = l.pop(); for (const x of l) { const [us, fp, rss] = x.split(' ').map(Number); s.push([us / 1000, fp / MIB, rss / MIB]); } });
    return { stop: () => { p.kill(); return s; }, samples: s };
  }
  const s = [], t0 = performance.now();
  const id = setInterval(() => { try { const m = meter(pid); s.push([performance.now() - t0, m.priv, m.rss]); } catch {} }, 20);
  return { stop: () => { clearInterval(id); return s; }, samples: s };
}
const settled = async (pid, ms = 1000) => { await sleep(ms); const v = []; for (let i = 0; i < 5; i++) { v.push(meter(pid)); await sleep(50); } v.sort((a, b) => a.priv - b.priv); return v[2]; };
const page = async () => {
  const b = await launch();
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/shrink.html` });
  for (let i = 0; i < 200 && !(await b.evaluate('!!window.READY')); i++) await sleep(50);
  if (!(await b.evaluate('!!window.READY'))) throw new Error('the page did not start');
  const { pid, renderers } = await rendererPid(b);
  return { b, pid, renderers };
};
const waitAnswered = async (b, n) => { for (let i = 0; i < 6000 && (await b.evaluate('X.answered()')) < n; i++) await sleep(5); return b.evaluate('X.answered()'); };

async function m0(run) {
  const { b, pid } = await page();
  try {
    const m_0 = await settled(pid, 1500);
    await b.evaluate('X.touch(100)');
    const m_1 = await settled(pid);
    await b.evaluate('X.grow_only(100)');
    const m_2 = await settled(pid);
    emit('M0', run, 'meter', 'touch_100MB_rise', m_1.priv - m_0.priv, 'MiB');
    emit('M0', run, 'meter', 'grow_only_100MB_rise', m_2.priv - m_1.priv, 'MiB');
    emit('M0', run, 'rss', 'touch_100MB_rise', m_1.rss - m_0.rss, 'MiB');
    emit('M0', run, 'rss', 'grow_only_100MB_rise', m_2.rss - m_1.rss, 'MiB');
  } finally { await b.close(); }
}

// Tracing of the browser, kept for the longest main-thread task (RunTask on the renderer's CrRendererMain) between two marks.
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
  return { longest: Math.max(0, ...tasks.map((e) => e.dur / 1000)), tasks: tasks.length, found: !!main };
}

async function arm(run, which) {
  await gate();
  const load = load1();
  const { b, pid, renderers } = await page();
  try {
    const base = await settled(pid, 1500);
    const ev = await traceStart(b);
    const s = sampler(pid);
    await sleep(100);
    const t0 = performance.now();
    await b.evaluate(which === 'nothing' ? `X.local(${MB}, ${PASSES})` : `X.job(${MB}, ${PASSES})`);
    const n = await waitAnswered(b, 1);
    const wall_outer = performance.now() - t0;
    await sleep(200);
    const tr = await traceEnd(b, ev, pid);
    const status = await b.evaluate('X.status()'), len = await b.evaluate('X.len()');
    const wall = await b.evaluate('X.wall()');
    const peak = Math.max(...s.samples.map((x) => x[1])) - base.priv, peak_rss = Math.max(...s.samples.map((x) => x[2])) - base.rss;
    const held = async (name, ms) => { const m = await settled(pid, ms); emit(EXP, run, which, name, m.priv - base.priv, 'MiB'); emit(EXP, run, which, name + '_rss', m.rss - base.rss, 'MiB'); };
    // 1 s after the answer (the answer came at about t0 + wall; 200 ms and the trace ended since)
    await held('held_1s', 600);
    await sleep(3400);
    await held('held_5s', 0);
    if (which === 'nothing') { await sleep((IDLE_S - 6) * 1000); await held(`held_${IDLE_S}s`, 0); }
    await b.send('HeapProfiler.enable');
    await b.send('HeapProfiler.collectGarbage');
    await held('held_after_gc_2s', which === 'nothing' ? 10000 : 2000);
    s.stop();
    emit(EXP, run, which, 'peak_rise', peak, 'MiB');
    emit(EXP, run, which, 'peak_rise_rss', peak_rss, 'MiB');
    emit(EXP, run, which, 'longest_main_task', tr.longest, 'ms');
    emit(EXP, run, which, 'main_tasks_traced', tr.tasks, 'count');
    emit(EXP, run, which, 'wall_cold', wall, 'ms');
    emit(EXP, run, which, 'wall_outer_cold', wall_outer, 'ms');
    emit(EXP, run, which, 'answer_status', status, 'code');
    emit(EXP, run, which, 'answer_len', len, 'bytes');
    emit(EXP, run, which, 'load1_before', load, 'load');
    emit(EXP, run, which, 'renderers', renderers, 'count');
    emit(EXP, run, which, 'trace_found_main_thread', tr.found ? 1 : 0, 'bool');
    // a second, warm call in the same page: the module compiled, the blob URL made
    await b.evaluate(which === 'nothing' ? `X.local(${MB}, ${PASSES})` : `X.job(${MB}, ${PASSES})`);
    await waitAnswered(b, 2);
    emit(EXP, run, which, 'wall_warm', await b.evaluate('X.wall()'), 'ms');
  } finally { await b.close(); }
}

const cpu = MAC ? os.cpus()[0].model : (fs.readFileSync('/proc/cpuinfo', 'utf8').match(/model name\s*:\s*(.*)/) || [])[1];
const clang = execFileSync(process.env.WASM_CC || 'clang', ['--version']).toString().split('\n')[0];
const head = (title) => `# ${title}\n# ${new Date().toISOString().slice(0, 10)} ${cpu}, ${os.cpus().length} cores, ${os.platform()} ${os.release()}, Chrome ${execFileSync(process.env.CHROME || (MAC ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'google-chrome'), ['--version']).toString().trim()}, ${clang}\n# MB=${MB} PASSES=${PASSES} IDLE_S=${IDLE_S} LOADMAX=${LOADMAX}\n# experiment\trun\tarm\tmetric\tvalue\tunit\n`;
try {
  if (mode === 'm0') for (let r = 1; r <= runs; r++) { await gate(); await m0(r); console.log('m0 run', r); }
  else if (mode === 'm1m3') for (let r = 1; r <= runs; r++) for (const w of r % 2 ? ['nothing', 'worker'] : ['worker', 'nothing']) { await arm(r, w); console.log('run', r, w); }
  else throw new Error('usage: shrink.mjs m0|m1m3 [runs]');
  fs.appendFileSync(OUT, head(mode) + rows.join('\n') + '\n');
  console.log('wrote', OUT);
} finally { srv.close(); }
