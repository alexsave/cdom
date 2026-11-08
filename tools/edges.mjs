// The name gate: cdom and its sibling C library are two repos with zero edges between them (neither
// depends on the other), so no tracked file of cdom's names that library, docs aside. A doc is prose
// kept as history (RESULTS.md, PERF_LOG.md, BRIDGE.md and README.md: every *.md) and the LICENSE;
// everything else git tracks is scanned, code, scripts, pages, test inputs and measurement records
// alike, case-insensitively. Fails naming each file and line.
// Run by tests/link/run.sh; standalone: node tools/edges.mjs [dir].
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const top = path.resolve(process.argv[2] || path.join(path.dirname(new URL(import.meta.url).pathname), '..'));
// Spelled in pieces, so this file does not name it itself: the library's name and its old spelling.
const NAME = new RegExp('c_?' + 'base', 'i');
const isDoc = (f) => f.endsWith('.md') || f === 'LICENSE';
const files = execFileSync('git', ['ls-files', '-z'], { cwd: top, encoding: 'utf8' }).split('\0').filter(Boolean);
let hits = 0;
for (const f of files) {
  if (isDoc(f)) {
    continue;
  }
  let text;
  try {
    text = fs.readFileSync(path.join(top, f), 'latin1');
  } catch {
    continue; // tracked, deleted in the tree
  }
  if (!NAME.test(text)) {
    continue;
  }
  text.split('\n').forEach((line, i) => {
    if (NAME.test(line)) {
      console.log(`FAIL ${f}:${i + 1}: names the other repo: ${line.trim().slice(0, 120)}`);
      hits++;
    }
  });
}
if (!hits) {
  console.log(`ok   edges: none of ${files.filter((f) => !isDoc(f)).length} tracked non-doc files names the other repo`);
}
process.exit(hits ? 1 : 0);
