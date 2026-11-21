// node tools/paintshot.mjs <page.html> <out.png> <width> <height> <links.txt> [notext]: renders a design page of design/ns2paint/ in headless
// Chrome (tools/cdp.mjs) at exactly width x height CSS pixels, scale 1, and saves the screenshot; links.txt gets one line per element
// with a data-link attribute: "<target> <x> <y> <w> <h> <status text>" (its box in the picture's pixels, rounded out), which
// tools/gen_ns2_paint.py turns into links around runs.  notext renders it with the class notext on <html> (design/ns2paint/paint.css:
// every glyph and icon transparent), the background the text snapping of tools/gen_ns2_paint.py measures against.  Used by docs/descent.md, "Painting a modern page".
import { launch, sleep } from './cdp.mjs';
import fs from 'node:fs';
import path from 'node:path';
const [page, out, w, h, linksOut, mode] = process.argv.slice(2);
const b = await launch();
await b.send('Emulation.setDeviceMetricsOverride', { width: +w, height: +h, deviceScaleFactor: 1, mobile: false });
await b.send('Page.navigate', { url: 'file://' + path.resolve(page) });
await sleep(1500);
if (mode === 'notext') await b.evaluate("document.documentElement.classList.add('notext'), 1");
await b.evaluate('document.fonts.ready.then(() => 1)');
const { result } = await b.send('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width: +w, height: +h, scale: 1 } });
fs.writeFileSync(out, Buffer.from(result.data, 'base64'));
const lines = await b.evaluate(`[...document.querySelectorAll('[data-link]')].map((e) => { const r = e.getBoundingClientRect();
  const x = Math.floor(r.left), y = Math.floor(r.top);
  return [e.dataset.link, x, y, Math.ceil(r.right) - x, Math.ceil(r.bottom) - y, e.dataset.status || ''].join(' '); }).join('\\n')`);
fs.writeFileSync(linksOut, lines + '\n');
console.log(out, w + 'x' + h, (lines.match(/\n/g) || []).length + (lines ? 1 : 0), 'links', b.exceptions.join(' '));
await b.close();
process.exit(0);
