// shim/cdom.min.js is generated from shim/cdom.js here, and committed, so an app inlines the small
// shim with no JS toolchain of its own: cdom runs the minifier, once, and apps copy a file.
//
//   node tools/minshim.mjs          writes shim/cdom.min.js
//   node tools/minshim.mjs check    fails when shim/cdom.min.js is not what cdom.js makes now
//
// The check runs in tests/build.sh, so an edit to cdom.js without regenerating fails the build.
// terser is pinned in the root package.json (package-lock.json), so the same source gives the same bytes.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { stripTable } from './striptable.mjs';

const top = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const src = path.join(top, 'shim', 'cdom.js');
const out = path.join(top, 'shim', 'cdom.min.js');
const tableOut = path.join(top, 'shim', 'cdom.strip');
const { minify } = createRequire(path.join(top, 'package.json'))('terser');

const HEADER = '// GENERATED from shim/cdom.js by tools/minshim.mjs - do not edit; run `node tools/minshim.mjs`.\n';

const made = await minify(fs.readFileSync(src, 'utf8'), { compress: { passes: 2 }, mangle: true });
const text = HEADER + made.code + '\n';
const table = stripTable(text.slice(HEADER.length)).replace(/^(cdomstrip 1 )\d+/, (_, head) => head + text.length)
  .replace(/^([FVGI]) (\d+) (\d+)/gm, (_, kind, start, end) => `${kind} ${+start ? +start + HEADER.length : 0} ${+end ? +end + HEADER.length : 0}`);

if (process.argv[2] === 'check') {
  const have = fs.existsSync(out) ? fs.readFileSync(out, 'utf8') : '';
  const haveTable = fs.existsSync(tableOut) ? fs.readFileSync(tableOut, 'utf8') : '';
  if (have !== text || haveTable !== table) {
    console.error('minshim: shim/cdom.min.js is stale - it is not what shim/cdom.js makes now; run `node tools/minshim.mjs` and commit it');
    process.exit(1);
  }
  console.log('minshim: shim/cdom.min.js matches shim/cdom.js');
} else {
  fs.writeFileSync(out, text);
  fs.writeFileSync(tableOut, table);
  console.log(`minshim: wrote shim/cdom.min.js (${Buffer.byteLength(text)} bytes)`);
}
