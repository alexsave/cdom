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
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => {
  const f = path.join(root, q.url.split(/[?#]/)[0]);
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
// hold: how long the button is down, in ms.
async function clickAt(x, y, hold = 30) {
  if (x < 0) return;
  await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y, 1, 'left'); await sleep(hold);
  await mouse('mouseReleased', x, y, 0, 'left'); await sleep(60);
}
const click = async (sel, hold) => clickAt(...await centre(sel), hold);
const draws = () => E('cdomF.draws');
const stat = (k) => E(`X.stat(${k})`);
const STAT = { count: 0, path: 1, inputs: 2, changes: 3, checks: 4, focuses: 5, blurs: 6, ticks: 7, items: 8, radios: 9 };
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
    // A field's text is its value, not text the frame shows: checked on its own.
    if (id) out.push([id, hidden, x.t === "input" || x.t === "button" || x.t === "textarea" || x.t === "select" ? null : cf_textOf(x)]);
    for (c = x.f; c; c = c.n) walk(c, hidden);
  }
  walk(cf_mount, false); return out; })()`;

const port = srv.address().port;
const measure = !!process.env.FRAMES_MEASURE;
run: try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/frames.html` });
  ok(await until('!!window.READY && window.cdomF && cdomF.draws > 0'), 'booted, and the frame drawn');
  if (!(await E('!!window.READY'))) break run;
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
  // Markup characters in a text, an attribute, a textarea's value and an option's text: each is read back from the
  // frame's own DOM, so markup the shim did not escape would show as elements or cut values, not as these characters.
  const ESC = `<b>"&amp;x</b>'`;
  const esc = await E(`(function () { var p = ${F}.querySelector("#esc"), t = ${F}.querySelector("#escta"), s = ${F}.querySelector("#escsel");
    return [p ? p.innerText : null, p ? p.getAttribute("title") : null, p ? p.getElementsByTagName("b").length : -1, t ? t.value : null,
      s && s.options[0] ? s.options[0].text : null]; })()`);
  ok(esc[0] === ESC && esc[2] === 0, `a text with markup characters reads back as them: ${JSON.stringify(esc[0])}, ${esc[2]} B elements`);
  ok(esc[1] === ESC, `an attribute value with markup characters reads back as them: ${JSON.stringify(esc[1])}`);
  ok(esc[3] === ESC, `a textarea value with markup characters reads back as them: ${JSON.stringify(esc[3])}`);
  ok(esc[4] === ESC, `an option text with markup characters reads back as them: ${JSON.stringify(esc[4])}`);

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
  await until(`cdomF.draws > ${d}`);
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
  await until(`cdomF.draws > ${d}`);
  ok((await textOf('#echop')) === 'echo abc' && (await draws()) === d + 1, `a change that C answers draws once: ${JSON.stringify(await textOf('#echop'))}, ${(await draws()) - d}`);
  // A slow click that leaves the field: the press held 600 ms, past cf_LATER, while the change and blur wait to be
  // drawn. The draw waits for the button's release, so the button under the pointer is still there for the click.
  await click('#name');
  await E(`${field('name')}.select()`);
  await b.send('Input.insertText', { text: 'slow' }); await sleep(30);
  d = await draws();
  const before = await stat(STAT.count);
  await click('#inc', 600);
  await until(`cdomF.draws > ${d}`);
  ok((await stat(STAT.count)) === before + 1 && (await textOf('#echop')) === 'echo slow' && (await draws()) === d + 1,
    `a click held 600 ms that left a field reaches C, drawn once: count ${(await stat(STAT.count)) - before}, ${JSON.stringify(await textOf('#echop'))}, ${(await draws()) - d} draws`);

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

  // Two radios of one group (name grp): the second's click unchecks the first, in the frame and as C reads them.
  await click('#r1');
  await click('#r2');
  ok((await stat(STAT.radios)) === 12 && (await E(`${field('r2')}.checked && !${field('r1')}.checked && ${field('r1')}.name === "grp" && ${field('r2')}.name === "grp"`)),
    `two radios of one name are one group: C read ${await stat(STAT.radios)} (want 1 then 2: 12), r1 ${await E(`${field('r1')}.checked`)}, r2 ${await E(`${field('r2')}.checked`)}`);

  // A form element from C is a DIV in the shim's one FORM: the fields in it and after it are that FORM's elements, in
  // order, and read back as such.
  ok(await E(`(function () { var d = ${F}, f = d.forms[0], i = d.querySelector("#inform"), a = d.querySelector("#afterform");
    return d.forms.length === 1 && !!i && !!a && i.form === f && a.form === f && ${field('inform')} === i && ${field('afterform')} === a
      && d.querySelector("#appform").tagName === "DIV"; })()`), 'a form from C is a DIV: the fields in it and after it stay in the one FORM, in order');
  await E(`${field('afterform')}.value = "typed after"`);
  await E('X.touch()');
  ok((await E(`${field('afterform')}.value`)) === 'typed after', `a field after the form is read back across a redraw: ${JSON.stringify(await E(`${field('afterform')}.value`))}`);

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

  // A trap in C: thrown out of the call, which never comes back to the shim (no finally in JavaScript 1.1), so nothing
  // is drawn; the next entry from the browser puts the counts back, notes it, and draws what the call had done.
  d = await draws();
  const was = await stat(STAT.count);
  let threw = '';
  try { await E('X.trap()'); } catch (e) { threw = String(e).slice(0, 80); }
  ok(threw && (await draws()) === d, `a trap is thrown out of the call, and nothing is drawn: ${threw ? 'threw' : 'no throw'}, ${(await draws()) - d} draws`);
  await click('#inc');
  ok((await textOf('#countp')) === `count ${was + 2}` && (await E('cdomF.errors.join()')).includes('did not return'),
    `the next click recovers: drawn with both counts (${JSON.stringify(await textOf('#countp'))}, want count ${was + 2}), and noted: ${JSON.stringify(await E('cdomF.errors'))}`);

  // A throw in the shim's own draw must not leave the handlers quiet (cf_drawing) for good. select on a checkbox gives
  // the focus back with no selection set, so it does not throw at all; a focus that throws while the shim gives it back
  // (planted once on the frame's inputs) leaves cf_drawing up, which the draw's settle timer puts down and notes.
  let selectThrew = '';
  try { await E('X.select_check()'); } catch (e) { selectThrew = String(e).slice(0, 120); }
  let n0 = await stat(STAT.count);
  await click('#inc');
  ok(!selectThrew && (await stat(STAT.count)) === n0 + 1, `cd_call(checkbox, "select") does not throw, and the next click reaches C: ${selectThrew || 'no throw'}, count ${(await stat(STAT.count)) - n0}`);
  await E('(function () { var P = frames[0].HTMLInputElement.prototype, f = P.focus; P.focus = function () { P.focus = f; throw new Error("planted"); }; })()');
  let focusThrew = '';
  try { await E('X.focus_name()'); } catch (e) { focusThrew = String(e).slice(0, 120); }
  await until('cdomF.errors.join().indexOf("a draw did not finish") >= 0');
  n0 = await stat(STAT.count);
  await click('#inc');
  ok(focusThrew && (await stat(STAT.count)) === n0 + 1 && (await E('cdomF.errors.join()')).includes('a draw did not finish'),
    `a throw while the shim draws does not lock the page: ${focusThrew ? 'threw' : 'no throw'}, the next click reached C (${(await stat(STAT.count)) - n0}), noted: ${JSON.stringify(await E('cdomF.errors'))}`);

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
    // The caret on IE 4 and Netscape 4, the browsers with onkeyup (tools/cdp.mjs's ?ie4 and ?ns4 flavours): typing in
    // the middle of the name field, whose input C answers by changing the echo. IE 4 reads the caret from
    // document.selection and redraws on each key; Netscape 4 cannot read it, and draws once the field is left.
    const key = (type, k, code, vk, text) => b.send('Input.dispatchKeyEvent', { type, key: k, code, windowsVirtualKeyCode: vk, text });
    const press = async (k, code, vk, text) => { await key(text ? 'keyDown' : 'rawKeyDown', k, code, vk, text); await key('keyUp', k, code, vk); await sleep(40); };
    for (const flavour of ['ie4', 'ns4']) {
      await b.send('Page.navigate', { url: `http://127.0.0.1:${port}/frames.html?${flavour}` });
      if (!(await until('!!window.READY && window.cdomF && cdomF.draws > 0'))) { ok(false, `${flavour}: booted`); continue; }
      const el = field('name');
      ok(await E(`typeof ${el}.selectionStart === "undefined" && !${el}.setSelectionRange && ${flavour === 'ie4' ? '' : '!'}(!!${el}.createTextRange && !!${F}.selection)`),
        `${flavour}: the field has no selectionStart${flavour === 'ie4' ? ', and has createTextRange and document.selection' : ', createTextRange or document.selection'}`);
      await click('#name');
      await press('Home', 'Home', 36); await press('ArrowRight', 'ArrowRight', 39); await press('ArrowRight', 'ArrowRight', 39);
      const d = await draws();
      await press('X', 'KeyX', 88, 'X');
      if (flavour === 'ie4') await until(`cdomF.draws > ${d}`);
      await press('Y', 'KeyY', 89, 'Y');
      if (flavour === 'ie4') await until(`cdomF.draws > ${d + 1}`);
      const typed = await E(`${field('name')}.value`), drew = (await draws()) - d;
      ok(typed === 'stXYart', `${flavour}: typing in the middle of a field C answers goes on at the caret: ${JSON.stringify(typed)} (${drew} draws)`);
      if (flavour === 'ie4') ok(drew === 2 && (await textOf('#echop')) === 'echo stXYart', `ie4: a draw a key, with the echo: ${drew}, ${JSON.stringify(await textOf('#echop'))}`);
      else {
        ok(drew === 0, `ns4: no draw while a key is typed in the focused field: ${drew}`);
        await clickAt(600, 700);
        await until(`cdomF.draws > ${d}`);
        ok((await textOf('#echop')) === 'echo stXYart', `ns4: drawn once the field is left: ${JSON.stringify(await textOf('#echop'))}`);
      }
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
    }
  }
  await b.close(); srv.close();
}
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
