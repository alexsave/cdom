// The string-names gate: the old string API is gone (a literal is plain C, a runtime string is read when
// used), so no tracked C file names it again. A name that comes back is a second way to do a thing that
// has one. Whole words only: CD_LITS_CHECK, CD_LIT_ and cd_bytes_buf are other names and are fine.
// Fails naming each file and line. Run by tests/link/run.sh; standalone: node tools/strings.mjs [dir].
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const top = path.resolve(process.argv[2] || path.join(path.dirname(new URL(import.meta.url).pathname), '..'));
const GONE = /\b(?:CD_LIT|cd_intern|cd_internz|cd_bytes)\b/;
const files = execFileSync('git', ['ls-files', '-z', '--', '*.c', '*.h'], { cwd: top, encoding: 'utf8' }).split('\0').filter(Boolean);
let hits = 0;
for (const f of files) {
  let text;
  try {
    text = fs.readFileSync(path.join(top, f), 'utf8');
  } catch {
    continue; // tracked, deleted in the tree
  }
  text.split('\n').forEach((line, i) => {
    const m = GONE.exec(line);
    if (m) {
      console.log(`FAIL ${f}:${i + 1}: ${m[0]} is gone, write the string: ${line.trim().slice(0, 120)}`);
      hits++;
    }
  });
}
if (!hits) {
  console.log(`ok   strings: none of ${files.length} tracked C files names CD_LIT, cd_intern, cd_internz or cd_bytes`);
}
process.exit(hits ? 1 : 0);
