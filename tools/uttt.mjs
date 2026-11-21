// Drives demo/uttt (the Ultimate Tic-Tac-Toe replay viewer) in Chrome through CDP, on the WebAssembly tier and on the
// frames tier (LEGACY=frames, and its ?ns4 flavour: tools/cdp.mjs), with real input; exit 1 on any failure.
//   FOOLISH=path/to/foolish node tools/uttt.mjs
// FOOLISH is a checkout of github.com/alexsave/foolish (../foolish beside cdom unless set): the viewer is built from its
// rules and code, and so are the expected positions (tools/uttt_expect.c, built natively against the same files), so
// nothing here is worked out by hand. With no checkout there it says so and exits 0: CI has no foolish, and does not
// run it. UTTT_MEASURE=1 also prints the sizes and a step's redraw time (demo/uttt/README.md's predictions).
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import zlib from 'node:zlib';
import { execFileSync, spawnSync } from 'node:child_process';

const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const FOOLISH = path.resolve(process.env.FOOLISH || path.join(top, '../foolish'));
const out = path.join(top, 'demo/uttt/out');
const CODE = 'AJG7NYNQG4RFXXGVPGD7QR2ETSOYTWMCREAQ'; // foolish's uttt/web/README.md's example
if (!fs.existsSync(path.join(FOOLISH, 'uttt/c/src/uttt_code.c'))) {
  console.log(`skip tools/uttt.mjs: FOOLISH=${FOOLISH} is not a checkout of github.com/alexsave/foolish (set FOOLISH to one)`);
  process.exit(0);
}

if (!process.env.UTTT_PROFILE) {
  // The parent: build the viewer and the expectations, then one child a profile, since tools/cdp.mjs fixes the tier
  // for its process (LEGACY).
  execFileSync('make', ['-s', '-C', path.join(top, 'demo/uttt'), `FOOLISH=${FOOLISH}`], { stdio: 'inherit' });
  const src = path.join(FOOLISH, 'uttt/c/src'), shared = path.join(FOOLISH, 'shared/c');
  execFileSync(process.env.HOST_CC || 'cc', ['-std=c2x', '-O1', '-Wall', '-Wextra', '-Werror', `-I${src}`, '-o', path.join(out, 'uttt_expect'),
    path.join(top, 'tools/uttt_expect.c'), path.join(src, 'uttt.c'), path.join(src, 'uttt_code.c'), path.join(shared, 'b32.c'),
    path.join(shared, 'mixrad.c'), '-lm'], { stdio: 'inherit' });
  fs.writeFileSync(path.join(out, 'expect.txt'), execFileSync(path.join(out, 'uttt_expect'), [CODE], { encoding: 'utf8' }));
  if (process.env.UTTT_MEASURE) {
    const sz = (f) => { const b = fs.readFileSync(path.join(out, f)); return [b.length, zlib.gzipSync(b, { level: 9 }).length]; };
    const files = ['uttt.html', 'cdom.frames.js', 'uttt.wasm.ie9.js'], s = files.map(sz);
    for (let i = 0; i < files.length; i++) console.log(`measure ${files[i]} ${s[i][0]} bytes, ${s[i][1]} gzipped`);
    console.log(`measure the frames tier's three files ${s.reduce((a, x) => a + x[0], 0)} bytes, ${s.reduce((a, x) => a + x[1], 0)} gzipped`);
    console.log(`measure uttt.wasm ${sz('uttt.wasm')[0]} bytes, ${sz('uttt.wasm')[1]} gzipped`);
    const ref = path.join(top, 'tests/out/frames.wasm.ie9.js');
    if (fs.existsSync(ref)) console.log(`measure uttt.wasm.ie9.js is ${s[2][0] - fs.statSync(ref).size} bytes larger than tests/out/frames.wasm.ie9.js`);
  }
  let failed = 0;
  for (const [profile, legacy] of [['webassembly', ''], ['frames', 'frames'], ['frames-ns4', 'frames']]) {
    console.log(`-- ${profile}`);
    const r = spawnSync(process.execPath, [new URL(import.meta.url).pathname], { stdio: 'inherit', env: { ...process.env, LEGACY: legacy, UTTT_PROFILE: profile } });
    if (r.status !== 0) failed++;
  }
  console.log(failed ? `${failed} profile(s) FAILED` : 'all ok');
  process.exit(failed ? 1 : 0);
}

// A child: one profile.
const { launch, sleep, TIER, FETCHED, tierFetched } = await import('./cdp.mjs');
const PROFILE = process.env.UTTT_PROFILE, FRAMES = TIER === 5, flavour = PROFILE === 'frames-ns4' ? '&ns4' : '';
const lines = fs.readFileSync(path.join(out, 'expect.txt'), 'utf8').trim().split('\n');
const n = +lines[0].split(' ')[1];
// The position after k plies, as tools/uttt_expect.c printed it.
const want = lines.slice(1).map((l) => { const [k, cells, blocks, bgs, over, turn, last] = l.split(' '); return { k: +k, cells, blocks, bgs, over, turn, last: +last }; });
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => {
  const f = path.join(out, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, body) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] || 'text/plain' }); r.end(body); });
}).listen(0);
const b = await launch();
let fails = 0;
setTimeout(() => { console.log('FAIL no result within 170 s'); process.exit(1); }, 170000).unref();
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', `${PROFILE}:`, m); } else console.log('ok  ', `${PROFILE}:`, m); };
const E = b.evaluate;
const D = FRAMES ? 'frames[0].document' : 'document';
const q = (sel) => `${D}.querySelector(${JSON.stringify(sel)})`;
const mouse = (type, x, y, buttons = 0, button = 'none') =>
  b.send('Input.dispatchMouseEvent', { type, x, y, buttons, button, clickCount: type === 'mouseMoved' ? 0 : 1 });
async function click(sel) {
  const [x, y] = await E(`(function () { var e = ${q(sel)}, r; if (!e) return [-1, -1]; r = e.getBoundingClientRect();
    return [Math.round((r.left + r.right) / 2), Math.round((r.top + r.bottom) / 2)]; })()`);
  if (x < 0) return;
  await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y, 1, 'left'); await sleep(20);
  await mouse('mouseReleased', x, y, 0, 'left'); await sleep(40);
}
async function until(expr, tries = 100) {
  for (let i = 0; i < tries && !(await E(expr)); i++) await sleep(20);
  return E(expr);
}
const draws = () => (FRAMES ? E('cdomF.draws') : 0);
const status = () => E(`(function () { var e = ${q('#status')}; return e ? e.innerText.replace(/^\s+|\s+$/g, "") : null; })()`);
const errLine = () => E(`(function () { var e = ${q('#err')}; return e ? e.innerText.replace(/^\s+|\s+$/g, "") : null; })()`);
const playLabel = () => E(`(function () { var e = ${q('#play')}; return e ? (e.tagName === "INPUT" ? e.value : e.innerText).replace(/^\s+|\s+$/g, "") : null; })()`);
const statusOf = (w) => `Move ${w.k} of ${n}. ` + (w.over === 'X' ? 'X wins.' : w.over === 'O' ? 'O wins.' : w.over === 'D' ? 'Draw.'
  : w.k === n ? `${w.turn} to play; the game stops here.` : `${w.turn} to play.`);
// What the page reads back is ES3 (no trim: LEGACY=frames is IE 6's language too).
// The board as the page shows it, read back from its DOM: the cells (- where a block is one big mark), the big
// marks (. for a block of cells), each block's background (L, W, D, . as tools/uttt_expect.c names them) and the
// cells in bold.
const BOARD = `(function () { var d = ${D}, cells = "", blocks = "", bgs = "", bold = [], b, i, m, e, c, s, t;
  for (b = 0; b < 9; b++) {
    e = d.querySelector("#b" + b);
    m = d.querySelector("#m" + b);
    blocks += !e ? "?" : m ? m.innerText.replace(/^\s+|\s+$/g, "") : ".";
    c = !e ? null : ${FRAMES ? 'e.querySelector("td[bgcolor]") ? e.querySelector("td[bgcolor]").getAttribute("bgcolor") : ""'
      : 'e.querySelector("div") ? e.querySelector("div").style.backgroundColor : ""'};
    bgs += c === null ? "?" : c === "" ? "." : c === "#ffff66" || c === "rgb(255, 255, 102)" ? "L" : c === "#99ee99" || c === "rgb(153, 238, 153)" ? "W"
      : c === "#cccccc" || c === "rgb(204, 204, 204)" ? "D" : "?";
  }
  for (i = 0; i < 81; i++) {
    e = d.querySelector("#c" + i);
    t = e ? e.innerText.replace(/^\s+|\s+$/g, "") : null;
    cells += blocks.charAt((i - i % 9) / 9) === "X" || blocks.charAt((i - i % 9) / 9) === "O" ? (e ? "!" : "-") : t === "X" || t === "O" || t === "." ? t : "?";
    if (e && e.querySelector("b")) bold.push(i);
  }
  return [cells, blocks, bgs, bold.join(" ")]; })()`;
// What the board must be after w's plies: a won block's cells are not drawn (its big mark is).
function expected(w) {
  const blocks = [...w.blocks].map((c) => (c === 'X' || c === 'O' ? c : '.')).join('');
  const cells = [...w.cells].map((c, i) => (blocks[Math.floor(i / 9)] === '.' ? c : '-')).join('');
  const bold = w.last >= 0 && blocks[Math.floor(w.last / 9)] === '.' ? String(w.last) : '';
  return [cells, blocks, w.bgs, bold];
}
async function boardIs(k, what) {
  const got = await E(BOARD), exp = expected(want[k]), st = await status();
  const same = got.join('|') === exp.join('|') && st === statusOf(want[k]);
  if (!same) {
    const parts = ['cells', 'big marks', 'backgrounds', 'bold'].map((p, i) => (got[i] === exp[i] ? '' : `\n  ${p}: shown ${JSON.stringify(got[i])}\n  ${' '.repeat(p.length)}  want  ${JSON.stringify(exp[i])}`)).join('');
    ok(false, `${what}: the board is the game after ${k} plies${parts}${st === statusOf(want[k]) ? '' : `\n  status ${JSON.stringify(st)}, want ${JSON.stringify(statusOf(want[k]))}`}`);
  }
  return same;
}
const booted = () => until(`!!window.READY${FRAMES ? ' && window.cdomF && cdomF.draws > 0' : ''}`);
async function type(text) {
  await click('#code');
  await E(`(function () { var f = ${q('#code')}; if (f) { f.focus(); f.select(); } })()`);
  await b.send('Input.insertText', { text }); await sleep(30);
  await click('#open');
  await sleep(FRAMES ? 320 : 40); // the frames tier draws a change 250 ms later (docs/frames-design.md, section 6)
}
const port = srv.address().port;
const page = (query) => `http://127.0.0.1:${port}/uttt.html${query}`;

run: try {
  await b.send('Page.navigate', { url: page(flavour ? `?${flavour.slice(1)}` : '') });
  ok(await booted(), 'booted');
  if (!(await E('!!window.READY'))) break run;
  ok(...tierFetched(await E(FETCHED), 'uttt'));
  if (FRAMES) ok(await E('frames.length === 1 && typeof document.getElementById === "undefined"'), 'the page is the frames tier: a frameset of one frame, no getElementById');
  else ok(await E('frames.length === 0'), 'the page is the WebAssembly tier: no frameset');
  ok((await status()) === 'No game open.' && !(await E(`!!${q('#c0')}`)), `no code: no board, and the status says so: ${JSON.stringify(await status())}`);

  // A bad code: the error line, and no board; the page lives on and opens a good one after it.
  await type('NOTACODE');
  ok((await errLine()).includes('not a game code') && (await status()) === 'No game open.' && !(await E(`!!${q('#c0')}`)),
    `a bad code shows the error line and no board: ${JSON.stringify(await errLine())}, ${JSON.stringify(await status())}`);
  await type(`https://uttt.live/${CODE}`);
  ok((await errLine()) === '' && (await status()) === statusOf(want[0]), `after the bad code, a uttt.live link opens: ${JSON.stringify(await status())}`);
  ok(lines[0] === `ok ${n}` && n > 0 && (await status()).endsWith(`of ${n}. X to play.`), `the move count is uttt_replay_read's n_plies, ${n}`);
  await boardIs(0, 'opened');

  // > k times: the game after k plies, for every k; one draw a step on the frames tier.
  let all = true, oneDraw = true;
  for (let k = 1; k <= n; k++) {
    const d = await draws();
    await click('#fwd');
    if (FRAMES) await until(`cdomF.draws > ${d}`, 25);
    if (FRAMES && (await draws()) !== d + 1) oneDraw = false;
    if (!(await boardIs(k, `> ${k} times`))) { all = false; break; }
  }
  ok(all, `> k times shows the game after k plies, for k = 1 to ${n} (cells, big marks, the forced block, the last move in bold, the status)`);
  if (FRAMES) ok(oneDraw, 'each > draws the frame once');
  await click('#fwd');
  ok(await boardIs(n, '> at the end'), '> at the end stays at the end');
  for (let k = n - 1; k >= n - 3; k--) await click('#back');
  ok(await boardIs(n - 3, '< three times'), '< goes back a move');
  await click('#start');
  ok(await boardIs(0, '|<'), '|< goes to the start');
  await click('#back');
  ok(await boardIs(0, '< at the start'), '< at the start stays there');
  await click('#end');
  ok(await boardIs(n, '>|'), `>| goes to the end: ${JSON.stringify(await status())}`);

  // Play advances a move a tick, Pause stops it, and reaching the end stops it.
  await click('#start');
  await click('#play');
  ok((await playLabel()) === 'Pause', `Play becomes Pause: ${JSON.stringify(await playLabel())}`);
  ok(await until(`(function () { var e = ${q('#status')}; return !!e && e.innerText.indexOf("Move 2 of") === 0; })()`, 100), `Play advances: ${JSON.stringify(await status())}`);
  await click('#play');
  const paused = await status();
  await sleep(1300);
  ok((await status()) === paused && (await playLabel()) === 'Play', `Pause stops it: ${JSON.stringify(paused)}, then ${JSON.stringify(await status())}`);
  await click('#end');
  for (let k = 0; k < 3; k++) await click('#back');
  await click('#play');
  await until(`(function () { var e = ${q('#play')}; return !!e && (e.value || e.innerText) === "Play"; })()`, 200);
  const d0 = await draws();
  await sleep(1400);
  ok((await playLabel()) === 'Play' && (await boardIs(n, 'Play to the end')) && (await draws()) === d0,
    `Play stops at the end: ${JSON.stringify(await playLabel())}, ${JSON.stringify(await status())}, ${(await draws()) - d0} draws after`);
  await click('#play');
  ok(await boardIs(0, 'Play at the end') && (await playLabel()) === 'Pause', 'Play at the end starts again from the start');
  await click('#play');

  if (FRAMES && process.env.UTTT_MEASURE) {
    // A step's redraw, timed in the page: 20 steps through the buttons' own handlers.
    // Each handler is looked up again, since every draw writes the frame's document afresh.
    const d = await draws();
    const ms = await E(`(function () { var t = new Date(), i; for (i = 0; i < 10; i++) { ${q('#fwd')}.onclick(); ${q('#start')}.onclick(); }
      return (new Date() - t) / 20; })()`);
    console.log(`measure a step's redraw on the frames tier: ${ms} ms (${(await draws()) - d} draws for 20 steps)`);
  }

  // The address's query opens the game at once.
  await b.send('Page.navigate', { url: page(`?${CODE}${flavour}`) });
  ok(await booted(), 'booted with the code in the query');
  await until(`(function () { var e = ${q('#status')}; return !!e && e.innerText.indexOf("Move 0 of") === 0; })()`);
  ok((await boardIs(0, 'the query')) && (await E(`${q('#code')}.value`)) === CODE, `uttt.html?<code> opens the game: ${JSON.stringify(await status())}`);
  await click('#end');
  ok(await boardIs(n, 'the query, then >|'), 'and it steps');

  const errs = b.exceptions.filter((e) => !/ReferenceError: TextDecoder is not defined$/.test(e));
  ok(!errs.length, `no page exception: ${errs.join('; ')}`);
  if (FRAMES) ok((await E('cdomF.errors.join("; ")')) === '', `the frames shim noted no error: ${await E('cdomF.errors.join("; ")')}`);
} catch (e) {
  fails++;
  console.log('FAIL', `${PROFILE}:`, e.stack || e);
}
await b.close(); srv.close();
console.log(fails ? `${PROFILE}: ${fails} FAILED` : `${PROFILE}: all ok`);
process.exit(fails ? 1 : 0);
