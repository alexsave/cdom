// The cd_state prototype's checks (bench/shared-state/lib), in headless Chrome on the real shim, in the
// style of tutorial/test.mjs: each named check prints ok or FAIL, and the run exits 1 on any FAIL.
// The exports of test.c are the entries; a check that expects a trap catches the wasm RuntimeError, and a
// check that traps runs in a fresh navigation (a trapped module is left mid-pass).
// Usage: bench/shared-state/test/build.sh && node bench/shared-state/test/test.mjs [outdir]
//   outdir: the modules' directory under test/ (default out; mutate.sh passes a mutant's).
import { launch, sleep } from '../../../tools/cdp.mjs';
import { serve } from '../../strings/serve.mjs';
const out = process.argv[2] || 'out';
let bad = 0;
const ok = (c, m) => { console.log(c ? 'ok  ' : 'FAIL', m); if (!c) bad++; };
const srv = serve();
const b = await launch();
// Loads a fresh page of one build; x(expr) runs an expression over the exports X and returns
// {v} or {err} (the message, prefixed TRAP for a wasm RuntimeError).
async function page(build) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/shared-state/test/test.html?build=${build}&out=${out}` });
    for (let i = 0; i < 100 && !(await b.evaluate('!!window.X || !!window.LOADERR')); i++) await sleep(50);
    const err = await b.evaluate('window.LOADERR || null');
    if (err) throw new Error(err);
    return async (expr) => b.evaluate(`(() => { try { return { v: ${expr} }; } catch (e) {
    return { err: (e instanceof WebAssembly.RuntimeError ? 'TRAP ' : 'ERROR ') + e.message }; } })()`);
}
const show = (r) => JSON.stringify(r);
try {
    let x = await page('test');
    ok((await x('X.check_build()')).v === 0, 'the plain build has no CD_STATE_CHECK');

    // 1
    let r = await x('X.t1_setup()');
    ok(r.v === 1, `1. the view draws once in its build (${show(r)})`);
    r = await x('X.t1_go()');
    ok(r.v === 1, `1. a view watching two states, one changed 1,000 times and the other once in one entry, runs exactly once (${show(r)})`);
    r = await x('[X.runs(), X.t1_go(), X.runs()]');
    ok(r.v && r.v[1] === 1 && r.v[2] - r.v[0] === 1, `1. cd_state_runs counts that one run (${show(r)})`);

    // 2
    r = await x('X.t2_setup()');
    ok(r.v === 1, `2. setup (${show(r)})`);
    r = await x('X.t2_go()');
    ok(r.v === 0, `2. a state changed and then the watching view's region hidden in the same entry: the view does not run (${show(r)})`);
    r = await x('X.watchers(2)');
    ok(r.v === 0, `2. and nothing watches the state after (${show(r)})`);

    // 3
    r = await x('X.t3_setup()');
    ok(r.v === 11, `3. setup: both views drew (${show(r)})`);
    r = await x('X.t3_go()');
    ok(r.v === 10, `3. two views queued in one round, the first hides the second's region: the second does not run (${show(r)})`);
    r = await x('X.watchers(3)');
    ok(r.v === 1, `3. and only the first still watches (${show(r)})`);

    // 4
    r = await x('X.t4_setup()');
    ok(r.v === 1, `4. setup (${show(r)})`);
    r = await x('X.t4_go()');
    ok(r.v === 111, `4. a view shows a region whose build watches the state being processed: one run of the pass, the new view drew in its build only (runs*100+a*10+b: ${show(r)})`);
    r = await x('X.watchers(4)');
    ok(r.v === 2, `4. the new view watches the state (${show(r)})`);
    r = await x('X.t4_go()');
    ok(r.v === 211, `4. the next change runs both, once each (${show(r)})`);

    // 5
    r = await x('X.t5_setup()');
    ok(r.v === 3, `5. setup (${show(r)})`);
    r = await x('X.t5_go()');
    ok(r.v === 11, `5. a view that changes another state: that state's watcher runs in the same flush (${show(r)})`);
    r = await x('X.t5_loop()');
    ok(typeof r.err === 'string' && r.err.startsWith('TRAP'), `5. a view that always changes its own state traps (${show(r)})`);

    // 6
    x = await page('test');
    r = await x('X.t6_show()');
    ok(r.v === 1, `6. the region's view watches the state (${show(r)})`);
    r = await x('[X.t6_set(1), document.querySelector("#app span").textContent]');
    ok(r.v && r.v[1] === '1', `6. it follows a change (${show(r)})`);
    r = await x('X.t6_hide()');
    ok(r.v === 0, `6. after the hide the state's watcher list is empty (${show(r)})`);
    r = await x('X.t6_set(2)');
    ok(r.v === 2, `6. a change while hidden runs nothing and throws nothing (the view's count stays 2) (${show(r)})`);
    r = await x('[X.t6_show(), document.querySelector("#app span").textContent]');
    ok(r.v && r.v[0] === 1 && r.v[1] === '2', `6. shown again, it draws the current value at build (${show(r)})`);
    r = await x('[X.t6_set(3), document.querySelector("#app span").textContent, document.querySelectorAll("#app span").length]');
    ok(r.v && r.v[1] === '3' && r.v[2] === 1, `6. and follows later changes (${show(r)})`);

    // 7
    r = await x('X.t7_go()');
    ok(r.v === 100, `7. cd_watch_at / cd_unwatch: unwatch while queued drops the run, and one unwatched before the flush never runs (a*100+b*10+c: ${show(r)})`);
    r = await x('X.t7_after()');
    ok(r.v === 0, `7. nothing watches after the three unwatches (${show(r)})`);

    // 8
    x = await page('test-check');
    ok((await x('X.check_build()')).v === 1, 'the check build has CD_STATE_CHECK');
    r = await x('X.t8_setup()');
    ok(r.v === 1, `8. setup (${show(r)})`);
    r = await x('X.t8_good()');
    ok(r.v === 21, `8. check build: views reading only what they watch do not trap (${show(r)})`);
    r = await x('X.t8_bad()');
    ok(typeof r.err === 'string' && r.err.startsWith('TRAP'), `8. check build: a view reading a state it does not watch traps (${show(r)})`);
} finally { await b.close(); srv.close(); }
console.log(bad ? `${bad} FAILED` : 'all ok');
process.exit(bad ? 1 : 0);
