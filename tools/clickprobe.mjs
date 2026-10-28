// One harness-shaped measured click per fresh page, probed (proof of why direct calls win).
// Reproduces the harness's steps for 01_run1k, 07_create10k, 09_clear1k_x8: navigate, the same
// warm-up clicks and checks, the harness's forced GC (gc({type:'major',execution:'sync',
// flavor:'last-resort'}); NOGC=1 skips it, as tools/onetrace.mjs did), CPU throttling 4x for
// clear, then one measured mouse click (Input.dispatchMouseEvent at the button's centre, as
// puppeteer's click) waited until the harness's check holds and two frames have passed.
// Differences from the harness: one tab navigated per iteration instead of a new tab, and the
// check is polled with Runtime.evaluate instead of puppeteer's selectors.
//   MODE=trace: traces devtools.timeline, disabled-by-default-devtools.timeline, v8,
//     disabled-by-default-v8.gc and v8.execute over the click; counts, inside the window from the
//     click's EventDispatch to the end of the last Paint/Commit on the renderer main thread,
//     MinorGC, MajorGC, incremental marking steps (V8.GC_MC_INCREMENTAL*), scavenges and
//     V8.InterruptHandler / StackGuard events, optimizing-compiler work and deopts on the main
//     thread (disabled-by-default-v8.compile) with their durations, and whether they fall inside
//     the click's script.
//   MODE=alloc: HeapProfiler.startSampling (samplingInterval SI bytes, default 16, objects
//     collected by minor and major GC included) around the click; bytes sampled per allocating
//     function, per click.
// Usage: MODE=trace|alloc node tools/clickprobe.mjs <entry> <run1k|create10k|clear> [N=20]
// Prints a summary; JSONL per iteration to OUT if set.
import fs from 'node:fs';
import { launch, sleep } from './cdp.mjs';
const [entry, bench] = process.argv.slice(2, 4), N = +(process.argv[4] || 20), MODE = process.env.MODE || 'trace';
// JSFLAGS: more V8 flags, merged into the one --js-flags switch (Chrome keeps only the last).
process.env.CHROME_ARGS = [`--js-flags=--expose-gc${process.env.JSFLAGS ? ' ' + process.env.JSFLAGS : ''}`, ...(process.env.CHROME_ARGS ? process.env.CHROME_ARGS.split(' ') : [])].join('\n');
const url = `http://localhost:${process.env.PORT || 8703}/frameworks/keyed/${entry}/index.html`;
const td1 = (k) => `document.querySelector("tbody>tr:nth-of-type(${k})>td:nth-of-type(1)")`;
const rows = [];
const b = await launch();
try {
  const E = b.evaluate;
  const until = async (expr) => { for (let i = 0; i < 400; i++) { if (await E(expr)) return; await sleep(i < 3 ? 10 : 25); } throw new Error('check failed: ' + expr); };
  const click = async (id) => {
    const r = await E(`(() => { const r = document.getElementById(${JSON.stringify(id)}).getBoundingClientRect(); return [r.x + r.width / 2, r.y + r.height / 2]; })()`);
    for (const type of ['mousePressed', 'mouseReleased']) await b.send('Input.dispatchMouseEvent', { type, x: r[0], y: r[1], button: 'left', clickCount: 1 });
  };
  const frames2 = 'new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(() => r(true), 0))))';
  for (let it = 0; it < N; it++) {
    await b.send('Page.navigate', { url: 'about:blank' }); await sleep(50);
    await b.send('Page.navigate', { url });
    await until('!!document.getElementById("run") && document.readyState === "complete"');
    await b.send('Page.bringToFront');
    for (let i = 0; i < 5; i++) { // run1k, create10k and clear warm up alike: 5x (run, clear)
      await click('run'); await until(`${td1(1)}?.innerText.includes("${i * 1000 + 1}")`);
      await click('clear'); await until(`!${td1(1000)}`);
    }
    if (bench === 'clear') { await click('run'); await until(`${td1(1)}?.innerText.includes("5001")`); }
    const [id, done] = bench === 'run1k' ? ['run', `${td1(1000)}?.innerText.includes("6000")`]
      : bench === 'create10k' ? ['runlots', `!!document.querySelector("tbody>tr:nth-of-type(10000)>td:nth-of-type(2)>a")`]
      : ['clear', `!${td1(1000)}`];
    await E(frames2);
    if (bench === 'clear') await b.send('Emulation.setCPUThrottlingRate', { rate: 4 });
    const ev = [];
    if (MODE === 'trace') {
      b.onEvent = (m) => { if (m.method === 'Tracing.dataCollected') ev.push(...m.params.value); };
      await b.send('Tracing.start', { traceConfig: { includedCategories: ['devtools.timeline', 'disabled-by-default-devtools.timeline', 'v8', 'disabled-by-default-v8.gc', 'disabled-by-default-v8.compile', 'v8.execute', 'blink.user_timing'] }, transferMode: 'ReportEvents' });
      await sleep(50);
    } else {
      await b.send('HeapProfiler.enable');
      await b.send('HeapProfiler.startSampling', { samplingInterval: +(process.env.SI || 16), includeObjectsCollectedByMajorGC: true, includeObjectsCollectedByMinorGC: true });
    }
    if (!process.env.NOGC) await E("gc({type:'major',execution:'sync',flavor:'last-resort'})");
    await sleep(MODE === 'trace' ? 0 : 0);
    // A V8 code log (JSFLAGS=--log-code ...) shows the measured click between these two marks.
    await E('(function __before_measured_click() { return 1; })()');
    const t0 = await E('performance.now()');
    await click(id); await until(done); await E(frames2);
    await E('(function __after_measured_click() { return 1; })()');
    if (bench === 'clear') await b.send('Emulation.setCPUThrottlingRate', { rate: 1 });
    if (MODE === 'trace') {
      const fin = new Promise(r => { const prev = b.onEvent; b.onEvent = (m) => { prev(m); if (m.method === 'Tracing.tracingComplete') r(); }; });
      await b.send('Tracing.end'); await fin;
      const clickEv = ev.find(e => e.name === 'EventDispatch' && e.args?.data?.type === 'click' && e.ph === 'X');
      const main = (e) => e.pid === clickEv.pid && e.tid === clickEv.tid;
      // The window ends with the first Commit after the click (the frame that shows its rows);
      // later frames (the checks' polling) are not the click's.
      const cm = ev.filter(e => main(e) && e.ph === 'X' && e.name === 'Commit' && e.ts > clickEv.ts + clickEv.dur).sort((a, c) => a.ts - c.ts)[0];
      const end = cm ? cm.ts + cm.dur : clickEv.ts + clickEv.dur;
      const inWin = ev.filter(e => main(e) && e.ts >= clickEv.ts && e.ts < end && (e.ph === 'X' || e.ph === 'B'));
      const inClick = (e) => e.ts >= clickEv.ts && e.ts < clickEv.ts + clickEv.dur;
      const K = { MinorGC: e => e.name === 'MinorGC', MajorGC: e => e.name === 'MajorGC', 'incremental marking step': e => e.name === 'V8.GC_MC_INCREMENTAL',
        'incremental start/finalize': e => /^V8\.GC_MC_INCREMENTAL_(START|FINALIZE)$/.test(e.name), scavenge: e => e.name === 'V8.GC_SCAVENGER' || e.name === 'V8.GC_MINOR_MARK_SWEEPER',
        'V8.InterruptHandler': e => e.name === 'V8.InterruptHandler', 'V8.StackGuard': e => e.name === 'V8.StackGuard' || e.name === 'V8.HandleInterrupts',
        'bytecode budget interrupt (tiering)': e => /BytecodeBudgetInterrupt/.test(e.name),
        'optimizing compile (main thread)': e => /Optimize|Maglev|Turbofan|TurboFan|OSR/.test(e.name), 'deopt': e => /Deopt/i.test(e.name),
        'any GC or interrupt event (top level)': null };
      const r = { it, total: (end - clickEv.ts) / 1000, click: clickEv.dur / 1000, k: {} };
      for (const [k, f] of Object.entries(K)) {
        if (!f) continue;
        const es = inWin.filter(f); r.k[k] = [es.length, es.reduce((a, e) => a + (e.dur || 0), 0) / 1000, es.filter(inClick).length, es.filter(inClick).reduce((a, e) => a + (e.dur || 0), 0) / 1000];
      }
      let gEnd = -1, gN = 0, gD = 0, gIn = 0;
      for (const e of inWin.filter(e => e.ph === 'X' && (/GC/.test(e.name) || /Interrupt/.test(e.name))).sort((a, c) => a.ts - c.ts)) { if (e.ts < gEnd) continue; gEnd = e.ts + e.dur; gN++; gD += e.dur; if (inClick(e)) gIn += e.dur; }
      r.k['any GC or interrupt event (top level)'] = [gN, gD / 1000, 0, gIn / 1000];
      const names = {}; for (const e of inWin) if (/GC|Interrupt|StackGuard|Optimiz|Maglev|Turbo|OSR|Deopt|Compile/i.test(e.name)) names[e.name] = (names[e.name] || 0) + 1;
      r.names = names;
      rows.push(r);
    } else {
      const { result: { profile } } = await b.send('HeapProfiler.stopSampling');
      await b.send('HeapProfiler.disable');
      const byNode = new Map(); const walk = (n, stack) => { byNode.set(n.id, [n.callFrame.functionName || '(anon)', stack]); for (const c of n.children) walk(c, stack + ' < ' + (n.callFrame.functionName || '(anon)')); };
      walk(profile.head, '');
      const fn = {}, total = { b: 0 };
      for (const s of profile.samples) { const [name, stack] = byNode.get(s.nodeId); const k = name + '  [' + stack.split(' < ').slice(-3).join(' < ') + ']'; fn[k] = (fn[k] || 0) + s.size; total.b += s.size; }
      rows.push({ it, bytes: total.b, fn, t0 });
    }
  }
} finally { await b.close(); }
if (process.env.OUT) fs.writeFileSync(process.env.OUT, rows.map(r => JSON.stringify(r)).join('\n') + '\n');
const med = a => { const s = [...a].sort((x, y) => x - y); const n = s.length; return n % 2 ? s[n >> 1] : (s[n / 2 - 1] + s[n / 2]) / 2; };
console.log(`${entry} ${bench} ${MODE}${process.env.NOGC ? ' (no forced GC)' : ''}: n=${rows.length}`);
if (MODE === 'trace') {
  console.log('  total median', med(rows.map(r => r.total)).toFixed(2), 'ms; click script median', med(rows.map(r => r.click)).toFixed(2), 'ms');
  for (const k of Object.keys(rows[0].k)) {
    const c = rows.map(r => r.k[k]);
    console.log(`  ${k}`.padEnd(30), 'per click: count', (c.reduce((a, x) => a + x[0], 0) / rows.length).toFixed(2), 'ms', (c.reduce((a, x) => a + x[1], 0) / rows.length).toFixed(3),
      '| inside the click script: count', (c.reduce((a, x) => a + x[2], 0) / rows.length).toFixed(2), 'ms', (c.reduce((a, x) => a + x[3], 0) / rows.length).toFixed(3), '| clicks with any', c.filter(x => x[0]).length);
  }
  const nm = {}; for (const r of rows) for (const [k, v] of Object.entries(r.names)) nm[k] = (nm[k] || 0) + v;
  console.log('  GC/interrupt/compile event names on the main thread (count over all clicks):', Object.entries(nm).sort((a, c) => c[1] - a[1]).map(([k, v]) => `${k} ${v}`).join(', ') || 'none');
} else {
  console.log('  bytes allocated per click: median', med(rows.map(r => r.bytes)).toFixed(0), 'mean', (rows.reduce((a, r) => a + r.bytes, 0) / rows.length).toFixed(0));
  const fn = {}; for (const r of rows) for (const [k, v] of Object.entries(r.fn)) fn[k] = (fn[k] || 0) + v;
  for (const [k, v] of Object.entries(fn).sort((a, c) => c[1] - a[1]).slice(0, 14)) console.log('   ', (v / rows.length).toFixed(0).padStart(9), 'B/click', k);
}
