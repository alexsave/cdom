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
  '03-input': async (t) => {
    ok(await t.text('.greeting') === 'Hello, stranger!', 'the greeting starts as "Hello, stranger!"');
    ok(await t.text('.count') === '0 characters', `the count starts at "0 characters" (${await t.text('.count')})`);
    await t.type('#name', 'Ada');
    ok(await t.text('.greeting') === 'Hello, Ada!', `typing Ada greets Ada (${await t.text('.greeting')})`);
    ok(await t.text('.count') === '3 characters', `Ada is 3 characters (${await t.text('.count')})`);
    for (let i = 0; i < 3; i++) await t.key('Backspace');
    ok(await t.text('.greeting') === 'Hello, stranger!', 'emptying the field greets a stranger again');
    ok(await t.text('.count') === '0 characters', 'an empty field is 0 characters');
    await t.type('#name', 'Zoë');
    ok(await t.text('.greeting') === 'Hello, Zoë!', `the e with a diaeresis is kept (${await t.text('.greeting')})`);
    ok(await t.text('.count') === '3 characters', `Zoë is 3 characters, not 4 bytes (${await t.text('.count')})`);
    for (let i = 0; i < 3; i++) await t.key('Backspace');
    await t.type('#name', 'Ada Lovelace');
    ok(await t.text('.greeting') === 'Hello, Ada Lovelace!', 'the name in the field is greeted');
    ok(await t.text('.count') === '12 characters', `Ada Lovelace is 12 characters (${await t.text('.count')})`);
  },
  '04-regions': async (t) => {
    const expanded = () => t.eval(`document.querySelector('.toggle').getAttribute('aria-expanded')`);
    const selected = () => t.eval(`[...document.querySelectorAll('[role=tab]')].filter(e => e.getAttribute('aria-selected') === 'true').map(e => e.textContent)`);
    // The disclosure: the section is not in the page until the button builds it.
    ok(await t.count('#details') === 0, 'the details are not in the page at first');
    ok(await t.text('.toggle') === 'Show details', 'the button says "Show details" at first');
    ok(await expanded() === 'false', 'aria-expanded is "false" at first');
    const before = await t.eval(`document.querySelectorAll('#app *').length`);
    await t.click('.toggle');
    ok(await t.count('#details') === 1, 'a click builds the details');
    ok(await t.text('.toggle') === 'Hide details', `the button now says "Hide details" (${await t.text('.toggle')})`);
    ok(await expanded() === 'true', 'aria-expanded is "true" while open');
    ok(await t.text('#details h2') === 'What a region is', 'the details have their heading');
    ok(await t.eval(`document.querySelector('.toggle').nextElementSibling?.id`) === 'details', 'the section is right after the button');
    await t.click('.toggle');
    ok(await t.count('#details') === 0, 'a second click takes the details away');
    ok(await t.text('.toggle') === 'Show details', 'the button says "Show details" again');
    ok(await expanded() === 'false', 'aria-expanded is "false" again');
    const after = await t.eval(`document.querySelectorAll('#app *').length`);
    ok(after === before, `closing leaves nothing behind (${after} elements, was ${before})`);
    await t.click('.toggle');
    ok(await t.count('#details') === 1, 'a third click builds the details again');
    // The tabs: each click rebuilds the panel, and the current tab does nothing.
    ok(await t.text('.panel h2') === 'Overview', 'the Overview panel is shown at first');
    ok(JSON.stringify(await selected()) === '["Overview"]', 'only Overview is selected at first');
    await t.clickText('[role=tab]', 'Usage');
    ok(await t.text('.panel h2') === 'Usage', `a click on Usage shows the Usage panel (${await t.text('.panel h2')})`);
    ok(await t.count('.panel code') === 2, 'the Usage panel has its two code elements');
    ok(JSON.stringify(await selected()) === '["Usage"]', 'only Usage is selected');
    ok(JSON.stringify(await t.texts('[role=tab].active')) === '["Usage"]', 'Usage is the active tab');
    await t.clickText('[role=tab]', 'Notes');
    ok(await t.text('.panel h2') === 'Notes', 'a click on Notes shows the Notes panel');
    ok(await t.count('.panel code') === 0, 'the Usage content is gone from the panel');
    await t.eval(`window.__notesHeading = document.querySelector('.panel h2'), true`);
    await t.clickText('[role=tab]', 'Notes');
    ok(await t.eval(`document.querySelector('.panel h2') === window.__notesHeading`), 'a click on the current tab leaves its panel as it was');
    ok(JSON.stringify(await selected()) === '["Notes"]', 'Notes is still the only selected tab');
    await t.clickText('[role=tab]', 'Usage');
    ok(await t.text('.panel h2') === 'Usage', 'the page ends on Usage, with the details open');
  },
  '05-todo': async (t) => {
    const texts = () => t.texts('.items .text');
    const rows = () => t.eval(`[...document.querySelectorAll('.items li')].map(li => li.className)`);
    const pressed = () => t.eval(`[...document.querySelectorAll('.filters button')].map(b => b.textContent + ':' + b.getAttribute('aria-pressed'))`);
    const add = async (s) => { await t.type('.new', s); await t.key('Enter'); };
    ok(await t.count('.items li') === 0, 'the list starts with no items');
    ok(await t.text('.left') === '0 items left', `the footer starts at "0 items left" (${await t.text('.left')})`);
    ok(JSON.stringify(await pressed()) === '["All:true","Active:false","Done:false"]', 'All is the filter at first');
    await add('Buy milk');
    ok(JSON.stringify(await texts()) === '["Buy milk"]', 'Enter adds "Buy milk"');
    ok(await t.eval(`document.querySelector('.new').value`) === '', 'the field is empty after an add');
    ok(await t.eval(`document.activeElement === document.querySelector('.new')`), 'the field keeps the focus after Enter');
    await add('Walk dog');
    await add('Write C');
    ok(JSON.stringify(await texts()) === '["Buy milk","Walk dog","Write C"]', 'three items, in the order they were added');
    ok(await t.text('.left') === '3 items left', `three open items are "3 items left" (${await t.text('.left')})`);
    // The rows' elements, kept on window: a row that is patched is the same element afterwards.
    await t.eval(`window.__walk = document.querySelectorAll('.items li')[1], window.__writeC = document.querySelectorAll('.items li')[2], true`);
    await t.click('.items li:nth-child(2) input[type=checkbox]');
    ok(JSON.stringify(await rows()) === '["item","item done","item"]', 'checking "Walk dog" gives its row the done class');
    ok(await t.eval(`document.querySelectorAll('.items li')[1].querySelector('input').checked`), 'and its checkbox is checked');
    ok(await t.text('.left') === '2 items left', `two open items are "2 items left" (${await t.text('.left')})`);
    ok(await t.eval(`document.querySelectorAll('.items li')[1] === window.__walk`), 'the Walk dog row is patched in place, not built again');
    ok(await t.eval(`document.querySelectorAll('.items li')[2] === window.__writeC`), 'the Write C row is the same element after another row changed');
    await t.clickText('.filters button', 'Active');
    ok(JSON.stringify(await texts()) === '["Buy milk","Write C"]', 'Active shows the open items, in order');
    ok(JSON.stringify(await pressed()) === '["All:false","Active:true","Done:false"]', 'Active is the pressed filter');
    await t.clickText('.filters button', 'Done');
    ok(JSON.stringify(await texts()) === '["Walk dog"]', 'Done shows only the checked item');
    await t.clickText('.filters button', 'All');
    ok(JSON.stringify(await texts()) === '["Buy milk","Walk dog","Write C"]', 'All shows every item again, in the original order');
    // Done took the Write C row away and All built it again, so the element is taken again from the page.
    await t.eval(`window.__writeC = document.querySelectorAll('.items li')[2], true`);
    await t.click('.items li:nth-child(1) .remove');
    ok(JSON.stringify(await texts()) === '["Walk dog","Write C"]', 'removing "Buy milk" leaves the other two, in order');
    ok(await t.text('.left') === '1 item left', `one open item is "1 item left" (${await t.text('.left')})`);
    ok(await t.eval(`document.querySelectorAll('.items li')[1] === window.__writeC`), 'removing a row leaves the Write C row as the same element');
    await t.click('.clear');
    ok(JSON.stringify(await texts()) === '["Write C"]', 'Clear done leaves only "Write C"');
    await t.type('.new', '   ');
    await t.key('Enter');
    ok(JSON.stringify(await texts()) === '["Write C"]', 'a field of spaces adds nothing');
    for (let i = 0; i < 3; i++) await t.key('Backspace');
    await t.clickText('.add button', 'Add');
    ok(JSON.stringify(await texts()) === '["Write C"]', 'Add with an empty field adds nothing');
    await t.type('.new', 'Ship it');
    await t.clickText('.add button', 'Add');
    ok(JSON.stringify(await texts()) === '["Write C","Ship it"]', 'a click on Add adds the todo');
    ok(await t.eval(`document.querySelector('.new').value`) === '', 'the field is empty after a click on Add');
    ok(await t.eval(`document.activeElement === document.querySelector('.new')`), 'the focus goes back to the field after a click on Add');
    await add('Read the cdom tutorial');
    ok(JSON.stringify(await texts()) === '["Write C","Ship it","Read the cdom tutorial"]', 'Enter adds the third todo after the others');
    await t.click('.items li:nth-child(2) input[type=checkbox]');
    ok(JSON.stringify(await rows()) === '["item","item done","item"]', 'checking "Ship it" gives its row the done class');
    await t.click('.items li:nth-child(2) input[type=checkbox]');
    ok(JSON.stringify(await rows()) === '["item","item","item"]', 'unchecking "Ship it" takes the done class away');
    ok(await t.eval(`!document.querySelectorAll('.items li')[1].querySelector('input').checked`), 'and its checkbox is unchecked');
    await t.click('.items li:nth-child(3) input[type=checkbox]');
    ok(await t.text('.left') === '2 items left', `two open items after one check are "2 items left" (${await t.text('.left')})`);
    await t.clickText('.filters button', 'All');
    ok(JSON.stringify(await pressed()) === '["All:true","Active:false","Done:false"]', 'the page ends on All');
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
