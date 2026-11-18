// tools/cdp.mjs must only ever drive the Chrome it started. Decoys answer /json/version as a Chrome would
// on every free port of the range cdp.mjs once picked from (8730-8739), and a run launches with a flag of
// its own: it must reach its own browser (the flag is there), within 20 s (the old code, sent to a decoy,
// waits forever for a WebSocket that never opens). Exit 1 on failure.
// Run it alone: while the decoys are up, another run still on a picked port could reach one of them.
// Then a run that ends without close() must leave no Chrome: a child launches one, prints its pid and ends
// by process.exit, by an uncaught throw, or by SIGTERM, SIGINT or SIGHUP from here; the Chrome must be
// gone within 5 s (the old cdp.mjs left it running, orphaned).
import http from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { launch, sleep } from './cdp.mjs';

if (process.argv[2] === 'leak') { // the child: launch, say the pid, end as told, never close
  const b = await launch();
  console.log('pid ' + b.pid);
  if (process.argv[3] === 'exit') process.exit(3);
  if (process.argv[3] === 'throw') throw new Error('thrown on purpose');
  await new Promise(() => setInterval(() => {}, 1000)); // until the parent's signal
}
const decoys = [];
const decoyTimer = setTimeout(() => { console.log('FAIL no browser within 20 s (a decoy, most likely)'); process.exit(1); }, 20000);
decoyTimer.unref();
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
clearTimeout(decoyTimer);
const gone = async (pid) => {
  for (let i = 0; i < 50; i++) { try { process.kill(pid, 0); } catch { return true; } await sleep(100); }
  return false;
};
for (const [how, sig] of [['exit'], ['throw'], ['signal', 'SIGTERM'], ['signal', 'SIGINT'], ['signal', 'SIGHUP']]) {
  const c = spawn(process.execPath, [fileURLToPath(import.meta.url), 'leak', how], { stdio: ['ignore', 'pipe', 'ignore'] });
  let out = '';
  c.stdout.on('data', (d) => { out += d; if (sig && /pid \d+\n/.test(out)) c.kill(sig); });
  await new Promise((r) => c.on('exit', r));
  const pid = +(/pid (\d+)/.exec(out) || [0, 0])[1];
  const ok = pid > 0 && await gone(pid);
  if (!ok && pid) try { process.kill(pid, 'SIGKILL'); } catch {} // leave none behind, even failing
  check(ok, `a run that ends by ${sig || how} without close() leaves no Chrome (pid ${pid || 'none printed'})`);
}
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
