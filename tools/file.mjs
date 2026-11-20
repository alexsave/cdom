// Runs tests/out/file.html (tests/file.c) in headless Chrome: files picked from disk into a file input
// (CDP DOM.setFileInputFiles, as a user's pick), then read with cd_file_read: every status, the bytes, stale
// answers dropped through cd_req, and the memory an answer leaves behind. Then the reader in order (cd_file_open,
// cd_file_next_cb, cd_file_close; the chunked-read design's test 10): the 20 MB file in pieces of 1 (its first MiB),
// 4093 and 1 MiB bytes, end to end equal to the file, the end said once, each answer a task of its own; a second next
// traps; a close with a piece pending answers nothing and writes nothing; a file deleted or replaced by a shorter one
// mid-read is UNREADABLE; and the same through FileReader on slices, where Blob.stream is missing. Fixtures: tests/fixtures (tools/fixtures.py);
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
const big = path.join(root, 'file-big.bin'), gone = path.join(root, 'file-gone.bin');
const bigBytes = Buffer.alloc(BIG);
for (let i = 0; i < BIG; i++) bigBytes[i] = (i * 131 + 7) & 255;
fs.writeFileSync(big, bigBytes);
fs.writeFileSync(gone, 'soon gone');
const SMALLF = path.join(root, 'file-small.bin');
fs.writeFileSync(SMALLF, 'a second, smaller file');
// For the reader: a 20 MB patterned file deleted mid-read, and one replaced by a shorter one mid-read.
const MID = BIG, del = path.join(root, 'file-del.bin'), shrink = path.join(root, 'file-shrink.bin');
fs.writeFileSync(del, bigBytes.subarray(0, MID));
fs.writeFileSync(shrink, bigBytes.subarray(0, MID));
const FILES = [path.join(fixtures, 'data.bin'), SMALLF, big, gone, del, shrink];
const [DATA, SMALL, BIGF, GONE, DEL, SHRINK] = FILES.keys();
const S = { OK: 0, NO_FILE: 1, TOO_BIG: 2, UNREADABLE: 3, UNSUPPORTED: 4, NO_ROOM: 5 };

const b = await launch();
let fails = 0;
const ok = (c, m, why = '') => { if (!c) { fails++; console.log('FAIL', m + (why ? ': ' + why : '')); } else console.log('ok  ', m); };
const E = b.evaluate;
// Record k as an object (tests/file.c, rec).
const rec = async (k) => { const v = await E(`[0, 1, 2, 3, 4].map((f) => X.rec(${k}, f))`);
  return { status: v[0], calls: v[1], len: v[2] >>> 0, pattern: v[3], keep: v[4] >>> 0 }; };
const answered = async (ks, ms = 10000) => {
  for (let t = 0; t < ms; t += 25) { if ((await E(`[${ks}].every((k) => X.rec(k, 0) !== -1)`))) return true; await sleep(25); }
  return false;
};
const traps = (expr) => E(`(() => { try { ${expr}; return 'no trap'; } catch (e) { return e instanceof WebAssembly.RuntimeError ? 'trap' : String(e); } })()`);
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
  fs.unlinkSync(gone); // picked, then gone from the disk before it is read

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

  // The token form, to the hook; a token in cdom's range traps.
  await E(`X.read_tok(${DATA}, 5000, 5)`);
  ok(await answered([5]) && (await rec(5)).status === S.OK && (await rec(5)).pattern === 1, 'cd_file_read with a token answers through cd_on_file');
  const bad = await traps('X.bad()');
  ok(bad === 'trap', 'traps: a token in cdom\'s range', bad);

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

  ok(await E('X.stale()') === 0, 'every answer\'s export flushes after the callback (no answer found a walk the one before left)', String(await E('X.stale()')));

  // The reader in order.
  const RD = ['status', 'reader', 'size', 'at', 'pieces', 'ends', 'bad', 'after', 'same', 'canary'];
  const rd = async () => { const v = await E(`[${RD.map((_, i) => i)}].map((f) => X.rd(f))`); const r = Object.fromEntries(RD.map((n, i) => [n, v[i]])); r.status |= 0; return r; };
  const rdDone = async (ms = 60000) => { for (let t = 0; t < ms; t += 20) { if ((await E('X.rd(0) | 0')) !== -1) return true; await sleep(20); } return false; };
  await E('tick()');
  for (const [max, limit] of [[1, 1 << 20], [4093, 0], [1 << 20, 0]]) {
    const st = await E(`X.rd_open(${BIGF})`);
    const slot0 = await E('X.slot_probe()');
    const t0 = Date.now();
    await E(`X.rd_start(${max}, ${limit}, 0)`);
    ok(await rdDone(), `next in pieces of ${max}: answered to the end`);
    const r = await rd();
    const want = limit || BIG;
    ok(st === 0 && r.size === BIG && r.status === (limit ? 100 : 0) && r.at === want && r.bad === 0 && r.canary === 1 && r.after === 0 && r.ends === (limit ? 0 : 1)
      && r.pieces === Math.ceil(want / max),
      `pieces of ${max}: ${limit ? 'the first MiB' : 'all 20 MB'} end to end equal to the file, each at most max, ${limit ? '' : 'the end (n 0) once, '}nothing past the room, nothing after (${Date.now() - t0} ms)`, JSON.stringify(r));
    ok(r.same <= r.pieces / 50, `pieces of ${max}: each answer is a task of its own (${r.same} of ${r.pieces} answers in the task of the one before)`, JSON.stringify(r));
    ok(await E('X.slot_probe()') === slot0, `pieces of ${max}: the callback slots are all freed`);
  }
  await E(`X.rd_open(${DATA})`);
  ok(await traps('X.rd_double()') === 'trap', 'a second cd_file_next_cb while one is pending traps');
  await E(`X.rd_open(${BIGF})`);
  const slotA = await E('X.slot_probe()');
  await E('X.rd_close_pending()');
  ok(await E('X.slot_probe()') === slotA, 'cd_file_close with a piece pending frees its callback slot');
  await sleep(300);
  const rc = await rd();
  ok(rc.pieces === 0 && rc.status === -1 && rc.after === 0 && await E('X.rd_untouched()') === 1, 'cd_file_close with a piece pending: no callback, nothing written at dst or around it', JSON.stringify(rc));
  ok(await E(`X.rd_open(40)`) === S.NO_FILE && await E(`X.rd_open(${DATA})`) === S.OK, 'cd_file_open answers at once: NO_FILE past the list, OK on a file');
  // Replaced by a shorter file after the first pieces: UNREADABLE. Deleted from the disk: after the open, before the
  // first piece, UNREADABLE; after the first pieces, UNREADABLE where the system ends an open file's reads (on Linux an
  // open file outlives its name, and the browser reads on to the end: then every byte must be the file's).
  const midRead = async (idx, act, first) => {
    await E(`X.rd_open(${idx})`);
    if (!first) act();
    await E('X.rd_start(65536, 0, 1)');
    for (let t = 0; first && t < 200 && (await E('X.rd(4)')) < 1; t++) await sleep(20);
    if (first) act();
    for (let t = 0; t < 3000 && (await E('X.rd(0) | 0')) === -1; t++) { await E('X.rd_more()'); await sleep(5); }
    return rd();
  };
  const rs1 = await midRead(SHRINK, () => fs.writeFileSync(shrink, 'short'), true);
  ok(rs1.status === S.UNREADABLE && rs1.bad === 0 && rs1.at < MID, `a file replaced by a shorter one mid-read: UNREADABLE after ${rs1.at} good bytes`, JSON.stringify(rs1));
  const rd1 = await midRead(DEL, () => fs.unlinkSync(del), true);
  ok((rd1.status === S.UNREADABLE && rd1.at < MID || rd1.status === S.OK && rd1.at === MID) && rd1.bad === 0,
    `a file deleted from the disk mid-read: ${rd1.status === S.OK ? 'read on to its end by the open file (this system keeps it)' : 'UNREADABLE'}, every byte given the file's`, JSON.stringify(rd1));
  fs.writeFileSync(del, bigBytes.subarray(0, 1000));
  const { result: { result: { objectId: pickId } } } = await b.send('Runtime.evaluate', { expression: 'document.getElementById("pick")' });
  await b.send('DOM.setFileInputFiles', { files: FILES, objectId: pickId });
  const rd2 = await midRead(DEL, () => fs.unlinkSync(del), false);
  ok(rd2.status === S.UNREADABLE && rd2.at === 0, 'a file deleted from the disk after cd_file_open, before its first piece: UNREADABLE', JSON.stringify(rd2));
  // The tier without Blob.stream: FileReader on slices of the size asked.
  const slices0 = await E('READS.length');
  await E(`(() => { window.STREAM = Blob.prototype.stream; delete Blob.prototype.stream; X.rd_open(${BIGF}); X.rd_start(${1 << 20}, 0, 0); })()`);
  ok(await rdDone(), 'without Blob.stream: answered to the end');
  await E('Blob.prototype.stream = STREAM');
  const rs = await rd(), sl = await E(`READS.slice(${slices0})`);
  ok(rs.status === 0 && rs.at === BIG && rs.bad === 0 && rs.ends === 1 && sl.length >= 20 && sl.every((n) => n <= 1 << 20),
    'without Blob.stream: all 20 MB through FileReader on slices of at most the size asked', JSON.stringify({ rs, slices: sl.length }));
  await E('window.TICKING = false');

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
  for (const f of [big, gone, SMALLF, del, shrink]) fs.rmSync(f, { force: true });
}
console.log(fails ? `${fails} failed` : 'all passed');
process.exit(fails ? 1 : 0);
