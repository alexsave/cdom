// Checks each page's rendered DOM (body innerHTML) is identical across the designs.
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const srv = serve();
const b = await launch();
let bad = 0;
try {
    for (const group of process.argv.slice(2)) {
        const [ref, ...rest] = group.split(',');
        const get = async (p) => { await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/${p}` }); await sleep(800); return b.evaluate('document.getElementById("app").innerHTML'); };
        const want = await get(ref);
        for (const p of rest) { const got = await get(p); const ok = got === want && want.length > 100; if (!ok) bad++; console.log(ok ? 'same' : 'DIFF', p, got.length, want.length); }
    }
} finally { await b.close(); srv.close(); }
process.exit(bad ? 1 : 0);
