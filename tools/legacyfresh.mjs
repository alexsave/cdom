// Checks that tools/legacy/build.mjs rebuilds an output exactly when one of its inputs changed, and never
// leaves a stale one: in a private copy of what the builds read, a run with nothing changed writes nothing,
// and changing any one declared input (each in turn, then putting it back) rebuilds exactly the outputs
// that name it. A missing, hand-edited or unstamped output is rebuilt too. Exit 1 on any failure.
// Needs tests/build.sh to have run (it takes listfuzz.wasm and legacy.wasm from tests/out).
// Usage: node tools/legacyfresh.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const T = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-legacyfresh-')));
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL ' + m); } };

try {
  for (const f of ['package.json', 'package-lock.json', 'shim/cdom.js', 'shim/boot.js', 'tests/listfuzz.html',
    'tools/legacy/build.mjs', 'tools/legacy/package.json', 'tools/legacy/package-lock.json', 'tools/legacy/polyfills.js']) {
    fs.mkdirSync(path.dirname(path.join(T, f)), { recursive: true });
    fs.copyFileSync(path.join(top, f), path.join(T, f));
  }
  // Real copies, not links (tools/mutate.py links node_modules into its trees): the vendored files and npm's
  // tree record are inputs this changes.
  for (const d of ['node_modules', 'tools/legacy/node_modules'])
    fs.cpSync(path.join(top, d), path.join(T, d), { recursive: true, dereference: true, filter: (s) => !s.endsWith('/.fresh') });
  fs.mkdirSync(path.join(T, 'in'));
  fs.mkdirSync(path.join(T, 'out'));
  for (const w of ['listfuzz.wasm', 'legacy.wasm']) fs.copyFileSync(path.join(top, 'tests/out', w), path.join(T, 'in', w));

  const L = path.join(T, 'tools/legacy/build.mjs');
  const builds = [['shim', 'out/cdom.legacy.js'], ['app', 'in/listfuzz.wasm', 'out/listfuzz.wasm.js'],
    ['page', 'tests/listfuzz.html', 'out/listfuzz.html']].map(([cmd, ...a]) => ({ cmd, args: a.map((x) => path.join(T, x)) }));
  for (const b of builds) {
    b.out = b.args[b.args.length - 1];
    b.inputs = execFileSync('node', [L, 'inputs', b.cmd, ...b.args.slice(0, -1)], { encoding: 'utf8' }).trim().split('\n');
  }
  const rel = (f) => path.relative(T, f);
  // What each build must name at least (the brief's list); inputs() may name more, and every one is changed below.
  const common = ['tools/legacy/build.mjs', 'tools/legacy/package-lock.json', 'tools/legacy/node_modules/.package-lock.json',
    'package-lock.json', 'node_modules/.package-lock.json'];
  const must = { shim: [...common, 'shim/cdom.js', 'tools/legacy/polyfills.js', 'tools/legacy/node_modules/promise-polyfill/dist/polyfill.js',
    'tools/legacy/node_modules/pepjs/dist/pep.js'], app: [...common, 'in/listfuzz.wasm'], page: [...common, 'shim/boot.js', 'tests/listfuzz.html'] };
  for (const b of builds) for (const f of must[b.cmd]) ok(b.inputs.map(rel).includes(f), `${b.cmd} does not name ${f} as an input`);

  const stamp = (f) => { try { return fs.statSync(f, { bigint: true }).mtimeNs; } catch { return -1n; } };
  // Runs every build; returns the commands whose output was written.
  const run = () => {
    const before = builds.map((b) => stamp(b.out));
    for (const b of builds) execFileSync('node', [L, b.cmd, ...b.args], { cwd: T, stdio: ['ignore', 'ignore', 'inherit'] });
    return builds.filter((b, i) => stamp(b.out) !== before[i]).map((b) => b.cmd).join(' ');
  };
  const all = builds.map((b) => b.cmd).join(' ');
  ok(run() === all, 'the first run did not build every output');
  const outs = builds.map((b) => fs.readFileSync(b.out));
  ok(run() === '', 'a run with nothing changed wrote an output');

  // Each input, changed and then put back: both runs rebuild exactly the builds that name it.
  const files = [...new Set(builds.flatMap((b) => b.inputs))];
  for (const f of files) {
    const want = builds.filter((b) => b.inputs.includes(f)).map((b) => b.cmd).join(' ');
    const src = fs.readFileSync(f);
    // A wasm changes into another real one, so wasm2js still builds; the rest gain a trailing line.
    fs.writeFileSync(f, f.endsWith('.wasm') ? fs.readFileSync(path.join(T, 'in/legacy.wasm')) : Buffer.concat([src, Buffer.from('\n')]));
    let got = run();
    ok(got === want, `changing ${rel(f)} rebuilt [${got}], not [${want}]`);
    fs.writeFileSync(f, src);
    got = run();
    ok(got === want, `putting ${rel(f)} back rebuilt [${got}], not [${want}]`);
  }
  builds.forEach((b, i) => ok(fs.readFileSync(b.out).equals(outs[i]), `${b.cmd}: with every input back, the output differs from the first build`));

  // The output itself: deleted, edited by hand, or its stamp gone.
  for (const b of builds) {
    fs.rmSync(b.out);
    ok(run() === b.cmd, `${b.cmd}: a deleted output was not rebuilt`);
    fs.appendFileSync(b.out, '\n');
    ok(run() === b.cmd, `${b.cmd}: an edited output was not rebuilt`);
    ok(fs.readFileSync(b.out).equals(outs[builds.indexOf(b)]), `${b.cmd}: the rebuilt output differs`);
  }
  fs.rmSync(path.join(T, 'tools/legacy/node_modules/.fresh'), { recursive: true });
  ok(run() === all, 'with the stamps gone, not every output was rebuilt');
  ok(run() === '', 'the run after that wrote an output');
  console.log(fails ? `${fails} failed` : `ok: ${files.length} inputs across ${builds.length} builds, each rebuilds exactly what names it`);
} finally {
  fs.rmSync(T, { recursive: true, force: true });
}
process.exit(fails ? 1 : 0);
