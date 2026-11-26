import { launch, sleep } from '../../../tools/cdp.mjs';
const b = await launch();
await b.send('Runtime.enable');
for (const v of ['a-rerun', 'b-module', 'c-version', 'e-state']) {
    await b.send('Page.navigate', { url: `file://${process.argv[2]}/${v}/build/index.html` });
    await sleep(500);
    const badge = () => b.evaluate(`document.querySelector('nav span')?.textContent ?? null`);
    const start = await badge();
    await b.evaluate(`document.querySelector('input').focus(), 1`);
    for (const ch of 'Ada') {
        await b.send('Input.dispatchKeyEvent', { type: 'keyDown', key: ch, text: ch, unmodifiedText: ch });
        await b.send('Input.dispatchKeyEvent', { type: 'keyUp', key: ch });
    }
    await sleep(100);
    const typed = await badge();
    const err = await b.evaluate(`document.body.innerText.includes('rror') ? document.body.innerText.slice(0,200) : ''`);
    console.log(v, JSON.stringify(start), '->', JSON.stringify(typed), err);
}
process.exit(0);
