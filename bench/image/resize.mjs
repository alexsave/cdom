// One cd_image_resize on bench/image's page in a Chrome of tools/cdp.mjs, for bench/image/equal.mjs and tools/pick.mjs:
// opens page (a URL), waits for its module (window.READY), resizes the file at input (a URL) into a box x box box, and
// answers the status, the size and a SHA-256 of the RGBA block.
import { sleep } from '../../tools/cdp.mjs';

export async function resizeHash(b, page, input, box) {
    await b.send('Page.navigate', { url: page });
    for (let i = 0; i < 200 && !(await b.evaluate('!!window.READY')); i++) await sleep(25);
    return b.evaluate(`(async () => {
    const data = new Uint8Array(await (await fetch(${JSON.stringify(input)})).arrayBuffer());
    const p = X.room(data.length);
    new Uint8Array(X.memory.buffer, p, data.length).set(data);
    const status = X.resize(p, data.length, ${box}, ${box});
    const o = new Uint32Array(X.memory.buffer, X.out(), 3);
    let hash = '';
    if (status === 0) {
      const d = await crypto.subtle.digest('SHA-256', new Uint8Array(X.memory.buffer, o[2], o[0] * o[1] * 4));
      hash = [...new Uint8Array(d)].map((v) => v.toString(16).padStart(2, '0')).join('');
      X.drop(o[2]);
    }
    X.drop(p);
    return { status, w: o[0], h: o[1], hash };
  })()`);
}
