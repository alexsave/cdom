// DOM identity: drives every action in the same order on two entries with Math.random pinned,
// and compares #main's outerHTML after each step. Exit 1 on the first difference.
// Usage: node tools/domcheck.mjs cdomb cdomp
import { launch, sleep } from './cdp.mjs';
const [a, c] = process.argv.slice(2);
const steps = ['#run', '#add', '#update', '#swaprows', 'tbody tr:nth-child(5) td:nth-child(2) a', '#update',
  'tbody tr:nth-child(3) td:nth-child(3) a span', 'tbody tr:nth-child(7) td:nth-child(2) a', '#swaprows', '#run',
  'tbody tr:nth-child(1000) td:nth-child(3) a span', 'tbody tr:nth-child(1) td:nth-child(3) a span', '#clear',
  '#runlots', '#update', 'tbody tr:nth-child(9999) td:nth-child(2) a', '#add', '#swaprows', '#clear', '#add', '#run'];
async function snaps(name) {
  const b = await launch();
  try {
    await b.send('Page.addScriptToEvaluateOnNewDocument', { source: 'Math.random = (() => { let s = 12345; return () => (s = (s * 1103515245 + 12345) % 2147483648) / 2147483648; })();' });
    await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${name}/index.html` });
    for (let i = 0; i < 100 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(50);
    const out = [await b.evaluate('document.getElementById("main").outerHTML')];
    for (const s of steps) {
      await b.evaluate(`document.querySelector(${JSON.stringify(s)}).click()`);
      out.push(await b.evaluate('document.getElementById("main").outerHTML'));
    }
    return out;
  } finally { await b.close(); }
}
const [x, y] = [await snaps(a), await snaps(c)];
let bad = 0;
for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) {
  let k = 0; while (x[i][k] === y[i][k]) k++;
  console.log(`step ${i} (${i ? steps[i - 1] : 'load'}) differs at ${k}:\n  ${a}: ${x[i].slice(Math.max(0, k - 60), k + 60)}\n  ${c}: ${y[i].slice(Math.max(0, k - 60), k + 60)}`);
  bad++; break;
}
console.log(bad ? 'DOM DIFFERS' : `DOM identical over ${x.length} snapshots (${x.reduce((s, h) => s + h.length, 0)} chars)`);
process.exit(bad ? 1 : 0);
