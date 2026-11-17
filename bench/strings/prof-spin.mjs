// Loads jsread.html with spin=8, for sample.sh.
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const srv = serve();
const b = await launch();
try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/strings/jsread.html?${process.argv[2]}&spin=8` });
  let r;
  for (let i = 0; i < 400 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
  console.log(JSON.stringify(r));
} finally { await b.close(); srv.close(); }
