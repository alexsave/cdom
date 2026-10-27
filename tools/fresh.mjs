// Harness-shaped script timing: each sample loads the page fresh, replays the harness's warm-up
// clicks for that benchmark (a frame between clicks), then times the measured click
// synchronously (script only, no paint). Entries alternate per sample so they share the load.
// Prints the median ms per benchmark and each entry's ratio to the first entry.
// Usage: node tools/fresh.mjs vanillajs,cdomp [samples=15] [bench filter, e.g. 01,07]
import { launch, sleep } from './cdp.mjs';
const fws = process.argv[2].split(',');
const N = +(process.argv[3] || 15);
const only = process.argv[4] ? process.argv[4].split(',') : null;
const row = (i, c) => `tbody>tr:nth-of-type(${i})>td:nth-of-type(${c})`;
const W = 5;
const runClear = Array.from({ length: W }, () => ['#run', '#clear']).flat();
const B = {
  '01_run1k': [runClear, '#run'],
  '02_replace1k': [Array(W).fill('#run'), '#run'],
  '03_update10th1k': [['#run', ...Array(W).fill('#update')], '#update'],
  '04_select1k': [['#run', row(5, 2) + '>a'], row(2, 2) + '>a'],
  '05_swap1k': [['#run', ...Array(W + 1).fill('#swaprows')], '#swaprows'],
  '06_remove-one-1k': [['#run', ...Array.from({ length: W }, (_, i) => row(W - i + 4, 3) + '>a>span'), row(6, 3) + '>a>span'], row(4, 3) + '>a>span'],
  '07_create10k': [runClear, '#runlots'],
  '08_create1k-after1k': [[...runClear, '#run'], '#add'],
  '09_clear1k': [[...runClear, '#run'], '#clear'],
};
const b = await launch();
const res = {};
try {
  for (const [name, [init, target]] of Object.entries(B)) {
    if (only && !only.some(o => name.startsWith(o))) continue;
    res[name] = fws.map(() => []);
    for (let s = 0; s < N; s++) for (let j = 0; j < fws.length; j++) {
      await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${fws[j]}/index.html` });
      for (let i = 0; i < 50 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(50);
      await b.send('Page.bringToFront');
      const t = await b.evaluate(`(async () => {
        const frame = () => new Promise(r => { const t = setTimeout(r, 200); requestAnimationFrame(() => { clearTimeout(t); setTimeout(r, 0); }); });
        for (const s of ${JSON.stringify(init)}) { document.querySelector(s).click(); await frame(); }
        const el = document.querySelector(${JSON.stringify(target)});
        await frame();
        const t0 = performance.now(); el.click(); return performance.now() - t0;
      })()`);
      res[name][j].push(t);
    }
    const med = res[name].map(v => { const a = [...v].sort((x, y) => x - y); return a[a.length >> 1]; });
    console.log(name.padEnd(20), fws.map((f, j) => `${f} ${med[j].toFixed(3)}${j ? ` (${(med[j] / med[0]).toFixed(3)})` : ''}`).join('  '));
  }
} finally { await b.close(); }
