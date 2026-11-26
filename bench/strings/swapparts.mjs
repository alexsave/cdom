// Usage: node bench/strings/swapparts.mjs results/pairs/<tag>/traces <entry>... (ALL=1 prints every trace)
// Per-trace parts of the measured swap (ms, throttled): the click's FunctionCall, the click dispatch,
// UpdateLayoutTree+Layout after it, PrePaint, the gap to the frame task, the frame's HitTest, Paint, Layerize, Commit, and the total.
import fs from 'fs';
const [dir, ...fws] = process.argv.slice(2);
const med = a => { const s = [...a].sort((x, y) => x - y); return s[s.length >> 1]; };
for (const fw of fws) {
    const rows = [];
    for (const f of fs.readdirSync(dir).filter(f => f.startsWith(fw + '-v') && f.includes(process.env.B || '_05_'))) {
        const t = JSON.parse(fs.readFileSync(dir + '/' + f)); const ev = t.traceEvents || t;
        const names = new Map();
        for (const e of ev) if (e.ph === 'M' && e.name === 'thread_name') names.set(`${e.pid}:${e.tid}`, e.args.name);
        const main = ev.filter(e => e.ph === 'X' && names.get(`${e.pid}:${e.tid}`) === 'CrRendererMain').sort((a, c) => a.ts - c.ts || c.dur - a.dur);
        const click = main.find(e => e.name === 'EventDispatch' && e.args?.data?.type === 'click');
        const after = main.filter(e => e.ts >= click.ts);
        const sum = (n, from, to) => after.filter(e => e.name === n && e.ts >= from && e.ts < to).reduce((s, e) => s + e.dur, 0) / 1000;
        const fc = after.find(e => e.name === 'FunctionCall' && e.ts < click.ts + click.dur);
        const paint = after.find(e => e.name === 'Paint');
        const frame = after.filter(e => e.name === 'RunTask' && e.ts <= paint.ts && e.ts + e.dur >= paint.ts + paint.dur).pop();
        if (!frame) { console.error("no frame task", f); continue; }
        const pp = after.find(e => e.name === 'PrePaint');
        const commit = after.find(e => e.name === 'Commit' && e.ts > paint.ts);
        const end = commit.ts + commit.dur;
        rows.push({
            fc: fc ? fc.dur / 1000 : 0, click: click.dur / 1000,
            layout: sum('UpdateLayoutTree', click.ts, pp.ts) + sum('Layout', click.ts, pp.ts), prepaint: pp.dur / 1000,
            gap: (frame.ts - pp.ts - pp.dur) / 1000, hit: sum('HitTest', frame.ts, frame.ts + frame.dur), paint: paint.dur / 1000,
            layerize: sum('Layerize', frame.ts, end), commit: commit.dur / 1000, total: (end - click.ts) / 1000,
        });
    }
    const ks = Object.keys(rows[0]);
    if (process.env.ALL) for (const r of rows.sort((a, b) => a.total - b.total)) console.log("   ", ks.map(k => r[k].toFixed(2).padStart(6)).join(""));
    console.log(fw.padEnd(8), ks.map(k => `${k} ${med(rows.map(r => r[k])).toFixed(2)}`).join('  '));
}
