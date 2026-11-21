// node tools/ns2render_layout.mjs <page.html> <out.tsv> <width> <height>: the layout of a design page of design/ns2paint/ as
// headless Chrome lays it out, for tools/gen_ns2_render.py (docs/descent.md, "Netscape 2 paints itself").  Tab-separated lines:
//   E  name x y w h  background-color  background-image  radius-tl radius-bl  box-shadow  border-width border-color  z  link
//      bottom-border-width bottom-border-color
//      every element and the ::before and ::after that paint something, in paint order (stacking z, then document order)
//   C  char x top w h  font-size font-weight color  z  state  link
//      every character of every text node, its box as Chrome placed it (text-transform applied)
//   L  target x y w h status   every element with data-link (the link regions)
//   B  ratio   the baseline of the page's font below a character box's top, in font sizes
// A throwaway layout dump for an offline generator, not a shipped path.
import { launch, sleep } from './cdp.mjs';
import fs from 'node:fs';
import path from 'node:path';
const [page, out, w, h] = process.argv.slice(2);
const b = await launch();
await b.send('Emulation.setDeviceMetricsOverride', { width: +w, height: +h, deviceScaleFactor: 1, mobile: false });
await b.send('Page.navigate', { url: 'file://' + path.resolve(page) });
await sleep(1500);
await b.evaluate('document.fonts.ready.then(() => 1)');
const text = await b.evaluate(`(() => {
  const L = [], T = '\\t';
  const zOf = (e) => { let z = 0; for (let p = e; p && p.nodeType === 1; p = p.parentElement) { const v = getComputedStyle(p).zIndex; if (v !== 'auto') { z = +v; } } return z; };
  const linkOf = (e) => { for (let p = e; p && p.nodeType === 1; p = p.parentElement) if (p.dataset && p.dataset.link) return p.dataset.link + ':' + p.getBoundingClientRect().left; return '-'; };
  const els = [...document.querySelectorAll('body *')];
  let order = 0;
  const rows = [];
  const push = (e, pseudo, r, cs) => {
    const paints = cs.backgroundColor !== 'rgba(0, 0, 0, 0)' || cs.backgroundImage !== 'none' || cs.boxShadow !== 'none' || parseFloat(cs.borderTopWidth) > 0 || parseFloat(cs.borderBottomWidth) > 0;
    if (!paints || cs.display === 'none') return;
    const name = e.tagName.toLowerCase() + (e.className && typeof e.className === 'string' ? '.' + e.className.split(' ').join('.') : '') + pseudo;
    rows.push([zOf(e), order++, ['E', name, r.left, r.top, r.width, r.height, cs.backgroundColor, cs.backgroundImage, cs.borderTopLeftRadius, cs.borderBottomLeftRadius,
      cs.boxShadow, cs.borderTopWidth, cs.borderTopColor, zOf(e), linkOf(e), cs.borderBottomWidth, cs.borderBottomColor].join(T)]);
  };
  for (const e of els) {
    if (e.closest('svg') && e.tagName.toLowerCase() !== 'svg') continue;
    const r = e.getBoundingClientRect(), cs = getComputedStyle(e);
    if (e.tagName.toLowerCase() === 'svg') {
      rows.push([zOf(e), order++, ['E', 'svg', r.left, r.top, r.width, r.height, cs.color, 'icon', 0, 0, 'none', 0, '-', zOf(e), linkOf(e), '0px', '-'].join(T)]);
      continue;
    }
    for (const pseudo of ['', '::before']) {
      const s = pseudo ? getComputedStyle(e, pseudo) : cs;
      if (pseudo && (s.content === 'none' || s.content === 'normal')) continue;
      if (!pseudo) { push(e, '', r, s); continue; }
      const x = r.left + parseFloat(s.left), y = r.top + parseFloat(s.top);
      push(e, pseudo, { left: x, top: y, width: parseFloat(s.width), height: parseFloat(s.height) }, s);
    }
    const s = getComputedStyle(e, '::after');
    if (s.content !== 'none' && s.content !== 'normal') {
      push(e, '::after', { left: r.left + parseFloat(s.left), top: r.top + parseFloat(s.top), width: parseFloat(s.width), height: parseFloat(s.height) }, s);
    }
  }
  rows.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  for (const r of rows) L.push(r[2]);
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let n; (n = walker.nextNode());) {
    const e = n.parentElement, cs = getComputedStyle(e);
    if (e.closest('svg')) continue;
    const up = cs.textTransform === 'uppercase';
    for (let i = 0; i < n.data.length; i++) {
      const ch = n.data[i];
      if (/\\s/.test(ch)) continue;
      const rg = document.createRange(); rg.setStart(n, i); rg.setEnd(n, i + 1);
      const r = rg.getBoundingClientRect();
      if (!r.width) continue;
      L.push(['C', up ? ch.toUpperCase() : ch, r.left, r.top, r.width, r.height, cs.fontSize, cs.fontWeight, cs.color, zOf(e), linkOf(e)].join(T));
    }
  }
  for (const e of document.querySelectorAll('[data-link]')) {
    const r = e.getBoundingClientRect();
    L.push(['L', e.dataset.link, Math.floor(r.left), Math.floor(r.top), Math.ceil(r.right) - Math.floor(r.left), Math.ceil(r.bottom) - Math.floor(r.top), e.dataset.status || ''].join(T));
  }
  const p = document.createElement('div');
  p.style.cssText = 'position:absolute;left:0;top:0;font-size:100px;line-height:normal';
  p.innerHTML = 'H<span style="display:inline-block;width:0;height:0;vertical-align:baseline"></span>';
  document.body.appendChild(p);
  const rg = document.createRange(); rg.setStart(p.firstChild, 0); rg.setEnd(p.firstChild, 1);
  L.push(['B', (p.lastChild.getBoundingClientRect().bottom - rg.getBoundingClientRect().top) / 100].join(T));
  p.remove();
  return L.join('\\n');
})()`);
fs.writeFileSync(out, text + '\n');
console.log(out, text.split('\n').length, 'lines', b.exceptions.join(' '));
await b.close();
process.exit(0);
