// S14, in headless Chrome through CDP Input events: for each variant (a unguarded, c upper-casing setter,
// g guarded) types "abcdef", puts the caret at offset 3 (setSelectionRange(3, 3)), types "X", and prints
// the value and selectionStart; then, for a and g, opens an IME composition (Input.imeSetComposition "ni"
// with selection 2,2, then "nih" with 3,3), commits it with Input.insertText, and prints the events a
// capture listener on the document logged (compositionstart, compositionupdate, compositionend,
// beforeinput, input with isComposing), with the value after each step. Observations only.
// Usage: bench/shared-state/caret/build.sh && node bench/shared-state/caret/caret.mjs
import { launch, sleep } from '../../../tools/cdp.mjs';
import { serve } from '../../strings/serve.mjs';
const COMMIT = '你'; // the committed text: one CJK character, as a pinyin IME commits "ni"
const srv = serve();
const b = await launch();
const ev = (e) => b.evaluate(e);
async function open(v) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/shared-state/caret/caret.html?v=${v}` });
    for (let i = 0; i < 100 && !(await ev('!!window.X || !!window.LOADERR')); i++) await sleep(50);
    const err = await ev('window.LOADERR || null');
    if (err) throw new Error(err);
    await ev(`(() => {
    window.LOG = [];
    const inp = document.querySelector('input');
    for (const t of ['compositionstart', 'compositionupdate', 'compositionend', 'beforeinput', 'input'])
      document.addEventListener(t, (e) => LOG.push([t, e.data ?? null, e.inputType ?? null, e.isComposing ?? null, inp.value, inp.selectionStart].join(' ')), true);
    inp.focus();
    return 1;
  })()`);
}
const state = async () => ev(`(() => { const i = document.querySelector('input'); return { value: i.value, selectionStart: i.selectionStart, selectionEnd: i.selectionEnd, writes: X.writes(), skips: X.skips() }; })()`);
async function type(s) {
    for (const ch of s) {
        await b.send('Input.dispatchKeyEvent', { type: 'keyDown', key: ch, text: ch, unmodifiedText: ch });
        await b.send('Input.dispatchKeyEvent', { type: 'keyUp', key: ch });
    }
    await sleep(50);
}
try {
    console.log(`# ${b.version}`);
    for (const v of ['a', 'c', 'g']) {
        await open(v);
        await type('abcdef');
        const typed = await state();
        await ev(`document.querySelector('input').setSelectionRange(3, 3), 1`);
        await type('X');
        const after = await state();
        console.log(`caret ${v}: after "abcdef" ${JSON.stringify(typed)}; caret at 3, then "X": ${JSON.stringify(after)}`);
        if (v === 'c') { // beyond the brief: "X" is upper case already, so (c)'s setter changed nothing at that step;
            // a lower-case "x" at offset 3 is the keystroke whose value the setter does change
            await ev(`document.querySelector('input').setSelectionRange(3, 3), 1`);
            await type('x');
            console.log(`caret c (extra): caret at 3 again, then lower-case "x", which the setter upper-cases: ${JSON.stringify(await state())}`);
        }
    }
    for (const v of ['a', 'g']) {
        await open(v);
        await ev('LOG.push("-- imeSetComposition ni (2,2)"), 1');
        await b.send('Input.imeSetComposition', { text: 'ni', selectionStart: 2, selectionEnd: 2 });
        await sleep(100);
        const s1 = await state();
        await ev('LOG.push("-- imeSetComposition nih (3,3)"), 1');
        await b.send('Input.imeSetComposition', { text: 'nih', selectionStart: 3, selectionEnd: 3 });
        await sleep(100);
        const s2 = await state();
        await ev(`LOG.push("-- insertText ${COMMIT}"), 1`);
        await b.send('Input.insertText', { text: COMMIT });
        await sleep(100);
        const s3 = await state();
        const log = await ev('LOG');
        const ends = log.filter((l) => l.startsWith('compositionend')).length;
        console.log(`ime ${v}: after "ni" ${JSON.stringify(s1)}; after "nih" ${JSON.stringify(s2)}; after the commit ${JSON.stringify(s3)}; compositionend ${ends} time(s)`);
        for (const l of log) console.log(`  ${v}| ${l}`);
    }
} finally { await b.close(); srv.close(); }
