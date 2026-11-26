// The timed runs: PASSES passes (default 5); in each, every (scenario, entry) of scenarios.mjs runs in a
// fresh navigation of page.html (mode time), the entries of a scenario one after another in an order
// rotated by (scenario index + pass), as tools/pair.sh rotates, so no entry always goes first. Before each
// navigation the 1-minute load must be under 4 (bounded wait, 1200 s, then the run is marked LOADGATE and
// goes on); every navigation is bounded to 120 s (B's may not finish: then TIMEOUT, and Chrome restarts).
// B's S6 with m = 1,000 runs once per pass, bounded to 60 s. Then the memory runs (mode mem): S7 and S10,
// each entry a fresh page, every pass. Appends raw rows to ../results/times.tsv and ../results/memory.tsv.
// Usage: PASSES=5 [PASS0=0] [ONLY=S1,S2,S7mem] [ENTRIES=A,L] [MEM=0] [RES=dir] node bench/shared-state/bench/run.mjs
import fs from 'node:fs';
import path from 'node:path';
import { execSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { launch, sleep } from '../../../tools/cdp.mjs';
import { serve } from '../../strings/serve.mjs';
import { SCENARIOS } from './scenarios.mjs';
const here = path.dirname(fileURLToPath(import.meta.url));
const res = process.env.RES || path.join(here, '../results'); // RES: another directory, for a trial run
fs.mkdirSync(res, { recursive: true });
const PASSES = +(process.env.PASSES || 5), PASS0 = +(process.env.PASS0 || 0);
const only = process.env.ONLY ? new Set(process.env.ONLY.split(',')) : null;
const onlyE = process.env.ENTRIES ? new Set(process.env.ENTRIES.split(',')) : null;
const TIMES = path.join(res, 'times.tsv'), MEM = path.join(res, 'memory.tsv');
if (!fs.existsSync(TIMES)) fs.writeFileSync(TIMES, ['pass', 'scenario', 'entry', 'pos', 'load1', 'chrome', 'K', 'n', 'series', 'median_ms', 'samples_ms'].join('\t') + '\n');
if (!fs.existsSync(MEM)) fs.writeFileSync(MEM, ['pass', 'scenario', 'entry', 'load1', 'chrome', 'row_struct_bytes', 'pages_before', 'touchedKB_before', 'pages_after', 'touchedKB_after'].join('\t') + '\n');
const load1 = () => +execSync('sysctl -n vm.loadavg').toString().trim().split(/\s+/)[1];
async function gate() {
    let w = 0;
    while (load1() >= 4) { if (w >= 1200) return 'LOADGATE'; await sleep(10000); w += 10; }
    return '';
}
const median = (v) => { const s = [...v].sort((a, b) => a - b); return s[s.length >> 1]; };
const srv = serve();
let b = await launch();
async function nav(entry, sc, mode, limit) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/shared-state/bench/page.html?entry=${entry}&sc=${sc}&mode=${mode}` });
    let r;
    for (let i = 0; i < limit * 10 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
    if (!r) { await b.close(); b = await launch(); return { error: 'TIMEOUT' }; } // a page stuck in a call: a new Chrome
    return r;
}
const f = (x) => x.toFixed(5);
try {
    for (let p = PASS0; p < PASS0 + PASSES; p++) {
        const plan = SCENARIOS.concat([{ sc: 'S6_B1000', entries: ['B'], limit: 60 }]);
        for (const [si, s] of plan.entries()) {
            if (only && !only.has(s.sc)) continue;
            const es = s.entries.filter((e) => !onlyE || onlyE.has(e));
            for (let k = 0; k < es.length; k++) {
                const e = es[(si + p + k) % es.length];
                const g = await gate();
                const l = load1();
                const r = await nav(e, s.sc, 'time', s.limit || 120);
                const row = (series, K, v) => fs.appendFileSync(TIMES, [p, s.sc, e, k, l, b.version, K, v.length, series, v.length ? f(median(v)) : '', v.map(f).join(',')].join('\t') + '\n');
                if (r.error || g) {
                    fs.appendFileSync(TIMES, [p, s.sc, e, k, l, b.version, '', 0, 'script', g || r.error.split('\n')[0], ''].join('\t') + '\n');
                    console.log(p, s.sc, e, g || r.error.split('\n')[0]);
                    continue;
                }
                row('script', r.K, r.script);
                if (r.total.length) row('total', r.K, r.total);
                if (r.script2) { row('clear_script', r.K, r.script2); row('clear_total', r.K, r.total2); }
                console.log(p, s.sc, e, `K=${r.K}`, `script ${f(median(r.script))}`, r.total.length ? `total ${f(median(r.total))}` : '', r.script2 ? `clear ${f(median(r.script2))}` : '', `load ${l}`);
            }
        }
        if (process.env.MEM !== '0') {
            for (const [si, sc] of ['S7', 'S10'].entries()) {
                if (only && !only.has(sc + 'mem')) continue;
                const es = (sc === 'S7' ? ['A', 'B', 'C', 'E', 'L', 'EI', 'LI'] : ['A', 'C', 'E', 'EI']).filter((e) => !onlyE || onlyE.has(e));
                for (let k = 0; k < es.length; k++) {
                    const e = es[(si + p + k) % es.length];
                    await gate();
                    const l = load1();
                    const r = await nav(e, sc, 'mem', 120);
                    if (r.error) { console.log(p, sc, 'mem', e, r.error); continue; }
                    const bf = r.before || { pages: '', touchedKB: '' };
                    fs.appendFileSync(MEM, [p, sc, e, l, b.version, r.rowStruct ?? '', bf.pages, bf.touchedKB, r.after.pages, r.after.touchedKB].join('\t') + '\n');
                    console.log(p, sc, 'mem', e, JSON.stringify(r));
                }
            }
        }
    }
} finally { await b.close(); srv.close(); }
