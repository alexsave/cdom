// Clicks every button and row action and checks the DOM against what the app should show.
import { launch, sleep, LEGACY, FETCHED } from './cdp.mjs';
const name = process.argv[2] || 'cdom';
const b = await launch();
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
try {
  // BASE serves the entries from somewhere other than the harness server.
  await b.send('Page.navigate', { url: `${process.env.BASE || `http://localhost:${process.env.PORT || 8093}/frameworks/keyed`}/${name}/index.html` });
  for (let i = 0; i < 50 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(100);
  const E = b.evaluate;
  // LEGACY=1 (see cdp.mjs) runs every check on the wasm2js build; either way only one path is fetched.
  const got = await E(FETCHED), mine = ['app.wasm.js', 'cdom.legacy.js'];
  ok(LEGACY ? mine.every(f => got.includes(f)) && !got.includes('app.wasm') : !got.some(f => mine.includes(f)) && got.includes('app.wasm'),
    `${LEGACY ? 'wasm2js' : 'wasm'} path only: ${got.filter(f => /\.(js|wasm)$/.test(f)).join(' ')}`);
  const click = sel => E(`document.querySelector(${JSON.stringify(sel)}).click()`);
  const rows = () => E('document.querySelectorAll("tbody tr").length');
  await click('#run'); ok(await rows() === 1000, 'run -> 1000 rows');
  ok(await E('document.querySelector("tbody tr td").textContent') === '1', 'first id is 1');
  ok(await E('/^\\w+ \\w+ \\w+$/.test(document.querySelector("tbody tr td:nth-child(2) a").textContent)'), 'label shape');
  // The shape alone passes when every row shows one label (a stale template slot did): 1,000 random
  // labels are about 850 distinct, so require most of them to differ.
  const distinct = await E('new Set([...document.querySelectorAll("tbody tr td:nth-child(2) a")].map(a => a.textContent)).size');
  ok(distinct >= 500, `labels differ across rows: ${distinct} distinct of 1000`);
  ok(await E('document.querySelector("tbody tr").outerHTML'), 'row html: ' + await E('document.querySelector("tbody tr").outerHTML'));
  await click('#add'); ok(await rows() === 2000, 'add -> 2000');
  ok(await E('document.querySelector("tbody tr:nth-child(1001) td").textContent') === '1001', 'appended ids continue');
  await click('#update');
  ok(await E('[...document.querySelectorAll("tbody tr")].filter((r,i)=>r.children[1].textContent.endsWith(" !!!") === (i%10===0)).length') === 2000, 'every 10th updated');
  await E('window.__r1 = document.querySelectorAll("tbody tr")[1]; window.__r998 = document.querySelectorAll("tbody tr")[998]');
  await click('#swaprows');
  ok(await E('document.querySelectorAll("tbody tr")[1] === window.__r998 && document.querySelectorAll("tbody tr")[998] === window.__r1'), 'swap moved the same nodes');
  await click('tbody tr:nth-child(5) td:nth-child(2) a');
  ok(await E('document.querySelectorAll("tbody tr.danger").length === 1 && document.querySelectorAll("tbody tr")[4].className === "danger"'), 'select row 5');
  await click('tbody tr:nth-child(7) td:nth-child(2) a');
  ok(await E('document.querySelectorAll("tbody tr.danger").length === 1 && document.querySelectorAll("tbody tr")[6].className === "danger"'), 'select row 7 unselects 5');
  await E('window.__r3 = document.querySelectorAll("tbody tr")[3]; window.__id2 = document.querySelectorAll("tbody tr")[2].children[0].textContent');
  await click('tbody tr:nth-child(3) td:nth-child(3) a span');
  ok(await rows() === 1999, 'remove -> 1999');
  ok(await E('document.querySelectorAll("tbody tr")[2] === window.__r3'), 'remove kept the next node');
  ok(await E('[...document.querySelectorAll("tbody tr td:first-child")].every(t => t.textContent !== window.__id2)'), 'removed id gone');
  await click('#runlots'); ok(await rows() === 10000, 'runlots -> 10000');
  ok(await E('document.querySelectorAll("tbody tr.danger").length') === 0, 'runlots clears selection');
  await click('#clear'); ok(await rows() === 0, 'clear -> 0');
  await click('#run'); ok(await rows() === 1000, 'run after clear');
  await click('#run'); ok(await rows() === 1000, 'run replaces');
  ok(await E('document.querySelector("tbody tr td").textContent') === '13001', 'run replace ids: ' + await E('document.querySelector("tbody tr td").textContent'));
  // Thirty updates: the replaced labels outweigh the live ones twice over, so the app compacts its label arena and then reads labels from the moved bytes.
  for (let i = 0; i < 30; i++) await click('#update');
  ok(await E(`document.querySelector("tbody tr td:nth-child(2) a").textContent.endsWith(${JSON.stringify(' !!!'.repeat(30))})`), 'repeated update');
  ok(await E('[...document.querySelectorAll("tbody tr")].every((r, i) => /^\\w+ \\w+ \\w+( !!!)*$/.test(r.children[1].textContent) && (r.children[1].textContent.split("!!!").length - 1 === (i % 10 === 0 ? 30 : 0)))'), 'labels intact after compaction');
  // The app compacts its label arena and must move every row's offset with it. A row that is not re-read keeps a stale
  // offset unseen, so remove rows (the rows every 10th update reads shift) and compare every label with a model after each step.
  await click('#run');
  const labels = () => E('[...document.querySelectorAll("tbody tr")].map(r => r.children[1].textContent)');
  const model = await labels();
  let bad = '';
  for (let step = 0; step < 40 && !bad; step++) {
    if (step % 10 === 5) { await click('tbody tr:nth-child(1) td:nth-child(3) a span'); model.shift(); }
    else { await click('#update'); for (let i = 0; i < model.length; i += 10) model[i] += ' !!!'; }
    const now = await labels();
    const at = now.findIndex((l, i) => l !== model[i]);
    if (now.length !== model.length || at >= 0) bad = `step ${step}: row ${at} shows "${now[at]}", expected "${model[at]}"`;
  }
  ok(!bad, `update and remove keep every label across arena compaction${bad ? ': ' + bad : ''}`);
} finally { await b.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
