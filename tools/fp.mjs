// First paint the way the harness's size run takes it (a fresh browser, one load, the page's own
// first-paint entry), interleaved across entries so they share the load. Prints the median,
// quartiles and each entry's median ratio to the first entry.
// Usage: node tools/fp.mjs vanillajs,cdomb,cdomp [samples=15]
import { launch, sleep } from './cdp.mjs';
const fws = process.argv[2].split(','), N = +(process.argv[3] || 15);
const v = fws.map(() => []);
for (let s = 0; s < N; s++) for (let j = 0; j < fws.length; j++) {
  const b = await launch();
  try {
    await b.send('Page.bringToFront');
    await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${fws[j]}/index.html` });
    let fp;
    for (let i = 0; i < 200 && !fp; i++) { await sleep(20); fp = await b.evaluate('performance.getEntriesByType("paint").find(e => e.name === "first-paint")?.startTime'); }
    v[j].push(fp);
  } finally { await b.close(); }
}
const q = (a, f) => { const s = [...a].sort((x, y) => x - y); return s[Math.floor(f * (s.length - 1))]; };
const m0 = q(v[0], 0.5);
fws.forEach((f, j) => console.log(f.padEnd(12), `median ${q(v[j], 0.5).toFixed(1)}  q1 ${q(v[j], 0.25).toFixed(1)}  q3 ${q(v[j], 0.75).toFixed(1)}  ratio ${(q(v[j], 0.5) / m0).toFixed(3)}`));
