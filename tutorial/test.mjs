// The tutorial's end-to-end test: opens each lesson's built page from disk (file://, as a reader would
// after make) in headless Chrome, drives it the way a user does (real mouse clicks and typed text through
// the DevTools input events, never element.click()), and checks what is on screen. Every lesson also
// fails on any page error, and on anything fetched (the module is bundled into the page).
// Usage: node tutorial/test.mjs [lesson-dir ...]   (default: every lesson); tutorial/run.sh builds first.
// SHOTS=dir writes a screenshot of each lesson's final state there.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { launch, sleep } from '../tools/cdp.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
let bad = 0;
const ok = (c, m) => { console.log(c ? 'ok  ' : 'FAIL', m); if (!c) bad++; };

// What each lesson must do. t is the page driver below; each check reads the page after a real input.
const LESSONS = {
  '01-hello': async (t) => {
    ok(await t.text('h1') === 'Hello, cdom', 'the heading says "Hello, cdom"');
    ok(JSON.stringify(await t.texts('li')) === '["apple","banana","cherry"]', 'the list shows the three fruits, in order');
    ok(await t.eval(`document.querySelector('a').getAttribute('href')`) === 'https://github.com/alexsave/cdom', 'the link points at the repo');
  },
  '02-counter': async (t) => {
    ok(await t.text('output') === '0', 'the count starts at 0');
    await t.click('button[aria-label=increment]');
    await t.click('button[aria-label=increment]');
    await t.click('button[aria-label=increment]');
    ok(await t.text('output') === '3', `three clicks on + make 3 (${await t.text('output')})`);
    for (let i = 0; i < 5; i++) await t.click('button[aria-label=decrement]');
    ok(await t.text('output') === '-2', `five clicks on - make -2 (${await t.text('output')})`);
    await t.clickText('button', 'reset');
    ok(await t.text('output') === '0', 'reset makes 0');
  },
};

// The page driver: one tab, the page's errors collected, clicks and keys as the browser's own input events.
function driver(b, errors) {
  const t = {
    eval: (expr) => b.evaluate(expr),
    text: (sel) => b.evaluate(`document.querySelector(${JSON.stringify(sel)})?.textContent ?? null`),
    texts: (sel) => b.evaluate(`[...document.querySelectorAll(${JSON.stringify(sel)})].map(e => e.textContent)`),
    count: (sel) => b.evaluate(`document.querySelectorAll(${JSON.stringify(sel)}).length`),
    // The center of the first element that matches (and, with text, whose text is exactly that), in the viewport.
    center: async (sel, text) => {
      const p = await b.evaluate(`(() => {
        const all = [...document.querySelectorAll(${JSON.stringify(sel)})];
        const e = ${text === undefined ? 'all[0]' : `all.find(e => e.textContent.trim() === ${JSON.stringify(text)})`};
        if (!e) return null;
        e.scrollIntoView({ block: 'center' });
        const r = e.getBoundingClientRect();
        return [r.left + r.width / 2, r.top + r.height / 2];
      })()`);
      if (!p) throw new Error(`no element ${sel}${text === undefined ? '' : ` with text "${text}"`}`);
      return p;
    },
    clickAt: async ([x, y]) => {
      const base = { x, y, button: 'left', clickCount: 1 };
      await b.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
      await b.send('Input.dispatchMouseEvent', { type: 'mousePressed', ...base, buttons: 1 });
      await b.send('Input.dispatchMouseEvent', { type: 'mouseReleased', ...base, buttons: 0 });
      await t.settle();
    },
    click: async (sel) => t.clickAt(await t.center(sel)),
    clickText: async (sel, text) => t.clickAt(await t.center(sel, text)),
    // Click into a field, then type: each character is a key event and an input event, as from a keyboard.
    type: async (sel, s) => {
      await t.click(sel);
      for (const ch of s) {
        await b.send('Input.dispatchKeyEvent', { type: 'keyDown', key: ch, text: ch, unmodifiedText: ch });
        await b.send('Input.dispatchKeyEvent', { type: 'keyUp', key: ch });
      }
      await t.settle();
    },
    // A named key (Enter, Escape, Backspace, ArrowUp...) to whatever has focus.
    key: async (key) => {
      const codes = { Enter: 13, Escape: 27, Backspace: 8, Tab: 9, ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39, ' ': 32 };
      const k = { key, code: key === ' ' ? 'Space' : key, windowsVirtualKeyCode: codes[key], nativeVirtualKeyCode: codes[key] };
      const text = key === 'Enter' ? '\r' : key === ' ' ? ' ' : undefined;
      await b.send('Input.dispatchKeyEvent', { type: 'keyDown', ...k, ...(text ? { text } : {}) });
      await b.send('Input.dispatchKeyEvent', { type: 'keyUp', ...k });
      await t.settle();
    },
    // Events reach C from the event loop, and a focus change waits a microtask: two frames covers both.
    settle: () => b.evaluate('new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r(1))))'),
    sleep,
    // Polls until fn() is truthy, up to ms; returns its last value.
    until: async (fn, ms = 3000) => {
      const end = Date.now() + ms;
      let v;
      while (!(v = await fn()) && Date.now() < end) await sleep(25);
      return v;
    },
    errors,
  };
  return t;
}

const names = process.argv.slice(2).length ? process.argv.slice(2).map((a) => path.basename(path.resolve(a))) : Object.keys(LESSONS);
const b = await launch();
const errors = [];
b.onEvent = (e) => {
  if (e.method === 'Runtime.exceptionThrown') errors.push(e.params.exceptionDetails.exception?.description || e.params.exceptionDetails.text);
  if (e.method === 'Runtime.consoleAPICalled' && (e.params.type === 'error' || e.params.type === 'warning'))
    errors.push(e.params.args.map((a) => a.value ?? a.description).join(' '));
};
try {
  for (const name of names) {
    const check = LESSONS[name];
    if (!check) { ok(false, `${name}: no checks in tutorial/test.mjs`); continue; }
    const page = path.join(here, name, 'build', 'index.html');
    if (!fs.existsSync(page)) { ok(false, `${name}: ${page} is not built (run tutorial/run.sh)`); continue; }
    console.log(`-- ${name}`);
    errors.length = 0;
    await b.send('Page.navigate', { url: pathToFileURL(page).href });
    const t = driver(b, errors);
    const up = await t.until(() => b.evaluate(`document.querySelector('#app')?.childElementCount > 0`));
    ok(up, `${name}: the app built its page`);
    if (up) {
      try { await check(t); } catch (e) { ok(false, `${name}: ${e.message}`); }
    }
    const fetched = await b.evaluate(`performance.getEntriesByType('resource').map(e => e.name)`);
    ok(fetched.length === 0, `${name}: nothing was fetched but the page (${JSON.stringify(fetched)})`);
    ok(errors.length === 0, `${name}: no error on the page${errors.length ? `: ${errors.join(' | ')}` : ''}`);
    if (process.env.SHOTS) {
      fs.mkdirSync(process.env.SHOTS, { recursive: true });
      const { result } = await b.send('Page.captureScreenshot', { format: 'png' });
      fs.writeFileSync(path.join(process.env.SHOTS, `${name}.png`), Buffer.from(result.data, 'base64'));
    }
  }
} finally {
  await b.close();
}
console.log(bad ? `tutorial: ${bad} FAILED` : 'tutorial: all ok');
process.exit(bad ? 1 : 0);
