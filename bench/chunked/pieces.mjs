// The cost of reading a picked file in pieces, in headless Chrome (docs/chunked-file-read-design.md, the shim's side).
// A real file from disk is put in a file input (CDP DOM.setFileInputFiles, as a user's pick), then read:
//   whole     FileReader.readAsArrayBuffer(file), today's cd_file_read
//   fr N      FileReader.readAsArrayBuffer(file.slice(at, at + N)), one piece in flight, on the main thread
//   ab N      file.slice(at, at + N).arrayBuffer(), one in flight
//   ab2 N     the same, two in flight
//   stream    file.stream().getReader(), the browser's own piece size
//   sync N    FileReaderSync.readAsArrayBuffer(file.slice(...)) in a dedicated worker, as a job would read
// Each piece is copied once into a Uint8Array of its size (the copy into C memory). Medians of REPS interleaved rounds.
// Usage: CHROME=path node bench/chunked/pieces.mjs FILE [REPS=5]
import http from 'node:http';
import { launch } from '../../tools/cdp.mjs';
const file = (await import('node:path')).resolve(process.argv[2]), reps = +(process.argv[3] || 5);
const page = `<!doctype html><meta charset=utf-8><input type=file id=pick><script>
const into = new Uint8Array(1 << 20);
function take(buf) { const v = new Uint8Array(buf); into.set(v.subarray(0, Math.min(v.length, into.length))); return v.length; }
function fr(blob) { return new Promise((ok, no) => { const r = new FileReader(); r.onload = () => ok(r.result); r.onerror = () => no(r.error); r.readAsArrayBuffer(blob); }); }
async function run(kind, n) {
  const f = document.getElementById('pick').files[0], size = f.size;
  let pieces = 0, longest = 0, total = 0;
  const t0 = performance.now();
  if (kind === 'whole') { total = take(await fr(f)); pieces = 1; }
  else if (kind === 'fr' || kind === 'ab') {
    for (let at = 0; at < size; at += n) {
      const s = performance.now();
      const b = kind === 'fr' ? await fr(f.slice(at, at + n)) : await f.slice(at, at + n).arrayBuffer();
      total += take(b); pieces++; longest = Math.max(longest, performance.now() - s);
    }
  } else if (kind === 'ab2') {
    let next = f.slice(0, n).arrayBuffer();
    for (let at = 0; at < size; at += n) {
      const cur = next;
      if (at + n < size) next = f.slice(at + n, at + 2 * n).arrayBuffer();
      total += take(await cur); pieces++;
    }
  } else if (kind === 'stream') {
    const r = f.stream().getReader();
    for (;;) { const c = await r.read(); if (c.done) break; total += take(c.value.buffer); pieces++; longest = Math.max(longest, c.value.length); }
  } else if (kind === 'sync') {
    const w = new Worker(URL.createObjectURL(new Blob(['(' + function () {
      onmessage = (e) => {
        const [f, n] = e.data, into = new Uint8Array(1 << 20), rs = new FileReaderSync();
        let total = 0, pieces = 0, longest = 0;
        const t0 = performance.now();
        for (let at = 0; at < f.size; at += n) {
          const s = performance.now();
          const v = new Uint8Array(rs.readAsArrayBuffer(f.slice(at, at + n)));
          into.set(v.subarray(0, Math.min(v.length, into.length))); total += v.length; pieces++;
          longest = Math.max(longest, performance.now() - s);
        }
        postMessage([performance.now() - t0, total, pieces, longest]);
      };
    } + ')()'], { type: 'text/javascript' })));
    const r = await new Promise((ok) => { w.onmessage = (e) => ok(e.data); w.postMessage([f, n]); });
    w.terminate();
    return { ms: performance.now() - t0, inner: r[0], total: r[1], pieces: r[2], longest: r[3] };
  }
  return { ms: performance.now() - t0, total, pieces, longest };
}
// The overlap arms: a stand-in decode (a busy loop of BUSY ms per MiB taken) on the thread that reads, the next piece
// asked for before the current one is decoded (one in flight ahead). What a read adds is the total less the busy time.
const BUSY = 30;
function busy(bytes) { const end = performance.now() + BUSY * bytes / 1048576; while (performance.now() < end); }
async function overlap(kind, n) {
  const f = document.getElementById('pick').files[0], size = f.size;
  const t0 = performance.now();
  if (kind === 'ovl-ab') {
    let next = f.slice(0, n).arrayBuffer();
    for (let at = 0; at < size; at += n) {
      const cur = await next;
      if (at + n < size) next = f.slice(at + n, at + 2 * n).arrayBuffer();
      busy(take(cur));
    }
  } else if (kind === 'ovl-stream') {
    const r = f.stream().getReader();
    let next = r.read();
    for (;;) { const c = await next; if (c.done) break; next = r.read(); busy(take(c.value.buffer)); }
  } else if (kind === 'ovl-whole') {
    busy(take(await fr(f)));
  } else {
    const w = new Worker(URL.createObjectURL(new Blob(['const BUSY = ' + BUSY + ';' + busy + '(' + function () {
      onmessage = async (e) => {
        const [kind, f, n] = e.data, into = new Uint8Array(1 << 20);
        const take = (b) => { const v = new Uint8Array(b); into.set(v.subarray(0, Math.min(v.length, into.length))); return v.length; };
        if (kind === 'ovl-sync') { const rs = new FileReaderSync(); for (let at = 0; at < f.size; at += n) busy(take(rs.readAsArrayBuffer(f.slice(at, at + n)))); }
        else if (kind === 'ovl-wstream') {
          const r = f.stream().getReader(); let next = r.read();
          for (;;) { const c = await next; if (c.done) break; next = r.read(); busy(take(c.value.buffer)); }
        } else {
          let next = f.slice(0, n).arrayBuffer();
          for (let at = 0; at < f.size; at += n) { const cur = await next; if (at + n < f.size) next = f.slice(at + n, at + 2 * n).arrayBuffer(); busy(take(cur)); }
        }
        postMessage(0);
      };
    } + ')()'], { type: 'text/javascript' })));
    const t1 = performance.now();
    await new Promise((ok) => { w.onmessage = ok; w.postMessage([kind, f, n]); });
    w.terminate();
    return { ms: performance.now() - t1, busy: BUSY * size / 1048576 };
  }
  return { ms: performance.now() - t0, busy: BUSY * size / 1048576 };
}
</script>`;
const srv = http.createServer((q, r) => { r.writeHead(200, { 'content-type': 'text/html' }); r.end(page); }).listen(0);
const b = await launch();
try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/` });
  for (let i = 0; i < 100 && !(await b.evaluate('typeof run === "function"')); i++) await new Promise((r) => setTimeout(r, 50));
  const { result: { result: { objectId } } } = await b.send('Runtime.evaluate', { expression: 'document.getElementById("pick")' });
  await b.send('DOM.setFileInputFiles', { files: [file], objectId });
  const arms = [['whole', 0], ['fr', 65536], ['fr', 262144], ['fr', 1048576], ['ab', 65536], ['ab', 262144], ['ab', 1048576],
    ['ab2', 262144], ['stream', 0], ['sync', 65536], ['sync', 262144], ['sync', 1048576]];
  const res = arms.map(() => []);
  for (let r = 0; r < reps; r++) for (let a = 0; a < arms.length; a++) res[a].push(await b.evaluate(`run(${JSON.stringify(arms[a][0])}, ${arms[a][1]})`));
  const oarms = [['ovl-whole', 0], ['ovl-ab', 262144], ['ovl-ab', 1048576], ['ovl-stream', 0], ['ovl-sync', 262144], ['ovl-sync', 1048576],
    ['ovl-wab', 1048576], ['ovl-wstream', 0]];
  const ores = oarms.map(() => []);
  for (let r = 0; r < reps; r++) for (let a = 0; a < oarms.length; a++) ores[a].push(await b.evaluate(`overlap(${JSON.stringify(oarms[a][0])}, ${oarms[a][1]})`));
  console.log(`# ${b.version}, ${file}, ${reps} rounds, medians`);
  console.log('arm\tpiece\tpieces\ttotal ms\tper piece ms\tlongest piece ms (stream: largest piece bytes)\tinside the worker ms');
  var med = (xs) => xs.slice().sort((x, y) => x - y)[xs.length >> 1];
  arms.forEach(([k, n], a) => {
    const ms = med(res[a].map((x) => x.ms)), p = res[a][0].pieces;
    console.log([k, n || '-', p, ms.toFixed(1), (ms / p).toFixed(3), med(res[a].map((x) => x.longest)).toFixed(k === 'stream' ? 0 : 2),
      k === 'sync' ? med(res[a].map((x) => x.inner)).toFixed(1) : '-'].join('\t'));
  });
  console.log(`# overlap: a stand-in decode of 30 ms per MiB on the reading thread (worker arms: in a worker, timed from the post)`);
  console.log('arm\tpiece\ttotal ms\tbusy ms\tadded by reading ms');
  oarms.forEach(([k, n], a) => { const ms = med(ores[a].map((x) => x.ms)), bz = ores[a][0].busy;
    console.log([k, n || '-', ms.toFixed(1), bz.toFixed(1), (ms - bz).toFixed(1)].join('\t')); });
} finally { await b.close(); srv.close(); }
