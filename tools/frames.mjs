// Drives tests/out/frames.html on the frames tier (docs/frames-design.md) with real input through CDP; exit 1 on any
// failure. LEGACY=frames node tools/frames.mjs: every frame of the page has no DOM mutation (tools/cdp.mjs, FRAMES_OLD),
// so only document.write paths work. FRAMES_MEASURE=1 also prints the measurements of section 11 (redraw cost a node,
// bytes a node, history.length for each way of rewriting a frame), which are timings and are never asserted.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep, TIER, FETCHED, tierFetched } from './cdp.mjs';

if (TIER !== 5) { console.log('FAIL tools/frames.mjs runs under LEGACY=frames'); process.exit(1); }
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
// FRAMES_APP=ie9 serves each app's IE 9 build (url + ".ie9.js") where the boot asks for the transformed file (url +
// ".js1.js"): the run the measurements compare with (docs/js1-transform-design.md).
const IE9_APP = process.env.FRAMES_APP === 'ie9';
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => {
  let f = path.join(root, q.url.split(/[?#]/)[0]);
  if (IE9_APP) f = f.replace(/\.js1\.js$/, '.ie9.js');
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); });
}).listen(0);
const b = await launch();
let fails = 0;
setTimeout(() => { console.log('FAIL no result within 120 s'); process.exit(1); }, 120000).unref();
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const E = b.evaluate;
const F = 'frames[0].document';
const mouse = (type, x, y, buttons = 0, button = 'none') =>
  b.send('Input.dispatchMouseEvent', { type, x, y, buttons, button, clickCount: type === 'mouseMoved' ? 0 : 1 });
// An element of the frame by selector, its centre in the top window's coordinates (the frame fills the window).
// [-1, -1] when there is no such element (a broken shim), which clickAt does not click.
const centre = (sel) => E(`(function () { var e = ${F}.querySelector(${JSON.stringify(sel)}), r; if (!e) return [-1, -1]; r = e.getBoundingClientRect();
  return [Math.round((r.left + r.right) / 2), Math.round((r.top + r.bottom) / 2)]; })()`);
async function clickAt(x, y) {
  if (x < 0) return;
  await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y, 1, 'left'); await sleep(30);
  await mouse('mouseReleased', x, y, 0, 'left'); await sleep(60);
}
const click = async (sel) => clickAt(...await centre(sel));
const draws = () => E('cdomF.draws');
const stat = (k) => E(`X.stat(${k})`);
const STAT = { count: 0, path: 1, inputs: 2, changes: 3, checks: 4, focuses: 5, blurs: 6, ticks: 7, items: 8 };
const textOf = (sel) => E(`(function () { var e = ${F}.querySelector(${JSON.stringify(sel)}); return e ? e.innerText : null; })()`);
// The field the frame's form calls name cN for the tree's input of id attribute id.
// A stand-in with no value and methods that do nothing when there is no such field (a broken shim), so a check fails
// on its own assertion and the run goes on.
const field = (id) => `(${F}.forms[0].elements[(function () { var i; for (i = 0; i < cf_nfields; i++) if (cf_pair(cf_fields[i].a, "id") === ${JSON.stringify(id)}) return i; return -1; })()] || { select: function () {}, setSelectionRange: function () {} })`;
// Waits until expr is true, or n tries of 20 ms.
async function until(expr, n = 100) {
  for (let i = 0; i < n && !(await E(expr)); i++) await sleep(20);
  return E(expr);
}
// The frame's document as markup (innerHTML and outerHTML are gone under LEGACY=frames; XMLSerializer is not), and the
// same for the shim's serialization of the tree as it is now, parsed afresh: equal when the frame shows the tree.
const shown = `new XMLSerializer().serializeToString(${F}.body)`;
const fresh = 'new XMLSerializer().serializeToString(new DOMParser().parseFromString(cf_html(), "text/html").body)';
async function matches(what) {
  // The frame's margins are the BODY's attributes in Chrome's DOM: not the shim's.
  const [a, c] = [(await E(shown)).replace(' marginwidth="8" marginheight="8"', ''), await E(fresh)];
  ok(a === c && a.length > 100, `${what}: the frame's HTML is the tree's (${a.length} chars)${a === c ? '' : `\n  frame ${a.slice(0, 400)}\n  tree  ${c.slice(0, 400)}`}`);
}
// The tree's elements that have an id and are drawn, each with its text: every one must be in the frame with that text.
const TREE_IDS = `(function () { var out = [];
  function walk(x, hidden) {
    var id, c;
    if (x.y !== 1) return;
    hidden = hidden || cf_in(cf_SKIP, x.t) || cf_pair(x.s, "display") === "none";
    id = cf_pair(x.a, "id");
    if (id) out.push([id, hidden, x.t === "input" || x.t === "button" ? null : cf_textOf(x)]);
    for (c = x.f; c; c = c.n) walk(c, hidden);
  }
  walk(cf_mount, false); return out; })()`;

// Wraps every function of the shim (cf_) and of the app file (c_), cdomF's handlers, the app's exports (cf_raw) and the
// shim's imports (cf_imports.cdom), each counting the depth in window.__D and its most in window.__maxD.
const DEPTH_WRAP = `(function () {
  window.__D = 0; window.__maxD = 0;
  function wrap(o, k) { var f = o[k]; o[k] = function () { if (++window.__D > window.__maxD) window.__maxD = window.__D;
    try { return f.apply(this, arguments); } finally { window.__D--; } }; }
  var k, all = [[window, /^(c|cf)_/], [cdomF, /./], [cf_raw, /./], [cf_imports.cdom, /./]], i;
  for (i = 0; i < all.length; i++) for (k in all[i][0]) if (all[i][1].test(k) && typeof all[i][0][k] === 'function') wrap(all[i][0], k);
  return 1; })()`;
let maxDepth = 0, depthBefore = 0;
const port = srv.address().port;
const measure = !!process.env.FRAMES_MEASURE;
run: try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/frames.html` });
  ok(await until('!!window.READY && window.cdomF && cdomF.draws > 0'), 'booted, and the frame drawn');
  if (!(await E('!!window.READY'))) break run;
  // The call depth (docs/js1-transform-design.md, T5), measured only: every function of the shim and the app file, the
  // shim's handlers, the app's exports and the shim's imports, counted on the way in and out from here on.
  if (measure) await E(DEPTH_WRAP);
  ok(...tierFetched(await E(FETCHED), 'frames'));
  ok(await E('typeof cdom === "undefined" && typeof document.getElementById === "undefined" && typeof document.createElement === "undefined"'
    + ` && typeof ${F}.createElement === "undefined" && typeof ${F}.body.appendChild === "undefined" && typeof ${F}.body.addEventListener === "undefined"`
    + ` && typeof ${F}.body.attachEvent === "undefined" && !("innerHTML" in ${F}.body)`),
    'no DOM mutation in the page or its frame: no createElement, appendChild, innerHTML, getElementById or listeners');
  ok(await E('frames.length === 1 && document.querySelector("frameset") !== null && document.querySelector("div#m") === null'),
    'the page is a frameset of one frame: the FRAMESET written while it parsed took the body\'s place');
  ok((await draws()) === 1, `cd_main drew once: ${await draws()}`);
  const h0 = await E('history.length');
  const errs = (x) => b.exceptions.filter((e) => !/ReferenceError: TextDecoder is not defined$/.test(e));
  await matches('after cd_main');
  const ids = await E(TREE_IDS);
  const bad = [];
  for (const [id, hidden, text] of ids) {
    const there = await E(`!!${F}.querySelector("#" + ${JSON.stringify(id)})`);
    if (hidden ? there : !there) bad.push(`${id} ${hidden ? 'drawn' : 'missing'}`);
    else if (!hidden && text !== null && (await textOf('#' + id)).trim() !== text.trim()) bad.push(`${id} text ${JSON.stringify(await textOf('#' + id))} not ${JSON.stringify(text)}`);
  }
  ok(ids.length >= 12 && !bad.length, `each of the tree's ${ids.length} elements with an id is in the frame with its text, and none not drawn is: ${bad.join('; ')}`);
  ok(await E(`!${F}.querySelector("#hidden") && !${F}.querySelector("#pic") && !${F}.querySelector("svg")`), 'display: none and the SVG are not written');
  ok(await E(`(function () { var s = ${F}.querySelector("#styled"), f = s.querySelector("font"); return !!f && f.getAttribute("color") === "red"
    && !!s.querySelector("b i u") && !s.getAttribute("style"); })()`), 'color, font-weight, font-style and text-decoration as FONT COLOR, B, I and U, and no style attribute');
  ok(await E(`(function () { var f = ${F}.querySelector("#title font"); return !!f && f.getAttribute("color") === "#800000"; })()`), "the heading's color as FONT COLOR");
  ok(await E(`(function () { var i = ${F}.querySelector("#inc"); return i.tagName === "INPUT" && i.type === "button" && i.value === "inc" && i.form === ${F}.forms[0]; })()`),
    'the button is an INPUT TYPE=button with its text as the value, in the one FORM');

  // A click on the button: delivered to C, and drawn once.
  let d = await draws();
  await click('#inc');
  ok((await stat(STAT.count)) === 1 && (await textOf('#countp')) === 'count 1', `a click on the button reached C and was drawn: count ${await stat(STAT.count)}, ${JSON.stringify(await textOf('#countp'))}`);
  ok((await draws()) === d + 1, `a click that changes the tree draws once: ${(await draws()) - d}`);
  await matches('after a click');

  // Clicks under one listener, with the path down to the target: two links, and a span's text in a link of its own.
  const paths = [];
  for (const sel of ['#one', '#two', '#three a']) { await click(sel); paths.push(await stat(STAT.path)); }
  ok(paths.join() === [1 << 16 | 0, 1 << 16 | 1, 1 << 16 | 2].join(), `links and a span's text under the listener reach it with depth 1 and paths 0, 1, 2: ${paths.map((p) => `${p >> 16}/${p & 0xffff}`)}`);
  ok(await E(`${F}.querySelector("#one").getAttribute("href").indexOf("javascript:parent.cdomF.k(") === 0`), 'a listened link is a javascript: URL into the top frame');

  // Focus, typing, and leaving the field: focus and blur reach C, and the change is an input and a change, drawn once,
  // later (cf_LATER), so the click that left the field is not lost.
  d = await draws();
  await click('#name');
  ok((await stat(STAT.focuses)) === 1, `focus in the name field reached C: ${await stat(STAT.focuses)}`);
  ok((await draws()) === d, `an event whose handler changes nothing draws nothing: ${(await draws()) - d}`);
  await E(`${field('name')}.select()`);
  await b.send('Input.insertText', { text: 'héllo' }); await sleep(30);
  d = await draws();
  await click('#inc');
  await sleep(400);
  ok((await stat(STAT.inputs)) === 1 && (await stat(STAT.changes)) === 1 && (await stat(STAT.blurs)) === 1,
    `leaving the field: one input, one change, one blur: ${await stat(STAT.inputs)}, ${await stat(STAT.changes)}, ${await stat(STAT.blurs)}`);
  ok((await textOf('#echop')) === 'echo héllo', `the input reached C, which read the value and drew it: ${JSON.stringify(await textOf('#echop'))}`);
  ok((await stat(STAT.count)) === 2, `the click that left the field still reached C: count ${await stat(STAT.count)}`);
  ok((await draws()) === d + 1, `a change and the click after it draw once: ${(await draws()) - d}`);
  // A change alone (the field left for nowhere): drawn once.
  await click('#name');
  await E(`${field('name')}.select()`);
  await b.send('Input.insertText', { text: 'abc' }); await sleep(30);
  d = await draws();
  await clickAt(600, 700);
  await sleep(400);
  ok((await textOf('#echop')) === 'echo abc' && (await draws()) === d + 1, `a change that C answers draws once: ${JSON.stringify(await textOf('#echop'))}, ${(await draws()) - d}`);

  // Typed text C has not seen survives a redraw, and so does the focus and the caret in it.
  await click('#note');
  await b.send('Input.insertText', { text: 'hello' }); await sleep(30);
  await E(`${field('note')}.setSelectionRange(2, 2)`);
  d = await draws();
  await E('X.touch()');
  ok((await draws()) === d + 1, `an export that changes the tree draws once: ${(await draws()) - d}`);
  ok((await E(`${field('note')}.value`)) === 'hello', `typed text C has not seen survives a redraw: ${JSON.stringify(await E(`${field('note')}.value`))}`);
  ok(await E(`${F}.activeElement === ${field('note')} && ${field('note')}.selectionStart === 2`), `the focus and the caret survive it: ${await E(`${F}.activeElement && ${F}.activeElement.name`)} at ${await E(`${field('note')}.selectionStart`)}`);
  await b.send('Input.insertText', { text: 'XY' }); await sleep(30);
  ok((await E(`${field('note')}.value`)) === 'heXYllo', `typing goes on at the caret: ${JSON.stringify(await E(`${field('note')}.value`))}`);
  ok((await E('X.note_len()')) === 7, `C reads the typed value from the field: ${await E('X.note_len()')} bytes`);
  d = await draws();
  await E('X.noop()');
  ok((await draws()) === d, `an export that changes nothing draws nothing: ${(await draws()) - d}`);
  // C sets a value and moves the focus.
  await E('X.set_note()');
  ok((await E(`${field('note')}.value`)) === 'from C' && (await E('X.note_len()')) === 6, `a value C sets is drawn and read back: ${JSON.stringify(await E(`${field('note')}.value`))}`);
  await E('X.focus_name()');
  ok(await E(`${F}.activeElement === ${field('name')}`), 'a focus C asks for is given');

  // A checkbox: its click is a change, with checked read in C.
  await click('#check');
  ok((await stat(STAT.checks)) === 2 && (await E(`${field('check')}.checked`)), `the checkbox's change reached C with checked 1: ${await stat(STAT.checks)}`);
  await click('#check');
  ok((await stat(STAT.checks)) === 21, `and again with checked 0: ${await stat(STAT.checks)}`);

  // A timer's callback draws.
  d = await draws();
  await E('X.tick_after(10)');
  ok(await until('X.stat(7) === 1') && (await textOf('#tickp')) === 'ticks 1', `a timer reached C and was drawn: ${JSON.stringify(await textOf('#tickp'))}`);
  ok((await draws()) === d + 1, `a timer draws once: ${(await draws()) - d}`);

  // Many ops in one call: still one draw.
  d = await draws();
  await E('X.grow(5)');
  ok((await draws()) === d + 1 && (await E(`${F}.querySelectorAll("#list li").length`)) === 5, `an export of 15 ops (5 items) draws once: ${(await draws()) - d}`);
  ok((await E('history.length')) === h0, `no redraw added a history entry: ${h0} then ${await E('history.length')}`);
  await matches('at the end');
  ok(!(await E('cdomF.errors.length')), `no errors noted: ${JSON.stringify(await E('cdomF.errors'))}`);
  ok(!errs().length, `no page exceptions: ${JSON.stringify(errs())}`);

  depthBefore = measure ? await E("window.__maxD") : 0;
  // A trap in C. The transformed file has no throw: C sets its flag and each function returns up the chain, the shim
  // notes it, and the call ends as any other, drawing what C did before it (docs/js1-transform-design.md, section 5).
  // The IE 9 build (FRAMES_APP=ie9) throws instead, out of a call that never comes back to the shim (no finally in
  // JavaScript 1.1), so nothing is drawn, and the next entry from the browser puts the counts back and draws.
  d = await draws();
  const was = await stat(STAT.count);
  let threw = '';
  try { await E('X.trap()'); } catch (e) { threw = String(e).slice(0, 80); }
  if (IE9_APP) {
    ok(threw && (await draws()) === d, `a trap is thrown out of the call, and nothing is drawn: ${threw ? 'threw' : 'no throw'}, ${(await draws()) - d} draws`);
    await click('#inc');
    ok((await textOf('#countp')) === `count ${was + 2}` && (await E('cdomF.errors.join()')).includes('did not return'),
      `the next click recovers: drawn with both counts (${JSON.stringify(await textOf('#countp'))}, want count ${was + 2}), and noted: ${JSON.stringify(await E('cdomF.errors'))}`);
  } else {
    ok(!threw && (await draws()) === d + 1 && (await textOf('#countp')) === `count ${was + 1}`,
      `a trap ends the call without a throw, and what C did before it is drawn: ${threw || 'no throw'}, ${(await draws()) - d} draws, ${JSON.stringify(await textOf('#countp'))}`);
    ok((await E('cdomF.errors.join()')) === 'cdom: C trapped in trap', `and it is noted, once: ${JSON.stringify(await E('cdomF.errors'))}`);
    // Five calls deep: no level goes on after the trap, and neither does the export.
    await E('X.trap_deep(5)');
    ok((await stat(STAT.count)) === was + 1, `a trap five calls deep stops every caller: count ${await stat(STAT.count)}, want ${was + 1}`);
    ok((await E('cdomF.errors.length')) === 2 && (await E('cdomF.errors[1]')) === 'cdom: C trapped in trap_deep', `noted: ${JSON.stringify(await E('cdomF.errors'))}`);
    await click('#inc');
    ok((await textOf('#countp')) === `count ${was + 2}` && (await E('cdomF.errors.length')) === 2,
      `the next click works as before: ${JSON.stringify(await textOf('#countp'))}, want count ${was + 2}, and nothing more noted`);
  }

  if (measure) maxDepth = await E('window.__maxD');
  if (measure) {
    // Redraw cost: a page grown to n nodes, X.touch() (one text changed) timed whole and cf_html() alone, 10 of each.
    const median = (a) => a.slice().sort((x, y) => x - y)[a.length >> 1];
    const nodes = 'cf_count(cf_mount)';
    await E('window.cf_count = function (x) { var n = 1, c; for (c = x.f; c; c = c.n) n += cf_count(c); return n; }');
    const rows = [];
    for (const want of [1000, 2000, 4000]) {
      const have = await E(nodes);
      if (have < want) await E(`X.grow(${Math.ceil((want - have) / 3)})`);
      // No performance.now under LEGACY=frames (IE 9 has none) and no Date.now (IE 6): the clock is new Date(), 1 ms
      // apart, so each sample is the mean of 10 in a row, and the median of 7 samples is kept.
      const n = await E(nodes), whole = [], ser = [];
      const mean10 = (call) => E(`(function () { var t = new Date().getTime(), i; for (i = 0; i < 10; i++) ${call}; return (new Date().getTime() - t) / 10; })()`);
      for (let i = 0; i < 7; i++) {
        whole.push(await mean10('X.touch()'));
        ser.push(await mean10('cf_html()'));
      }
      rows.push({ nodes: n, us_node: +(median(whole) * 1000 / n).toFixed(2), serialize_us_node: +(median(ser) * 1000 / n).toFixed(2), bytes: await E('cdomF.bytes') });
    }
    console.log('measure redraw', JSON.stringify(rows));
    const li = (rows[1].bytes - rows[0].bytes) / (rows[1].nodes - rows[0].nodes) * 3;
    console.log('measure bytes a list item (3 nodes)', li.toFixed(1), 'a node', (li / 3).toFixed(1));
    console.log('measure size cdom.frames.js', fs.statSync(path.join(root, 'cdom.frames.js')).size);
  }
} finally {
  if (!fails || measure) {
    // The fallback: a page that opens its BODY with a tag, where the FRAMESET written while it parses is ignored, gets
    // it written over the page once the page has loaded.
    await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/frames-body.html` });
    ok(await until('!!window.READY && window.cdomF && cdomF.draws > 0 && frames.length === 1'), 'frames-body.html (a BODY tag): booted, and drawn in the frameset written over the page');
    if (await E('frames.length === 1')) {
      await click('#inc');
      ok((await textOf('#countp')) === 'count 1', `and a click there reaches C: ${JSON.stringify(await textOf('#countp'))}`);
    }
    // The copy for the XP VM whose string table Netscape 3 parses (tools/js1table.mjs): the same page, the same run.
    await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/frames-ns.html` });
    ok(await until('!!window.READY && window.cdomF && cdomF.draws > 0') && (await E('cdomLits.length')) === 4,
      'frames-ns.html (the table as new Array() and assignments): booted and drawn');
    if (await E('frames.length === 1')) {
      await click('#inc');
      ok((await textOf('#countp')) === 'count 1', `and a click there reaches C: ${JSON.stringify(await textOf('#countp'))}`);
    }
    if (measure) {
      // History: what each way of rewriting the frame does to the top's history.length, 20 rewrites each, in a page
      // of its own (the same frameset, under LEGACY=frames).
      const ways = {
        'open()': 'd.open(); d.write(h); d.close();',
        'open("text/html")': 'd.open("text/html"); d.write(h); d.close();',
        'open("text/html", "replace")': 'd.open("text/html", "replace"); d.write(h); d.close();',
        'location.replace(javascript:)': 'w.location.replace("javascript:parent.__h");',
        'location.href = javascript:': 'w.location.href = "javascript:parent.__h";',
      };
      const out = {};
      for (const [k, how] of Object.entries(ways)) {
        await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/frames.html` });
        await until('!!window.READY && cdomF.draws > 0');
        const before = await E('history.length');
        for (let i = 0; i < 20; i++) {
          await E(`(function () { var w = frames[0], d = w.document, h = "<p>" + ${i} + "</p>"; window.__h = h; ${how} })()`);
          await sleep(40);
        }
        // What the frame shows after: the last rewrite's text, or the rewrite did not happen and the count says nothing.
        out[k] = [(await E('history.length')) - before, await E('frames[0].document.body ? frames[0].document.body.innerText : ""')];
      }
      console.log('measure history', JSON.stringify(out));
      // The JS 1 transform's measurements (docs/js1-transform-design.md, T4 and T2 and T3), for whichever app file this run
      // serves (FRAMES_APP=ie9: the IE 9 build): the boot of frames.html to its first draw, from the CDP side, polled every
      // 2 ms; a click on its button, called in the page; js1cpu's loop and recursions, timed in the page. The page's clock
      // is new Date() (1 ms), so each in-page sample is the mean of several in a row; the median of 7 samples is kept.
      const median = (a) => a.slice().sort((x, y) => x - y)[a.length >> 1];
      const boots = [];
      for (let i = 0; i < 7; i++) {
        const t0 = Date.now();
        await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/frames.html` });
        while (!(await E('!!window.READY && window.cdomF && cdomF.draws > 0')) && Date.now() - t0 < 20000) await sleep(2);
        boots.push(Date.now() - t0);
      }
      const clicks = [];
      for (let i = 0; i < 7; i++) {
        clicks.push(await E(`(function () { var t = new Date().getTime(), i; for (i = 0; i < 20; i++) ${F}.querySelector("#inc").onclick();
          return (new Date().getTime() - t) / 20; })()`));
      }
      const app = IE9_APP ? 'ie9' : 'js1';
      console.log(`measure ${app} boot ms ${median(boots)} (${boots}), click ms ${median(clicks)} (${clicks}), count ${await stat(STAT.count)}`);
      await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/js1cpu.html` });
      ok(await until('!!window.READY'), 'js1cpu.html booted');
      const inPage = (call, n) => E(`(function () { var t = new Date().getTime(), i; for (i = 0; i < ${n}; i++) ${call}; return (new Date().getTime() - t) / ${n}; })()`);
      const loops = [], recs = [];
      for (let i = 0; i < 7; i++) { loops.push(await inPage('X.loop(100000)', 50)); recs.push(await inPage('X.recs(1000, 20)', 50)); }
      console.log(`measure ${app} js1cpu loop(100000) ms ${median(loops)} (${loops}), recs(1000, 20) ms ${median(recs)} (${recs}), answers ${await E('X.loop(100000) >>> 0')} ${await E('X.recs(1000, 20) >>> 0')}`);
      console.log(`measure ${app} size ${fs.statSync(path.join(root, `frames.wasm.${app === 'ie9' ? 'ie9' : 'js1'}.js`)).size}`);
    }
  }
  if (measure && maxDepth) console.log(`measure ${IE9_APP ? 'ie9' : 'js1'} depth: ${depthBefore} JavaScript frames at most before the trap section, ${maxDepth} with it (trap_deep(5) recurses)`);
  await b.close(); srv.close();
}
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
