// The seed gate: an app's random draws start from cd_seed (src/cdom.h), the one seed, which a page fixes
// in one line (cdom.start's options.seed), so a run is made deterministic there and nowhere else. A
// generator seeded with a literal in C is a second source that line does not reach, so no tracked C file
// (demo/, tests/, bench/, micro/ and src/ alike) gives a variable named for a seed or a generator (rng,
// seed, rand in its name) a number, or passes a number to a seeding call (srand, *seed*(...)).
// ALLOWED lists the fixtures that must draw the same numbers on every run and cannot reach cd_seed, each
// with why; a test that can reach cd_seed pins it through the page instead (tests/unit.html).
// Fails naming each file and line. Run by tests/link/run.sh; standalone: node tools/seeds.mjs [dir].
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const top = path.resolve(process.argv[2] || path.join(path.dirname(new URL(import.meta.url).pathname), '..'));
const LIT = String.raw`(?:\(\s*\w+\s*\)\s*)?(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*`;
const NAME = String.raw`\b\w*(?:rng|seed|rand)\w*`;
const ASSIGN = new RegExp(String.raw`${NAME}\s*=\s*${LIT}\s*[;,)]`, 'i');
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
if (!hits) {
  console.log(`ok   seeds: none of ${files.length} tracked C files seeds a generator with a literal (${used.size} allowed fixture)`);
}
process.exit(hits ? 1 : 0);
