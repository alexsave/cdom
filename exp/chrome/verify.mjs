// Verifies each flag-arm wrapper: launches Chrome through it (tools/cdp.mjs with CHROME_BIN),
// reads the command line from chrome://version, and runs two behavioural checks in a page:
// a numeric loop (about 10x slower without the optimizing tiers) and a young-generation probe
// (scavenges counted from a trace while 40 MB of short-lived arrays are allocated).
// Usage: node exp/chrome/verify.mjs base noim noopt maxlazy semi64 semi128
import path from 'node:path';
const here = path.dirname(new URL(import.meta.url).pathname);
const { launch, sleep } = await import(path.join(here, '../../tools/cdp.mjs'));
for (const arm of process.argv.slice(2)) {
  if (arm === 'base') delete process.env.CHROME_BIN; else process.env.CHROME_BIN = path.join(here, `${arm}.sh`);
  process.env.CHROME_ARGS = '--js-flags=--expose-gc';
  const b = await launch();
  try {
    await b.send('Page.navigate', { url: 'chrome://version' }); await sleep(800);
    const cl = await b.evaluate('document.getElementById("command_line")?.innerText || ""');
    const js = (cl.match(/--js-flags=\S+( --?[a-z][^ ]*)*/) || [])[0];
    await b.send('Page.navigate', { url: 'about:blank' }); await sleep(300);
    const loop = await b.evaluate(`(() => { function f(n) { const a = new Float64Array(64); let s = 0; for (let i = 0; i < n; i++) { a[i & 63] += i * 0.5; s += a[(i * 7) & 63]; } return s; } f(1e5); const t = performance.now(); f(2e7); return performance.now() - t; })()`);
    const ev = [];
    b.onEvent = (m) => { if (m.method === 'Tracing.dataCollected') ev.push(...m.params.value); };
    await b.send('Tracing.start', { traceConfig: { includedCategories: ['devtools.timeline', 'disabled-by-default-v8.gc', 'v8'] }, transferMode: 'ReportEvents' });
    await b.evaluate(`(() => { gc(); let keep = 0; for (let i = 0; i < 40000; i++) { const a = new Array(128).fill(i); keep += a.length; } return keep; })()`);
    const done = new Promise(r => { const prev = b.onEvent; b.onEvent = (m) => { prev(m); if (m.method === 'Tracing.tracingComplete') r(); }; });
    await b.send('Tracing.end'); await done;
    const minor = ev.filter(e => e.name === 'MinorGC' && e.ph === 'X').length, inc = ev.filter(e => e.name === 'V8.GC_MC_INCREMENTAL').length;
    console.log(arm.padEnd(8), 'js-flags:', js, '| loop 2e7:', loop.toFixed(1), 'ms | MinorGC while allocating ~40 MB:', minor, '| incremental steps:', inc);
  } finally { await b.close(); }
}
