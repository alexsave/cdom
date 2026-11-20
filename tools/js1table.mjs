// A copy of a built page whose string table Netscape 3 can parse, for the XP VM (docs/js1-transform-design.md,
// section 10): tools/cdlits writes the table as an array literal (var cdomLits=[...]), which JavaScript 1.1 has not,
// and this writes it as var cdomLits = new Array(); and one assignment an entry, each value as cdlits wrote it.
// node tools/js1table.mjs <in.html> <out.html>. The page is otherwise unchanged; tests/build.sh makes the -ns copies.
import fs from 'node:fs';
import { createRequire } from 'node:module';

const acorn = createRequire(new URL('./legacy/package.json', import.meta.url))('acorn');
const [src, out] = process.argv.slice(2);
const html = fs.readFileSync(src, 'utf8');
const at = html.indexOf('var cdomLits=['), end = at < 0 ? -1 : html.indexOf('];', at);
if (at < 0 || end < 0 || html.indexOf('var cdomLits=[', at + 1) >= 0) throw new Error(`${src}: not one var cdomLits=[...]; table`);
const table = html.slice(at, end + 2);
const tree = acorn.parse(table, { ecmaVersion: 5 });
const items = tree.body[0].declarations[0].init.elements;
const lines = ['var cdomLits = new Array();', ...items.map((e, i) => `cdomLits[${i}] = ${table.slice(e.start, e.end)};`)];
const { js11 } = await import('./legacy/js11.mjs');
js11(`${src}'s table`, lines.join('\n'));
fs.writeFileSync(out, html.slice(0, at) + lines.join('\n') + html.slice(end + 2));
