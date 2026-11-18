// A static server over the repo root (and /css from jfb, for the keyed pages), on a free port.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const top = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const types = { '.html': 'text/html; charset=utf-8', '.wasm': 'application/wasm', '.js': 'text/javascript', '.css': 'text/css' };
export function serve() {
  const srv = http.createServer((q, r) => {
    let u = decodeURIComponent(q.url.split('?')[0]);
    const f = u.startsWith('/css/') ? path.join(top, 'jfb', u) : path.join(top, u);
    fs.readFile(f, (e, b) => {
      if (e) { r.writeHead(404); r.end(); return; }
      r.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream', 'cache-control': 'no-store',
        // cross-origin isolated, so performance.now() has 5 us resolution rather than 100 us
        'cross-origin-opener-policy': 'same-origin', 'cross-origin-embedder-policy': 'require-corp' });
      r.end(b);
    });
  }).listen(0);
  return { port: srv.address().port, close: () => srv.close() };
}
