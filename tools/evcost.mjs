// Event delivery cost: ns per delegated click on a row link (listener walk, inbox, cd_event, the select's ops),
// median of 15 samples of 20,000 clicks, for each entry named on the command line. Needs the jfb server on 8093 (or PORT, or BASE as in quick.mjs).
import { launch, sleep } from './cdp.mjs';
const names = process.argv.slice(2);
const b = await launch();
try {
  for (const name of names) {
    await b.send('Page.navigate', { url: `${process.env.BASE || `http://localhost:${process.env.PORT || 8093}/frameworks/keyed`}/${name}/index.html` });
    for (let i = 0; i < 50 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(100);
    const r = await b.evaluate(`(() => {
      document.getElementById('run').click();
      const a = [...document.querySelectorAll('tbody tr td:nth-child(2) a')].slice(0, 2), v = [];
      for (let s = 0; s < 15; s++) {
        const t0 = performance.now();
        for (let i = 0; i < 20000; i++) a[i & 1].click();
        v.push((performance.now() - t0) * 1e6 / 20000);
      }
      v.sort((x, y) => x - y);
      return +v[7].toFixed(1);
    })()`);
    console.log(name.padEnd(14), r, 'ns per click');
  }
} finally { await b.close(); }
