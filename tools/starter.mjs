// Opens a page the starter built (make -C cdom new, then make: tests/link/run.sh) from disk, file://, in
// headless Chrome, and checks what a new user sees: "cdom" in an <h1>, its box centered in the viewport
// (both axes, to a pixel), the "edit src/app.c" hint, no error, and nothing fetched but the page (its
// module is bundled, so a separate .wasm, which file:// cannot fetch, is never asked for).
// Usage: node tools/starter.mjs path/to/build/index.html [screenshot.png]
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { launch, sleep } from './cdp.mjs';

const page = path.resolve(process.argv[2]);
const shot = process.argv[3];
let bad = 0;
const ok = (c, m) => { console.log(c ? 'ok  ' : 'FAIL', m); if (!c) bad++; };
const b = await launch();
const errors = [];
b.onEvent = (e) => {
  if (e.method === 'Runtime.exceptionThrown') errors.push(e.params.exceptionDetails.exception?.description || e.params.exceptionDetails.text);
  if (e.method === 'Runtime.consoleAPICalled' && e.params.type === 'error') errors.push(e.params.args.map((a) => a.value ?? a.description).join(' '));
};
try {
  await b.send('Page.navigate', { url: pathToFileURL(page).href });
  let seen = null;
  for (let i = 0; i < 100 && !seen; i++) {
    await sleep(50);
    seen = await b.evaluate(`(() => {
      const h = document.querySelector('#app h1');
      if (!h || h.textContent !== 'cdom') return null;
      const r = h.getBoundingClientRect(), t = document.createRange();
      t.selectNodeContents(h);
      const g = t.getBoundingClientRect(); // the glyphs' box, not the block's full width
      const hint = document.querySelector('#app .hint'), u = document.createRange();
      if (hint) u.selectNodeContents(hint);
      const hg = hint ? u.getBoundingClientRect() : null;
      return { protocol: location.protocol, w: innerWidth, h: innerHeight,
        box: [r.left, r.top, r.width, r.height], text: [g.left, g.top, g.width, g.height],
        hint: hint ? hint.textContent : null, hintBox: hg ? [hg.left, hg.top, hg.width, hg.height] : null,
        scroll: [document.documentElement.scrollWidth, document.documentElement.scrollHeight],
        fetched: performance.getEntriesByType('resource').map((e) => e.name) };
    })()`);
  }
  ok(seen, `the page shows "cdom" in an <h1> in #app${seen ? '' : ` (body: ${JSON.stringify(await b.evaluate('document.body.innerHTML.slice(0, 200)'))})`}`);
  if (seen) {
    ok(seen.protocol === 'file:', `it was opened from disk (${seen.protocol})`);
    const [x, y, w, h] = seen.text;
    const dx = x + w / 2 - seen.w / 2, dy = y + h / 2 - seen.h / 2;
    ok(w > 0 && Math.abs(dx) <= 1 && Math.abs(dy) <= 1,
      `"cdom" is centered in the ${seen.w}x${seen.h} viewport (its text's center is off by ${dx.toFixed(1)}, ${dy.toFixed(1)} px)`);
    const hx = seen.hintBox ? seen.hintBox[0] + seen.hintBox[2] / 2 - seen.w / 2 : NaN;
    ok(seen.hint === 'edit src/app.c' && seen.hintBox[1] + seen.hintBox[3] <= seen.h && Math.abs(hx) <= 1,
      `the hint "${seen.hint}" is on screen, centered across (off by ${hx.toFixed(1)} px)`);
    ok(seen.scroll[0] <= seen.w && seen.scroll[1] <= seen.h, `the page does not scroll (${seen.scroll.join('x')})`);
    ok(seen.fetched.length === 0, `nothing was fetched but the page (${JSON.stringify(seen.fetched)})`);
  }
  ok(errors.length === 0, `no error on the page${errors.length ? `: ${errors.join(' | ')}` : ''}`);
  if (shot) {
    const { result } = await b.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(shot, Buffer.from(result.data, 'base64'));
  }
} finally {
  await b.close();
}
process.exit(bad ? 1 : 0);
