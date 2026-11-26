// Opens the page tests/link/run.sh built from the fixture app tests/link/apps/css, with css.html as its
// page, from disk (file://) in headless Chrome, and checks what the bundled stylesheets do there: the
// cascade follows the byte order of the files' paths (b/00.css is last, so it wins on the same
// selector, and the page's own rule after the marker wins over all of them), the </style in a string
// reads back exactly and ends nothing (no script ran: window.pwned is undefined), and nothing is fetched.
// Usage: node tests/link/css.mjs path/to/index.html
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { launch, sleep } from '../../tools/cdp.mjs';

const page = path.resolve(process.argv[2]);
let bad = 0;
const ok = (c, m) => { console.log(c ? 'ok  ' : 'FAIL', m); if (!c) bad++; };
const b = await launch();
const errors = [];
b.onEvent = (e) => {
    if (e.method === 'Runtime.exceptionThrown') errors.push(e.params.exceptionDetails.exception?.description || e.params.exceptionDetails.text);
    if (e.method === 'Runtime.consoleAPICalled' && e.params.type === 'error') errors.push(e.params.args.map((a) => a.value ?? a.description).join(' '));
};
try {
    await b.send('Page.navigate', { url: pathToFileURL(page).href });
    let seen = null;
    for (let i = 0; i < 100 && !seen; i++) {
        await sleep(50);
        seen = await b.evaluate(`(() => {
      const p = document.getElementById('probe');
      if (!p || document.readyState !== 'complete') return null;
      const s = getComputedStyle(p), before = getComputedStyle(p, '::before');
      return { last: s.getPropertyValue('--last').trim(), first: s.getPropertyValue('--first').trim(),
        after: s.getPropertyValue('--after').trim(), hidden: s.getPropertyValue('--hidden').trim(),
        content: before.content, pwned: typeof window.pwned, styles: document.querySelectorAll('style').length,
        scripts: document.scripts.length, fetched: performance.getEntriesByType('resource').map((e) => e.name) };
    })()`);
    }
    ok(seen, 'the page loaded');
    if (seen) {
        ok(seen.last === '"b/00"', `the last stylesheet in byte order of its path, b/00.css, wins on the same selector (--last is ${seen.last})`);
        ok(seen.first === '"page"' && seen.after === '"page"', `the page's own rules before and after the marker are kept (--first ${seen.first}, --after ${seen.after})`);
        ok(seen.hidden === '', `no skipped file reached the page (--hidden is ${JSON.stringify(seen.hidden)})`);
        ok(seen.content === '"</style><script>window.pwned=1</script>"', `the </style in a string reads back exactly (${seen.content})`);
        ok(seen.pwned === 'undefined' && seen.styles === 1, `it ended nothing: no script ran (window.pwned is ${seen.pwned}), one <style> (${seen.styles})`);
        ok(seen.fetched.length === 0, `nothing was fetched but the page (${JSON.stringify(seen.fetched)})`);
    }
    ok(errors.length === 0, `no error on the page${errors.length ? `: ${errors.join(' | ')}` : ''}`);
} finally {
    await b.close();
}
process.exit(bad ? 1 : 0);
