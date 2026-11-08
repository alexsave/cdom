// tools/cdp.mjs must only ever drive the Chrome it started. Decoys answer /json/version as a Chrome would
// on every free port of the range cdp.mjs once picked from (8730-8739), and a run launches with a flag of
// its own: it must reach its own browser (the flag is there), within 20 s (the old code, sent to a decoy,
// waits forever for a WebSocket that never opens). Exit 1 on failure.
// Run it alone: while the decoys are up, another run still on a picked port could reach one of them.
import http from 'node:http';
import { launch } from './cdp.mjs';

const decoys = [];
setTimeout(() => { console.log('FAIL no browser within 20 s (a decoy, most likely)'); process.exit(1); }, 20000).unref();
for (let port = 8730; port <= 8739; port++) {
  const srv = http.createServer((q, r) => {
    r.writeHead(200, { 'content-type': 'application/json' });
    r.end(JSON.stringify({ Browser: 'decoy', webSocketDebuggerUrl: `ws://127.0.0.1:${port}/devtools/browser/decoy` }));
  });
  await new Promise((ok) => srv.once('error', ok).listen(port, '127.0.0.1', () => { decoys.push(srv); ok(); }));
}
let fails = 0;
const check = (c, m) => { console.log(c ? 'ok  ' : 'FAIL', m); if (!c) fails++; };
try {
  const b = await launch(['--js-flags=--expose-gc']);
  try {
    check(b.version !== 'decoy' && /Chrome/.test(b.version), `the browser it reached is a Chrome: ${b.version}`);
    check(await b.evaluate('typeof gc') === 'function', 'and the one it launched (its --expose-gc is in effect)');
  } finally { await b.close(); }
} catch (e) { check(false, `launch: ${e.message}`); }
for (const d of decoys) d.close();
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
