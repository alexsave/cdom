// Runs tests/out/host.html in headless Chrome: the page's own checks, then real input through CDP; exit 1 on any failure.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from './cdp.mjs';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
const b = await launch();
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const E = b.evaluate;
const log = (n) => E(`LOG.slice(${n}).map(r => r.join())`);
const mark = () => E('LOG.length');
// Waits until the log since n has an entry for which want(entry) holds; returns the entries since n.
async function until(n, want, ms = 3000) {
  let got = [];
  for (let t = 0; t < ms; t += 50) { got = (await E(`LOG.slice(${n})`)); if (got.some(want)) return got; await sleep(50); }
  return got;
}
const mouse = (type, x, y, extra = {}) => b.send('Input.dispatchMouseEvent', { type, x, y, button: 'none', ...extra });
async function click(x, y, button = 'left') {
  const buttons = button === 'left' ? 1 : 2;
  await mouse('mouseMoved', x, y);
  await mouse('mousePressed', x, y, { button, buttons, clickCount: 1 });
  await mouse('mouseReleased', x, y, { button, buttons: 0, clickCount: 1 });
}
try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/host.html` });
  let ready;
  for (let i = 0; i < 100 && !ready; i++) { await sleep(100); ready = await E('window.READY'); }
  if (ready !== true) throw new Error('page did not start: ' + ready);
  const id = {};
  for (const k of ['BOX', 'OTHER', 'TEXT', 'CHECK', 'MEDIA', 'GOOD', 'BAD', 'SCROLLER', 'FSBTN']) id[k] = await E(`H.id('${k}')`);

  // load and error arrive through the capture listener on the mount
  let got = await until(0, (r) => r[0] === 32);
  ok(got.some((r) => r[0] === 31 && r[1] === id.GOOD), 'img load delivered (capture)');
  ok(got.some((r) => r[0] === 32 && r[1] === id.BAD), 'img error delivered (capture)');

  const errs = await E('syncChecks()');
  for (const e of errs) ok(false, e);
  ok(errs.length === 0, `page checks (${errs.length} failures)`);
  const ferrs = await E('focusCheck()');
  for (const e of ferrs) ok(false, e);
  ok(ferrs.length === 0, 'focus() inside a batch: deferred focusin, batch applied once');

  // scroll through the capture listener, with scrollTop read in the handler
  let n = await mark();
  await E(`H.setN(${id.SCROLLER}, 'scrollTop', 40)`);
  got = await until(n, (r) => r[0] === 30);
  ok(got.some((r) => r.join() === `30,${id.SCROLLER},40,0`), 'scroll delivered with scrollTop 40: ' + got.map((r) => r.join()).join(' '));

  // media: duration on loadedmetadata, then volume, currentTime, muted, play, timeupdate, ended
  n = await mark();
  await E(`H.setS(${id.MEDIA}, 'src', wav())`);
  got = await until(n, (r) => r[0] === 33);
  ok(got.some((r) => r[0] === 33 && r[1] === id.MEDIA && Math.abs(r[2] - 0.25) < 1e-6), 'loadedmetadata with duration 0.25: ' + got.map((r) => r.join()).join(' '));
  await E(`H.setN(${id.MEDIA}, 'volume', 0.25)`);
  ok(await E(`H.num(${id.MEDIA}, 'volume')`) === 0.25, 'media volume set and read');
  await E(`H.setN(${id.MEDIA}, 'currentTime', 0.1)`);
  ok(Math.abs(await E(`H.num(${id.MEDIA}, 'currentTime')`) - 0.1) < 0.01, 'media currentTime set and read');

  // pointer capture: down on the box captures, so moves and the up over the other box still reach the box
  await mouse('mouseMoved', 350, 60);
  n = await mark();
  await mouse('mouseMoved', 352, 62);
  ok((await log(n)).some((r) => r.startsWith(`11,${id.OTHER},`)), 'without capture a move over the other box reaches it');
  n = await mark();
  await mouse('mouseMoved', 60, 60);
  await mouse('mousePressed', 60, 60, { button: 'left', buttons: 1, clickCount: 1 });
  await mouse('mouseMoved', 200, 60, { button: 'left', buttons: 1 }); // a drag reports its button, as a real one does
  await mouse('mouseMoved', 350, 60, { button: 'left', buttons: 1 });
  await mouse('mouseReleased', 350, 60, { button: 'left', buttons: 0, clickCount: 1 });
  got = await log(n);
  ok(got.includes(`23,${id.BOX},0,0`), 'pointerover delivered');
  const down = got.find((r) => r.startsWith(`9,${id.BOX},`));
  ok(down && down.endsWith(',1') && +down.split(',')[2] >= 1, 'pointerdown with pointer id and buttons: ' + down);
  ok(got.includes(`11,${id.BOX},350,1`) && !got.some((r) => r.startsWith(`11,${id.OTHER},`)), 'captured move over the other box reaches the box: ' + got.join(' '));
  ok(got.includes(`10,${id.BOX},350,0`) && !got.some((r) => r.startsWith(`10,${id.OTHER},`)), 'captured up reaches the box');

  // contextmenu prevented; the next event starts with a clear flag
  n = await mark();
  await click(60, 60, 'right');
  got = await until(n, (r) => r[0] === 21);
  ok(got.some((r) => r[0] === 21 && r[1] === id.BOX), 'contextmenu delivered');
  ok(await E('page.contextmenu') === true, 'contextmenu defaultPrevented');
  await click(60, 60);
  ok((await log(n)).includes(`0,${id.BOX},0,0`) && await E('page.click') === false, 'the next click is not prevented');

  // wheel with deltaY in the record and through CD_EVENT, prevented
  n = await mark();
  await mouse('mouseWheel', 60, 60, { deltaX: 0, deltaY: 120 });
  got = await until(n, (r) => r[0] === 22);
  ok(got.some((r) => r.join() === `22,${id.BOX},120,120`), 'wheel deltaY 120: ' + got.map((r) => r.join()).join(' '));
  ok(await E('page.wheel') === true, 'wheel defaultPrevented');

  // play needs the user activation the clicks above gave the page; CD_RESULT holds play()'s promise
  n = await mark();
  await E('X.c_play()');
  ok(await E(`H.num(${id.MEDIA}, 'muted')`) === 1, 'media muted set');
  ok(await E('cdom.node(5).then(() => "resolved", (e) => String(e))') === 'resolved', 'play() promise kept as CD_RESULT');
  got = await until(n, (r) => r[0] === 35);
  for (const [t, name] of [[36, 'play'], [34, 'timeupdate'], [35, 'ended']]) ok(got.some((r) => r[0] === t && r[1] === id.MEDIA), `media ${name} delivered (capture)`);

  // keydown and keyup at document level with the focus on body
  await E('document.activeElement && document.activeElement.blur()');
  ok(await E('document.activeElement === document.body'), 'focus on body');
  n = await mark();
  await b.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'q', code: 'KeyQ', windowsVirtualKeyCode: 81, text: 'q' });
  await b.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'q', code: 'KeyQ', windowsVirtualKeyCode: 81 });
  got = await log(n);
  ok(got.includes('4,2,81,0') && got.includes('5,2,81,0'), 'keydown and keyup on CD_DOCUMENT: ' + got.join(' '));
  ok(await E('STRS.at(-1)') === 'q', 'key string through CD_EVENT');

  // input.value after the user typed: the property path works where the attribute path does not
  await click(100, 160);
  await b.send('Input.insertText', { text: 'typed' });
  ok((await E(`H.el('TEXT').value`)).endsWith('typed'), 'user typed into the input');
  await E(`H.el('TEXT').setAttribute('value', 'by attribute')`);
  ok(await E(`H.el('TEXT').value`) !== 'by attribute', 'control: setAttribute does not change an edited value');
  await E(`H.setS(${id.TEXT}, 'value', 'reset')`);
  ok(await E(`H.el('TEXT').value`) === 'reset', 'SET value after a user edit');
  await click(255, 157);
  ok(await E(`H.el('CHECK').checked`) === true, 'user checked the box');
  await E(`H.el('CHECK').removeAttribute('checked')`);
  ok(await E(`H.el('CHECK').checked`) === true, 'control: the attribute does not change a clicked box');
  await E(`H.setB(${id.CHECK}, 'checked', 0)`);
  ok(await E(`H.el('CHECK').checked`) === false && await E(`H.num(${id.CHECK}, 'checked')`) === 0, 'SET checked false after a user click');

  // window resize through the reserved id, with innerWidth and devicePixelRatio read from C
  n = await mark();
  await b.send('Emulation.setDeviceMetricsOverride', { width: 900, height: 600, deviceScaleFactor: 2, mobile: false });
  got = await until(n, (r) => r[0] === 12 && r[2] === 900);
  ok(got.some((r) => r.join() === '12,1,900,600'), 'resize on CD_WINDOW with innerWidth 900, innerHeight 600: ' + got.map((r) => r.join()).join(' '));
  ok(await E('H.num(1, "devicePixelRatio")') === 2, 'devicePixelRatio 2 read through CD_WINDOW');
  ok(got.filter((r) => r[0] === 12).length >= 1 && new Set(got.filter((r) => r[0] === 12).map(String)).size === got.filter((r) => r[0] === 12).length, 'resize delivered once per change (listening twice adds no listener)');
  await b.send('Emulation.clearDeviceMetricsOverride');
  await sleep(200);

  // fullscreen: a real click requests it with an options object; the handler exits through CD_DOCUMENT
  n = await mark();
  await click(50, 215);
  got = await until(n, (r) => r[0] === 38 && r[2] === -1, 4000);
  const fs1 = got.filter((r) => r[0] === 38);
  if (!fs1.length) console.log('SKIP fullscreen: this headless Chrome never entered fullscreen (no fullscreenchange in 4 s)');
  else {
    ok(fs1[0].join() === `38,2,${id.FSBTN},0`, 'fullscreenchange on CD_DOCUMENT with fullscreenElement = the button');
    ok(fs1.length >= 2 && fs1[1].join() === '38,2,-1,0', 'exitFullscreen through CD_DOCUMENT, fullscreenElement null');
  }

  // visibilitychange: a second tab in front hides this one
  n = await mark();
  const { result: { targetId: other } } = await b.sendBrowser('Target.createTarget', { url: 'about:blank' });
  await b.sendBrowser('Target.activateTarget', { targetId: other });
  got = await until(n, (r) => r[0] === 13, 2000);
  let vis = await E('STRS.slice(-2)');
  if (!got.some((r) => r[0] === 13)) {
    console.log('note: activating another tab did not hide this one; dispatching visibilitychange on document');
    await E(`document.dispatchEvent(new Event('visibilitychange'))`);
    got = await log(n).then((a) => a.map((s) => s.split(',').map(Number)));
    vis = await E('STRS.slice(-1)');
  }
  ok(got.some((r) => r[0] === 13 && r[1] === 2), 'visibilitychange on CD_DOCUMENT, visibilityState read: ' + vis.join());
  await b.sendBrowser('Target.closeTarget', { targetId: other });
  await b.sendBrowser('Target.activateTarget', { targetId: b.targetId });
} catch (e) { ok(false, String(e.stack || e)); }
finally { await b.close(); srv.close(); }
console.log(fails ? `${fails} FAILED` : 'host: all ok');
process.exit(fails ? 1 : 0);
