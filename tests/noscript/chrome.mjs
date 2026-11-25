// The no-script fallback in headless Chrome, for tests/noscript/run.sh (docs/noscript-design.md). Each page is served
// from its own folder on 127.0.0.1, and every request is logged from the network domain (a preload scanner's too).
//
//   node tests/noscript/chrome.mjs off  page.html shot.gif=WxH snow.gif=WxH   script off: the fallback, and only it
//   node tests/noscript/chrome.mjs on   page.html shot.gif=WxH snow.gif=WxH   script on: the app; the fallback a comment, no GIF fetched
//   node tests/noscript/chrome.mjs gifs page.html                             prints the GIFs a page with script requests (a measurement)
//   node tests/noscript/chrome.mjs ends page.html...                          for each page, whether every <script> ended at its
//                                                                             first </script> (ended) or one ran on past it (ran-on):
//                                                                             their texts against page.html.content, each script's
//                                                                             text to its first </script>, NUL between
// Prints ok and FAIL lines; exits 1 on a FAIL.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { launch, sleep } from '../../tools/cdp.mjs';

const TEXT = "Brother I'm sorry, the only way I could support your browser is if I had a server running just for you. "
    + 'And even if I did, anything you sent to that server would be decrypted. Regrettably I have chosen to drop support on this browser. '
    + "Here's a snowman";
const [mode, ...rest] = process.argv.slice(2);
let fails = 0;
const check = (pass, msg) => { console.log(`${pass ? 'ok  ' : 'FAIL'} ${msg}`); if (!pass) fails++; };
const img = (a) => { const m = /^([^=]+)=(\d+)x(\d+)$/.exec(a || ''); return m && { name: m[1], w: +m[2], h: +m[3] }; };

function serve(dir) {
    const server = http.createServer((req, res) => {
        const f = path.resolve(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/^\/+/, ''));
        if (!f.startsWith(dir + path.sep) || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
        const type = { '.html': 'text/html', '.gif': 'image/gif', '.wasm': 'application/wasm', '.js': 'text/javascript' }[path.extname(f)] || 'application/octet-stream';
        res.writeHead(200, { 'content-type': type, 'cache-control': 'no-store' });
        res.end(fs.readFileSync(f));
    });
    return new Promise((r) => server.listen(0, '127.0.0.1', () => r(server)));
}

// Opens page in a fresh tab of b, script on or off; returns the URLs requested while it loaded.
async function open(b, page, script) {
    const dir = path.dirname(path.resolve(page)), server = await serve(dir), requested = [];
    b.onEvent = (d) => { if (d.method === 'Network.requestWillBeSent' && !d.params.request.url.startsWith('data:')) requested.push(d.params.request.url.split('?')[0].split('/').pop()); };
    await b.send('Network.enable');
    await b.send('Network.setCacheDisabled', { cacheDisabled: true });
    await b.send('Emulation.setScriptExecutionDisabled', { value: !script });
    const url = `http://127.0.0.1:${server.address().port}/${encodeURIComponent(path.basename(page))}`;
    await b.send('Page.navigate', { url });
    let ready = false;
    for (let i = 0; i < 200 && !ready; i++) {
        try { ready = await b.evaluate(`location.href === ${JSON.stringify(url)} && document.readyState === "complete"`); } catch {}
        if (!ready) await sleep(50);
    }
    await sleep(300); // a late request (a lazy image, a fetch the app starts) would be on its way by now
    server.close();
    return { ready, requested };
}
const collapse = (s) => s.split(/\s+/).filter(Boolean).join(' ');

const b = await launch();
try {
    if (mode === 'off' || mode === 'on') {
        const [page, shotArg, snowArg] = rest, shot = img(shotArg), snow = img(snowArg);
        if (!snow) throw new Error('usage: chrome.mjs off|on page.html shot.gif=WxH snow.gif=WxH');
        const names = [shot.name, snow.name], { ready, requested } = await open(b, page, mode === 'on');
        check(ready, `${mode}: the page loads`);
        const got = await b.evaluate(`(() => {
      const app = document.getElementById('app');
      const comments = [], walk = document.createTreeWalker(document, NodeFilter.SHOW_COMMENT);
      while (walk.nextNode()) comments.push(walk.currentNode.data);
      return { text: document.body.innerText, images: [...document.images].map((i) => [i.getAttribute('src'), i.complete, i.naturalWidth,
        i.naturalHeight, i.width, i.height, i.alt]), app: app ? app.childNodes.length : -1, noscripts: document.getElementsByTagName('noscript').length,
        comment: comments.some((c) => c.includes('Brother') && c.includes('<img src=')) };
    })()`);
        if (mode === 'off') {
            check(collapse(got.text) === TEXT, `script off: the page's text is the paragraph and nothing else: ${JSON.stringify(collapse(got.text)).slice(0, 300)}`);
            check(got.images.length === 2 && got.images.every((x, i) => x[0] === names[i] && x[1] && x[2] === [shot, snow][i].w && x[3] === [shot, snow][i].h
              && x[4] === x[2] && x[5] === x[3]), `script off: the screenshot, then the snowman, each loaded at its size and drawn at it: ${JSON.stringify(got.images)}`);
            check(names.every((n) => requested.includes(n)), `script off: both GIFs requested (${requested.filter((u) => u.endsWith('.gif')).join(' ')})`);
            check(got.app === 0, `script off: the app's mount is empty (${got.app} nodes)`);
        } else {
            check(got.app > 0 && collapse(got.text).includes('cdom'), `script on: the app drew in its mount (${got.app} nodes): ${JSON.stringify(collapse(got.text)).slice(0, 120)}`);
            check(!got.text.includes('Brother') && !got.text.includes('snowman'), 'script on: no word of the fallback shows');
            check(got.images.length === 0, `script on: no image in the document (${got.images.length})`);
            check(got.noscripts === 0 && got.comment, `script on: the fallback is in the comment the page's script opened, not in a <noscript> element (${got.noscripts} noscript, comment ${got.comment})`);
            const gifs = requested.filter((u) => names.includes(u));
            check(gifs.length === 0, `script on: neither GIF requested (${gifs.join(' ') || 'none'}; requested ${requested.join(' ')})`);
        }
    } else if (mode === 'gifs') {
        const { requested } = await open(b, rest[0], true);
        console.log(requested.filter((u) => u.endsWith('.gif')).join(' ') || 'none');
    } else if (mode === 'ends') {
        for (const page of rest) {
            const { ready } = await open(b, page, false);
            const texts = ready && await b.evaluate('[...document.scripts].map((s) => s.text).join("\\0")');
            console.log(`${path.basename(page)} ${texts === fs.readFileSync(`${page}.content`, 'latin1') ? 'ended' : 'ran-on'}`);
        }
    } else throw new Error(`chrome.mjs: no mode ${mode}`);
} finally {
    await b.close();
}
process.exit(fails ? 1 : 0);
