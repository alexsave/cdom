// node tools/stripcheck.mjs [TABLE=path]: shim-strip experiment check (see tools/striptable.mjs).
// For many import subsets (each import alone, all but each one, random ones from one seeded generator)
// and every export subset of the hooks: a synthetic module importing them, stripped by shimstrip; the
// stripped shim must parse and name no global the full shim does not (a cut declaration still named
// shows up as a new free name).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
const require = createRequire(new URL('../package.json', import.meta.url));
const acorn = require('acorn');
const W = new URL('..', import.meta.url).pathname.replace(/\/$/, '');
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'stripcheck-'));
execFileSync(process.env.CC || 'cc', ['-std=c99', '-O2', '-o', path.join(dir, 'shimstrip'), `${W}/tools/shimstrip.c`]);
const TABLE = process.env.TABLE || `${W}/shim/cdom.strip`;
const table = fs.readFileSync(TABLE, 'utf8');
const imports = [...table.matchAll(/^I \d+ \d+ (\S+)/gm)].map((m) => m[1]);
const hookExports = [...table.matchAll(/^X \d+ \d+ \d+ (\S+)/gm)].map((m) => m[1]);
const leb = (n) => { const out = []; do { let b = n & 127; n >>>= 7; if (n) b |= 128; out.push(b); } while (n); return out; };
const str = (s) => [...leb(s.length), ...Buffer.from(s)];
const section = (id, body) => [id, ...leb(body.length), ...body];
function module(names, exportNames) {
  const type = section(1, [1, 0x60, 0, 0]);
  const imp = section(2, [...leb(names.length), ...names.flatMap((n) => [...str('cdom'), ...str(n), 0, 0])]);
  const exp = section(7, [...leb(exportNames.length), ...exportNames.flatMap((n, i) => [...str(n), 0, ...leb(i)])]);
  // export the imported functions themselves under the hook names (an export may re-export an import)
  return Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0, ...type, ...imp, ...exp]);
}
// Free names: identifiers read that no scope declares (a rough resolver: every declared name anywhere
// in the file counts as declared, so it can only miss, never invent; the cut names are mangled and
// declared once, at closure level, so their loss is seen).
function freeNames(code) {
  const ast = acorn.parse(code, { ecmaVersion: 'latest' });
  const declared = new Set();
  const used = new Set();
  (function walk(n, parent, key) {
    if (n.type === 'Identifier') {
      const isKey = (parent.type === 'MemberExpression' && key === 'property' && !parent.computed)
        || (parent.type === 'Property' && key === 'key' && !parent.computed && !parent.shorthand);
      const isDecl = (parent.type === 'VariableDeclarator' && key === 'id') || (/Function/.test(parent.type) && (key === 'id' || key === 'params'))
        || parent.type === 'CatchClause';
      if (isDecl) declared.add(n.name); else if (!isKey) used.add(n.name);
    }
    for (const k of Object.keys(n)) {
      const v = n[k];
      if (Array.isArray(v)) v.forEach((c) => c && typeof c.type === 'string' && walk(c, n, k));
      else if (v && typeof v.type === 'string') walk(v, n, k);
    }
  })(ast, {}, '');
  // patterns: names inside destructuring and arrow params are declared too
  for (const m of code.matchAll(/(?:const|let|var)\s*\[([^\]]*)\]/g)) m[1].split(',').forEach((s) => declared.add(s.trim()));
  return new Set([...used].filter((u) => !declared.has(u)));
}
const full = freeNames(fs.readFileSync(`${W}/shim/cdom.min.js`, 'utf8'));
let seed = 12345; // the experiment's one seed
const rand = () => (seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32;
const cases = [];
for (const n of imports) cases.push([n]);
for (const n of imports) cases.push(imports.filter((m) => m !== n));
for (let i = 0; i < 200; i++) cases.push(imports.filter(() => rand() < 0.3));
let bad = 0, n = 0, min = Infinity, max = 0;
for (const names of cases) {
  for (let mask = 0; mask < 1 << hookExports.length; mask++) {
    const ex = hookExports.filter((_, i) => mask >> i & 1);
    fs.writeFileSync(path.join(dir, 'syn.wasm'), module(names, ex));
    const out = execFileSync(path.join(dir, 'shimstrip'), [path.join(dir, 'syn.wasm'), `${W}/shim/cdom.min.js`, TABLE]).toString();
    n++;
    min = Math.min(min, out.length); max = Math.max(max, out.length);
    let free;
    try { free = freeNames(out); } catch (e) { bad++; console.log('PARSE', names.join(','), ex.join(','), e.message); continue; }
    const extra = [...free].filter((f) => !full.has(f));
    if (extra.length) { bad++; if (bad < 10) console.log('FREE', extra.join(','), 'for', names.join(','), ex.join(',')); }
  }
}
console.log(`${n} stripped shims, ${bad} bad; sizes ${min}..${max}`);
fs.rmSync(dir, { recursive: true });
process.exit(bad ? 1 : 0);
