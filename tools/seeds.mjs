// The seed gate: an app's random draws start from cd_seed (src/cdom.h), the one seed, which a page fixes
// in one line (cdom.start's options.seed), so a run is made deterministic there and nowhere else. A
// generator seeded with a literal in C is a second source that line does not reach, so no tracked C file
// (demo/, tests/, bench/, micro/ and src/ alike) gives a variable named for a seed or a generator (rng,
// seed, rand in its name) a number, or passes a number to a seeding call (srand, *seed*(...)).
// ALLOWED lists the fixtures that must draw the same numbers on every run and cannot reach cd_seed, each
// with why; a test that can reach cd_seed pins it through the page instead (tests/unit.html).
// An app draws from cd_rand (src/cdom.c, the one generator, seeded from cd_seed), so no file under demo/
// or tutorial/ carries a generator of its own: a xorshift step (a shift by 13) there fails too.
// The shipped JS (every tracked .js under shim/ and tools/legacy/, the files a page runs) draws from no
// generator nothing can seed: no Math.random, with two exceptions, JS_EXCEPTION and FRAMES_EXCEPTION below, one draw each.
// Fails naming each file and line. Run by tests/link/run.sh; standalone: node tools/seeds.mjs [dir].
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const top = path.resolve(process.argv[2] || path.join(path.dirname(new URL(import.meta.url).pathname), '..'));
const LIT = String.raw`(?:\(\s*\w+\s*\)\s*)?(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*`;
const NAME = String.raw`\b\w*(?:rng|seed|rand)\w*`;
const ASSIGN = new RegExp(String.raw`${NAME}\s*=\s*${LIT}\s*[;,)]`, 'i');
const OWN_GENERATOR = /\^=\s*\w+\s*<<\s*13\b/;
const APP_DIRS = ['demo/', 'tutorial/'];
const CALL = new RegExp(String.raw`\b(?:srand\w*|\w*seed\w*)\s*\(\s*${LIT}\s*\)`, 'i');
// file -> the exact line (trimmed) that may seed with a literal, and why.
const ALLOWED = {
  'tests/link/alloc.c': {
    line: 'uint32_t rng = 99;',
    // tests/link/run.mjs runs the module with every cdom import stubbed: no shim, no cdom.start, so no
    // cd_seed. Its block sizes must be the same on every run, so a heap that overlaps fails the same way.
    why: 'allocator stress fixture run without the shim (no cd_seed); fixed so a failure reproduces',
  },
};
// The one approved exception in the shipped JS: shim/cdom.js's seed() falls back on Math.random and the clock
// when the page passed no options.seed and the browser has no crypto.getRandomValues (IE 9 and 10, which have
// no cryptographic generator, on a static page with no server to send a seed). Exactly one Math.random, in
// seed(), after both of those, under a comment that says below IE 11 cd_seed is neither secret nor
// unpredictable; shim/cdom.min.js, made from it by tools/minshim.mjs, carries the same one.
const JS_DIRS = ['shim/', 'tools/legacy/'];
const JS_RANDOM = /Math\.random\b/g;
const JS_EXCEPTION = { file: 'shim/cdom.js', min: 'shim/cdom.min.js', start: '\n  function seed(options) {', end: '\n  }\n',
  comment: 'neither secret nor unpredictable' };
// The same exception, extended to the frames tier (Alex, 2026-11-20): tools/legacy/frames.js's cf_seed(options), exactly one
// Math.random, after the options.seed test, under a comment that says cd_seed there is "neither secret nor unpredictable".
// No other file and no other place in frames.js may draw one.
const FRAMES_EXCEPTION = { file: 'tools/legacy/frames.js', start: '\nfunction cf_seed(options) {', end: '\n}\n',
  comment: 'neither secret nor unpredictable' };
const files = execFileSync('git', ['ls-files', '-z', '--', '*.c', '*.h'], { cwd: top, encoding: 'utf8' }).split('\0').filter(Boolean);
let hits = 0;
const used = new Set();
for (const f of files) {
  let text;
  try {
    text = fs.readFileSync(path.join(top, f), 'utf8');
  } catch {
    continue; // tracked, deleted in the tree
  }
  text.split('\n').forEach((line, i) => {
    if (OWN_GENERATOR.test(line) && APP_DIRS.some((d) => f.startsWith(d))) {
      console.log(`FAIL ${f}:${i + 1}: a generator of its own; draw from cd_rand (src/cdom.h): ${line.trim().slice(0, 120)}`);
      hits++;
      return;
    }
    if (!ASSIGN.test(line) && !CALL.test(line)) {
      return;
    }
    const allowed = ALLOWED[f];
    if (allowed && line.trim() === allowed.line) {
      used.add(f);
      return;
    }
    console.log(`FAIL ${f}:${i + 1}: seeds a generator with a literal, not cd_seed: ${line.trim().slice(0, 120)}`);
    hits++;
  });
}
for (const f of Object.keys(ALLOWED)) {
  if (!used.has(f)) {
    console.log(`FAIL tools/seeds.mjs allows ${f}: "${ALLOWED[f].line}", which is no longer there; drop the entry`);
    hits++;
  }
}
const jsFiles = execFileSync('git', ['ls-files', '-z', '--', '*.js'], { cwd: top, encoding: 'utf8' }).split('\0')
  .filter((f) => f && JS_DIRS.some((d) => f.startsWith(d)));
const count = (text) => (text.match(JS_RANDOM) || []).length;
for (const f of jsFiles) {
  let text;
  try {
    text = fs.readFileSync(path.join(top, f), 'utf8');
  } catch {
    continue;
  }
  const n = count(text);
  if (f === JS_EXCEPTION.min) {
    if (n !== 1) {
      console.log(`FAIL ${f}: ${n} Math.random, want the one of ${JS_EXCEPTION.file}'s seed() (tools/minshim.mjs makes it)`);
      hits++;
    }
    continue;
  }
  const ex = f === JS_EXCEPTION.file ? JS_EXCEPTION : f === FRAMES_EXCEPTION.file ? FRAMES_EXCEPTION : null;
  const from = ex ? text.indexOf(ex.start) : -1;
  const to = from < 0 ? -1 : text.indexOf(ex.end, from);
  const body = to < 0 ? '' : text.slice(from, to);
  text.split('\n').forEach((line, i) => {
    if (!count(line)) return;
    const at = text.split('\n').slice(0, i).join('\n').length + 1; // the line's offset
    if (ex && from >= 0 && at > from && at < to) return; // the exception function's, checked below
    console.log(`FAIL ${f}:${i + 1}: Math.random, a generator nothing can seed (seeds from cd_seed, or options.seed in the shim): ${line.trim().slice(0, 120)}`);
    hits++;
  });
  if (!ex) continue;
  // The exception itself: one draw, after options.seed (and, in the shim, crypto.getRandomValues), under its comment
  // (inside the function in the shim, above it in frames.js).
  const frames = ex === FRAMES_EXCEPTION;
  const order = [body.indexOf('options.seed'), frames ? body.indexOf('options.seed') : body.indexOf('crypto.getRandomValues'), body.search(JS_RANDOM)];
  const around = from < 0 ? '' : text.slice(Math.max(0, from - 1500), to);
  const fn = frames ? 'cf_seed(options)' : 'seed()';
  const why = from < 0 || to < 0 ? `no ${fn} function`
    : count(body) !== 1 ? `${count(body)} Math.random in ${fn}, want exactly 1`
    : !(order[0] >= 0 && order[0] <= order[1] && order[1] < order[2]) ? `Math.random is not after options.seed${frames ? '' : ' and crypto.getRandomValues'}`
    : !around.includes(ex.comment) ? `no comment that cd_seed is then "${ex.comment}"`
    : '';
  if (why) {
    console.log(`FAIL ${f}: the allowed seed exception (${frames ? 'cf_seed() without options.seed' : 'seed() without options.seed or crypto'}): ${why}`);
    hits++;
  }
}
if (!hits) {
  console.log(`ok   seeds: none of ${files.length} tracked C files seeds a generator with a literal (${used.size} allowed fixture); ` +
    `${jsFiles.length} shipped JS files draw no Math.random but seed()'s one exception`);
}
process.exit(hits ? 1 : 0);
