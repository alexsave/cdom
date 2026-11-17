// Runs jsread.html for each reader, length and kind, one navigation each; TSV out.
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const srv = serve();
const b = await launch();
const readers = (process.argv[2] || 'loop,td,td_fixed,subarray,fcc_apply,arr,args').split(',');
try {
  console.log(`# ${b.version}`);
  console.log('read\tkind\tlen\tns\tq1\tq3\tok');
  for (const kind of ['ascii', 'utf8'])
    for (const len of [1, 4, 9, 12, 13, 16, 24, 30, 64, 200])
      for (const read of readers) {
        if (kind === 'utf8' && (len < 2 || ['args', 'fcc_apply'].includes(read))) continue;
        await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/strings/jsread.html?read=${read}&len=${len}&kind=${kind}` });
        let r;
        for (let i = 0; i < 300 && !r; i++) { await sleep(50); try { r = await b.evaluate('window.RESULT'); } catch {} }
        console.log([read, kind, len, r.ns.toFixed(2), r.lo.toFixed(2), r.hi.toFixed(2), r.ok].join('\t'));
      }
} finally { await b.close(); srv.close(); }
