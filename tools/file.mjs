// Runs tests/out/file.html (tests/file.c) in headless Chrome: files picked from disk into a file input
// (CDP DOM.setFileInputFiles, as a user's pick), then read with cd_file_read and made into pictures with
// cd_file_fit; every status, the bytes, the fit's sizes and orientation, the WebP fallback, stale answers
// dropped through cd_req, and the memory an answer leaves behind. Fixtures: tests/fixtures (tools/fixtures.py);
// the 20 MB file and the file that goes missing are made here, in tests/out. Exit 1 on any failure.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from './cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const root = path.join(top, 'tests/out'), fixtures = path.join(top, 'tests/fixtures');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
// The files, by their index in the input's list.
const BIG = 20000000;
const big = path.join(root, 'file-big.bin'), gone = path.join(root, 'file-gone.bin'), goneImage = path.join(root, 'file-gone.png');
const bigBytes = Buffer.alloc(BIG);
for (let i = 0; i < BIG; i++) bigBytes[i] = (i * 131 + 7) & 255;
fs.writeFileSync(big, bigBytes);
fs.writeFileSync(gone, 'soon gone');
fs.copyFileSync(path.join(fixtures, 'land.png'), goneImage);
const FILES = ['data.bin', 'land.png', 'port.png', 'small.png', 'odd.png', 'rot6.jpg', 'notimage.png'].map((f) => path.join(fixtures, f)).concat([big, gone, goneImage]);
const [DATA, LAND, PORT, SMALL, ODD, ROT6, NOTIMAGE, BIGF, GONE, GONE_IMAGE] = FILES.keys();
const S = { OK: 0, NO_FILE: 1, TOO_BIG: 2, UNREADABLE: 3, UNSUPPORTED: 4, NO_ROOM: 5, NOT_IMAGE: 6 };
const T = { JPEG: 0, WEBP: 1, PNG: 2 };

const b = await launch();
let fails = 0;
const ok = (c, m, why = '') => { if (!c) { fails++; console.log('FAIL', m + (why ? ': ' + why : '')); } else console.log('ok  ', m); };
const E = b.evaluate;
// Record k as an object (tests/file.c, rec).
const rec = async (k) => { const v = await E(`[0, 1, 2, 3, 4, 5, 6, 7].map((f) => X.rec(${k}, f))`);
  return { status: v[0], calls: v[1], len: v[2] >>> 0, w: v[3], h: v[4], type: v[5], pattern: v[6], keep: v[7] >>> 0 }; };
const answered = async (ks, ms = 10000) => {
  for (let t = 0; t < ms; t += 25) { if ((await E(`[${ks}].every((k) => X.rec(k, 0) !== -1)`))) return true; await sleep(25); }
  return false;
};
const traps = (expr) => E(`(() => { try { ${expr}; return 'no trap'; } catch (e) { return e instanceof WebAssembly.RuntimeError ? 'trap' : String(e); } })()`);
// The picture C kept for record k, decoded by the page: its size and the colour at two points (fractions of it).
const picture = (k, mime, at) => E(`(async () => {
  const len = X.rec(${k}, 2) >>> 0, p = X.rec(${k}, 7) >>> 0;
  const bytes = new Uint8Array(X.memory.buffer, p, len).slice();
  const bm = await createImageBitmap(new Blob([bytes], { type: '${mime}' }));
  const cv = new OffscreenCanvas(bm.width, bm.height), cx = cv.getContext('2d'); cx.drawImage(bm, 0, 0);
  const px = (fx, fy) => [...cx.getImageData(Math.floor(fx * bm.width), Math.floor(fy * bm.height), 1, 1).data].slice(0, 3);
  return { w: bm.width, h: bm.height, head: [bytes[0], bytes[1]], at: ${JSON.stringify(at)}.map(([fx, fy]) => px(fx, fy)) };
})()`);
const red = ([r, g, b]) => r > 150 && g < 90 && b < 90, blue = ([r, g, b]) => b > 150 && r < 90 && g < 90;
async function pickAll(page) {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/${page}` });
  for (let i = 0; i < 100 && !(await E('!!window.READY')); i++) await sleep(50);
  const { result: { result: { objectId } } } = await b.send('Runtime.evaluate', { expression: 'document.getElementById("pick")' });
  await b.send('DOM.setFileInputFiles', { files: FILES, objectId });
}
try {
  await pickAll('file.html');
  ok(await E('!!window.READY'), 'the page started');
  const sizes = await E(`[X.size_of(0), X.size_of(${BIGF}), String(X.size_of(40))]`);
  ok(sizes.join() === `1000,${BIG},NaN`, 'a picked file\'s size is a path away before reading: cd_get_num(input, "files.0.size"), NaN past the list', JSON.stringify(sizes));
  // Picked, then gone from the disk before it is read: one whose size the page never asked (Chrome then
  // reports 0), and an image whose size it did.
  ok(await E(`X.size_of(${GONE_IMAGE})`) === fs.statSync(goneImage).size, 'the image that goes missing has its size');
  fs.unlinkSync(gone);
  fs.unlinkSync(goneImage);

  // cd_file_read: the bytes; at exactly max_bytes; over it, TOO_BIG with the size and never read; nothing at
  // the index, or a node with no files list; a file gone from the disk.
  const sync = await E(`READS.length = 0; X.read(X.pick(), ${DATA}, 1000, 0); X.read(X.pick(), ${DATA}, 999, 1); X.read(X.pick(), 40, 1000, 2);
    X.read(X.plain(), 0, 1000, 3); X.read(X.pick(), ${GONE}, 1000, 4); [0, 1, 2, 3, 4].map((k) => X.rec(k, 0))`);
  ok(sync.every((s) => s === -1), 'every answer comes from the event loop, never inside the call (even NO_FILE and TOO_BIG)', JSON.stringify(sync));
  ok(await answered([0, 1, 2, 3, 4]), 'every read answered');
  const [r0, r1, r2, r3, r4] = await Promise.all([0, 1, 2, 3, 4].map(rec));
  ok(r0.status === S.OK && r0.len === 1000 && r0.pattern === 1 && r0.calls === 1, 'OK: the file\'s 1,000 bytes in C memory, each the byte it is on disk, at max_bytes exactly', JSON.stringify(r0));
  ok(r1.status === S.TOO_BIG && r1.len === 1000 && r1.keep === 0, 'TOO_BIG: over max_bytes, with the real size and no bytes', JSON.stringify(r1));
  const reads = await E('READS.slice()');
  ok(reads.filter((n) => n === 1000).length === 1, 'a file over max_bytes is never read (one read of the 1,000-byte file, for the OK)', JSON.stringify(reads));
  ok(r2.status === S.NO_FILE && r2.len === 0 && r3.status === S.NO_FILE, 'NO_FILE: nothing at the index, and a node with no files list', JSON.stringify([r2, r3]));
  ok(r4.status === S.UNREADABLE && r4.len === 0, 'UNREADABLE: a picked file gone from the disk before the read', JSON.stringify(r4));

  // The token form, to the hook; tokens in cdom's range and bad fit arguments trap.
  await E(`X.read_tok(${DATA}, 5000, 5)`);
  ok(await answered([5]) && (await rec(5)).status === S.OK && (await rec(5)).pattern === 1, 'cd_file_read with a token answers through cd_on_file');
  const bad = [];
  for (let i = 0; i < 6; i++) bad.push(await traps(`X.bad(${i})`));
  ok(bad.every((t) => t === 'trap'), 'traps: a token in cdom\'s range (read, fit), a box of 0 wide or high, a type past PNG, a quality over 100', JSON.stringify(bad));

  // A stale answer is dropped through cd_req: the second read begun before the first answered is the one applied.
  await E(`X.stale_reads(${DATA}, ${SMALL}, 6, 7)`);
  ok(await answered([7]), 'the current read answered');
  await sleep(200);
  const [s6, s7] = await Promise.all([6, 7].map(rec));
  ok(s6.status === -1 && s7.status === S.OK && s7.len === fs.statSync(FILES[SMALL]).size && await E('X.dropped()') === 1,
    'a read answered after a newer one of its slot began is dropped (cd_req_current), the newer one applied', JSON.stringify({ s6, s7, dropped: await E('X.dropped()') }));

  // Memory: a 20 MB file read twice, then 20 MB of the app's: the bytes are freed after each answer, so the
  // reads and the app's block share one room (docs/file-read-design.md, the prediction: 305 to 307 pages).
  const p0 = await E('X.pages()');
  await E(`X.read(X.pick(), ${BIGF}, ${BIG}, 8)`);
  ok(await answered([8], 20000), 'the 20 MB read answered');
  const p1 = await E('X.pages()');
  await E(`X.read(X.pick(), ${BIGF}, ${BIG}, 9)`);
  ok(await answered([9], 20000), 'the second 20 MB read answered');
  const p2 = await E('X.pages()');
  await E('X.take20()');
  const p3 = await E('X.pages()');
  const [b8, b9] = await Promise.all([8, 9].map(rec));
  console.log(`     memory: +${p1 - p0} pages for the first 20 MB read, +${p2 - p1} for the second, +${p3 - p2} for the app's 20 MB after`);
  ok(b8.status === S.OK && b8.pattern === 1 && b9.status === S.OK && b9.pattern === 1, 'a 20 MB file, read twice, every byte as on disk');
  ok(p1 - p0 >= 305 && p1 - p0 <= 307 && p2 === p1 && p3 === p2, 'nothing holds a file\'s bytes past its answer: the second read and the app\'s 20 MB after it grow the memory 0 pages',
    `+${p1 - p0}, +${p2 - p1}, +${p3 - p2}`);

  // cd_file_fit: the box's sizes, landscape and portrait, one that fits already (never scaled up), odd sizes,
  // a box of another shape; PNG out, so the colours are exact.
  const fits = [[LAND, 100, 100, 100, 67], [PORT, 100, 100, 67, 100], [SMALL, 100, 100, 50, 30], [ODD, 100, 100, 100, 30],
    [LAND, 200, 50, 75, 50], [ODD, 333, 101, 333, 101], [ODD, 101, 7, 23, 7]];
  await E(fits.map(([i, mw, mh], k) => `X.fit(${i}, ${mw}, ${mh}, ${T.PNG}, 0, ${10 + k})`).join(';'));
  ok(await answered(fits.map((_, k) => 10 + k)), 'every fit answered');
  for (const [k, [i, mw, mh, w, h]] of fits.entries()) {
    const r = await rec(10 + k), pic = r.status === S.OK ? await picture(10 + k, 'image/png', [[0.1, 0.5], [0.9, 0.5]]) : null;
    ok(r.status === S.OK && r.w === w && r.h === h && r.type === T.PNG && pic && pic.w === w && pic.h === h && red(pic.at[0]) && blue(pic.at[1]),
      `cd_file_fit: ${path.basename(FILES[i])} into ${mw} x ${mh} is ${w} x ${h}, upright, the answer's size the picture's`, JSON.stringify({ r, pic }));
  }
  // EXIF orientation 6: stored 60 x 40 (red over blue), upright 40 x 60 (blue left, red right); into 20 x 20, 13 x 20.
  await E(`X.fit(${ROT6}, 20, 20, ${T.PNG}, 0, 17); X.fit(${ROT6}, 100, 100, ${T.PNG}, 0, 18)`);
  ok(await answered([17, 18]), 'the rotated photo answered');
  const [o17, o18] = await Promise.all([17, 18].map(rec));
  const pic18 = await picture(18, 'image/png', [[0.1, 0.5], [0.9, 0.5]]);
  ok(o17.w === 13 && o17.h === 20 && o18.w === 40 && o18.h === 60 && blue(pic18.at[0]) && red(pic18.at[1]),
    'a JPEG with EXIF orientation 6 comes out upright: 40 x 60, blue on the left and red on the right, and fits a box as its upright size', JSON.stringify({ o17, o18, pic18 }));
  // JPEG and WebP out, with their quality; a browser that cannot encode WebP (toBlob gives PNG, as Safari's) gives JPEG.
  await E(`X.fit(${ODD}, 300, 300, ${T.JPEG}, 5, 19); X.fit(${ODD}, 300, 300, ${T.JPEG}, 100, 20); X.fit(${ODD}, 300, 300, ${T.WEBP}, 80, 21)`);
  ok(await answered([19, 20, 21]), 'the encoded fits answered');
  const [j19, j20, w21] = await Promise.all([19, 20, 21].map(rec));
  const head19 = (await picture(19, 'image/jpeg', [])).head, head21 = (await picture(21, 'image/webp', [])).head;
  ok(j19.type === T.JPEG && head19.join() === '255,216' && j20.len > j19.len * 1.2, 'JPEG out at its quality: 100 is larger than 5', JSON.stringify({ j19, j20, head19 }));
  ok(w21.type === T.WEBP && head21.join() === '82,73', 'WebP out where the browser encodes it (RIFF)', JSON.stringify({ w21, head21 }));
  await E(`window.realToBlob = HTMLCanvasElement.prototype.toBlob;
    HTMLCanvasElement.prototype.toBlob = function (done, mime, q) { return realToBlob.call(this, done, mime === 'image/webp' ? 'image/png' : mime, q); };
    X.fit(${ODD}, 300, 300, ${T.WEBP}, 80, 22)`);
  ok(await answered([22]), 'the fit with no WebP encoder answered');
  const f22 = await rec(22), head22 = (await picture(22, 'image/jpeg', [])).head;
  await E('HTMLCanvasElement.prototype.toBlob = realToBlob');
  ok(f22.status === S.OK && f22.type === T.JPEG && head22.join() === '255,216' && f22.w === 300,
    'a browser that cannot encode WebP gives JPEG instead, and the answer says JPEG', JSON.stringify({ f22, head22 }));
  // Not an image; gone from the disk; nothing at the index; the token form to its hook.
  await E(`X.fit(${NOTIMAGE}, 50, 50, ${T.PNG}, 0, 23); X.fit(${GONE}, 50, 50, ${T.PNG}, 0, 24); X.fit(${GONE_IMAGE}, 50, 50, ${T.PNG}, 0, 29); X.fit(40, 50, 50, ${T.PNG}, 0, 25);
    X.fit_tok(${LAND}, 30, 30, ${T.JPEG}, 80, 26)`);
  ok(await answered([23, 24, 25, 26, 29]), 'the failing fits answered');
  const [n23, g24, m25, t26] = await Promise.all([23, 24, 25, 26].map(rec));
  ok(n23.status === S.NOT_IMAGE && n23.len === 0 && n23.w === 0, 'NOT_IMAGE: a file the browser cannot decode', JSON.stringify(n23));
  const g29 = await rec(29);
  ok(g24.status === S.UNREADABLE && g29.status === S.UNREADABLE, 'UNREADABLE: a file gone from the disk, its size unknown (0) or known', JSON.stringify([g24, g29]));
  ok(m25.status === S.NO_FILE, 'NO_FILE: nothing at the index', JSON.stringify(m25));
  ok(t26.status === S.OK && t26.w === 30 && t26.h === 20 && t26.type === T.JPEG, 'cd_file_fit with a token answers through cd_on_file_fit', JSON.stringify(t26));
  // A stale fit is dropped.
  await E(`X.stale_fits(${LAND}, ${PORT}, 27, 28)`);
  ok(await answered([28]), 'the current fit answered');
  await sleep(200);
  const [s27, s28] = await Promise.all([27, 28].map(rec));
  ok(s27.status === -1 && s28.status === S.OK && s28.w === 33 && s28.h === 50 && await E('X.dropped()') === 2,
    'a fit answered after a newer one of its slot began is dropped, the newer one applied', JSON.stringify({ s27, s28 }));

  // NO_ROOM: the heap capped at 12.5 MB (file_cap.wasm) has no room for 20 MB; the size still comes.
  await pickAll('file_cap.html');
  await E(`X.read(X.pick(), ${BIGF}, ${BIG}, 0)`);
  ok(await answered([0], 20000), 'the read past the heap\'s cap answered');
  const c0 = await rec(0);
  ok(c0.status === S.NO_ROOM && c0.len === BIG, 'NO_ROOM: bytes that do not fit in C memory are a status with their size, not a trap', JSON.stringify(c0));
} catch (e) {
  fails++;
  console.log('FAIL', e.stack || e);
} finally {
  await b.close();
  srv.close();
  for (const f of [big, gone, goneImage]) fs.rmSync(f, { force: true });
}
console.log(fails ? `${fails} failed` : 'all passed');
process.exit(fails ? 1 : 0);
