// node tools/ns2render_chrome.mjs <url> <out.png>: opens demo/ns2render's frameset in headless Chrome, waits for the first
// draw (the view's end.gif), and saves a screenshot: a check of host.js outside Netscape 2 (Chrome runs its JavaScript 1.0).
import { launch, sleep } from './cdp.mjs';
import fs from 'node:fs';
const [url, out] = process.argv.slice(2);
const b = await launch();
await b.send('Emulation.setDeviceMetricsOverride', { width: 640, height: 400, deviceScaleFactor: 1, mobile: false });
await b.send('Page.navigate', { url });
let ok = 0;
for (let i = 0; i < 100 && !ok; i++) {
  await sleep(200);
  ok = await b.evaluate("(function(){try{var d=frames[0].document;return d.images.length>100&&/end\\.gif/.test(d.images[d.images.length-1].src)?1:0}catch(e){return 0}})()");
}
const t = await b.evaluate("frames[0].document.images[frames[0].document.images.length-1].src");
const { result } = await b.send('Page.captureScreenshot', { format: 'png' });
fs.writeFileSync(out, Buffer.from(result.data, 'base64'));
console.log(out, ok ? 'drawn' : 'NOT drawn', t, b.exceptions.join(' '));
await b.close();
process.exit(0);
