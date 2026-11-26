// Safari for the benches, through safaridriver (WebDriver over HTTP): the same evaluate and close as tools/cdp.mjs's
// launch, and navigate(url) in place of CDP's Page.navigate. Safari has no headless mode, so a window opens.
// It needs "Allow Remote Automation" (Safari's Develop menu, or `safaridriver --enable` once).
// Bench and test use only: the shipped paths never load it.
import { spawn } from 'node:child_process';
import net from 'node:net';
import { sleep } from './cdp.mjs';

const freePort = () => new Promise((r) => { const s = net.createServer().listen(0, () => { const p = s.address().port; s.close(() => r(p)); }); });

export async function launch() {
    const port = await freePort();
    const proc = spawn('/usr/bin/safaridriver', ['-p', String(port)], { stdio: 'ignore' });
    const kill = () => { try { proc.kill('SIGTERM'); } catch {} };
    process.on('exit', kill);
    const url = `http://127.0.0.1:${port}`;
    const call = async (method, p, body) => {
        const r = await fetch(url + p, { method, headers: { 'content-type': 'application/json' }, body: body && JSON.stringify(body) });
        const d = await r.json();
        if (d.value && d.value.error) throw new Error(`safaridriver ${p}: ${d.value.error}: ${d.value.message}`);
        return d.value;
    };
    for (let i = 0; i < 100; i++) { try { await call('GET', '/status'); break; } catch { await sleep(100); } }
    const s = await call('POST', '/session', { capabilities: { alwaysMatch: { browserName: 'safari' } } });
    const sp = `/session/${s.sessionId}`;
    await call('POST', sp + '/timeouts', { script: 300000, pageLoad: 60000 });
    // The expression may be a promise; its value comes back as CDP's returnByValue would give it.
    const evaluate = async (expr) => {
        const v = await call('POST', sp + '/execute/async', {
            script: 'const done = arguments[arguments.length - 1];' +
              'Promise.resolve().then(() => (0, eval)(arguments[0])).then((v) => done({ v }), (e) => done({ e: String(e && e.stack || e) }));',
            args: [expr] });
        if (v.e) throw new Error(v.e);
        return v.v;
    };
    const navigate = (u) => call('POST', sp + '/url', { url: u });
    const close = async () => { try { await call('DELETE', sp); } catch {} kill(); await sleep(300); };
    return { version: 'Safari ' + s.capabilities.browserVersion, navigate, evaluate, close };
}
