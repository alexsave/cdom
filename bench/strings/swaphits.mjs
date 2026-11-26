// Usage: node bench/strings/swaphits.mjs results/pairs/<tag>/traces <entry>...
// HitTest events after the click in each swap trace: how many, and their mean duration (ms, throttled).
import fs from 'fs';
const [dir, ...fws] = process.argv.slice(2);
for (const fw of fws) {
    let n = 0, c = 0, d = 0; const per = [];
    for (const f of fs.readdirSync(dir).filter(f => f.startsWith(fw + '-v') && f.includes('_05_'))) {
        const ev = (j => j.traceEvents || j)(JSON.parse(fs.readFileSync(dir + '/' + f)));
        const click = ev.find(e => e.name === 'EventDispatch' && e.args?.data?.type === 'click');
        const hs = ev.filter(e => e.name === 'HitTest' && e.ph === 'X' && e.ts > click.ts && e.pid === click.pid && e.tid === click.tid);
        n++; c += hs.length; d += hs.reduce((s, e) => s + e.dur, 0); per.push(hs.length);
    }
    console.log(fw, 'traces', n, 'hit tests/trace', (c / n).toFixed(2), 'ms each', (d / c / 1000).toFixed(3), per.join(','));
}
