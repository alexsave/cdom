// Dev loop: synchronous script time of each button click (no paint), median of N, unthrottled.
import { launch, sleep } from './cdp.mjs';
const names = process.argv.slice(2);
const b = await launch();
try {
  for (const name of names) {
    await b.send('Page.navigate', { url: `http://localhost:8093/frameworks/keyed/${name}/index.html` });
    for (let i = 0; i < 50 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(100);
    const r = await b.evaluate(`(() => {
      const c = s => document.querySelector(s).click();
      const t = (pre, s, n) => { const v = []; for (let i = 0; i < n; i++) { pre && pre(); const t0 = performance.now(); c(s); v.push(performance.now() - t0); } v.sort((a, b) => a - b); return +v[n >> 1].toFixed(3); };
      const out = {};
      out.run1k = t(() => c('#clear'), '#run', 31);
      out.replace1k = t(() => c('#run'), '#run', 31);
      c('#run');
      out.swap = t(null, '#swaprows', 101);
      out.update = t(null, '#update', 31);
      let k = 0; out.select = t(() => k++, 'tbody tr:nth-child(' + 5 + ') td:nth-child(2) a', 101);
      out.append = t(() => { c('#clear'); c('#run'); }, '#add', 21);
      out.clear1k = t(() => c('#run'), '#clear', 31);
      out.create10k = t(() => c('#clear'), '#runlots', 9);
      return out;
    })()`);
    console.log(name.padEnd(14), JSON.stringify(r));
  }
} finally { await b.close(); }
