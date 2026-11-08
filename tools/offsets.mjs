// The shim hardcodes no offset into a C struct: every field offset and record size comes from
// cd_layout (src/cdom.c, CD_LAYOUT, filled with offsetof and sizeof), read once at start. This scan
// finds every place shim/cdom.js reaches C memory, an index into one of its views (u8, u32, i32, f32,
// f64), their set and subarray, a typed array made over memory.buffer (its offset) and the bytes and
// copyBytes helpers (their pointer), and fails on a number written into one: `i32[record + 11]`,
// `u32[at + 2]`. A name set to a pointer in words (`const record = ptr >> 2`) is a base: indexed bare
// (`i32[record]`, a first field by position) or stepped by a number (`index * 4`, `meta += 3`), it
// fails too. A shift (`ptr >> 2`, `>> 1`) is a unit, bytes to words or words to doubles, not an offset,
// so it is the one literal allowed. The old shim (main before the offsets moved) fails on all 30 places.
// Run by tools/unit.mjs; standalone: node tools/offsets.mjs [file] lists what it finds.
import fs from 'node:fs';
import path from 'node:path';

const VIEW = /\b(?:u8|u32|i32|f32|f64)(?:\s*\[|\.(?:set|subarray)\s*\()|\bnew\s+\w*Array\s*\(\s*memory\.buffer\s*,|\b(?:copyBytes|bytes)\s*\(/g;

// The text from just after an opening bracket to its partner, and where it ends.
function balanced(text, from) {
  let level = 1;
  for (let i = from; i < text.length; i++) {
    const c = text[i];
    if (c === '(' || c === '[' || c === '{') {
      level++;
    } else if (c === ')' || c === ']' || c === '}') {
      if (--level === 0) {
        return text.slice(from, i);
      }
    }
  }
  return text.slice(from);
}
// The first argument of an argument list, at top level.
function firstArgument(args) {
  let level = 0;
  for (let i = 0; i < args.length; i++) {
    const c = args[i];
    if (c === '(' || c === '[' || c === '{') {
      level++;
    } else if (c === ')' || c === ']' || c === '}') {
      level--;
    } else if (c === ',' && level === 0) {
      return args.slice(0, i);
    }
  }
  return args;
}

// Comments blanked, lines kept (a "//" after a colon is a URL, not a comment).
const uncommented = (source) => source.replace(/(^|[^:])\/\/[^\n]*/gm, (c, keep) => keep + ' '.repeat(c.length - keep.length));

// Every literal offset into C memory in source: [{ line, text }].
export function literalOffsets(source) {
  const code = uncommented(source);
  const found = [];
  for (const m of code.matchAll(VIEW)) {
    const inside = balanced(code, m.index + m[0].length);
    let reach = inside; // an index, or set's and subarray's arguments: all of it
    if (m[0].endsWith(',')) {
      reach = firstArgument(inside); // a typed array over memory.buffer: its offset, the argument after the buffer
    } else if (/\b(?:copyBytes|bytes)\s*\($/.test(m[0])) {
      reach = firstArgument(inside); // the helpers: their pointer
    }
    const bare = reach.replace(/>>>?\s*\d+/g, '');
    // A number in it is an offset; an array literal set onto a view writes a struct by position.
    if (/\b\d+(?:\.\d+)?\b/.test(bare) || (/\.set\s*\($/.test(m[0]) && /^\s*\[/.test(inside))) {
      const line = code.slice(0, m.index).split('\n').length;
      found.push({ line, text: source.split('\n')[line - 1].trim() });
    }
  }
  // A base: a name set to a pointer turned into words or doubles (`const record = ptr >> 2`). Read
  // bare (`i32[record]`) it is the struct's first field by position, an offset of 0 written as nothing;
  // moved by a number (`index * 4` in its value, `meta += 3`) it steps by a hardcoded size.
  const bases = new Set();
  const report = (index) => {
    const line = code.slice(0, index).split('\n').length;
    found.push({ line, text: source.split('\n')[line - 1].trim() });
  };
  for (const m of code.matchAll(/\b(?:const|let|var)\s+(\w+)\s*=\s*([^;\n]*)/g)) {
    if (/>>>?\s*[23]\b/.test(m[2]) && !/=>/.test(m[2])) {
      bases.add(m[1]);
      if (/\b\d+\b/.test(m[2].replace(/>>>?\s*\d+/g, ''))) {
        report(m.index);
      }
    }
  }
  for (const name of bases) {
    for (const m of code.matchAll(new RegExp(`\\b(?:u8|u32|i32|f32|f64)\\s*\\[\\s*${name}\\s*\\]|\\b${name}\\s*[-+]=\\s*[^;,)\\n]*\\d`, 'g'))) {
      report(m.index);
    }
  }
  found.sort((x, y) => x.line - y.line);
  return found;
}
// How many places the scan looked at, so a scan that matches nothing is not mistaken for a pass.
export function memoryReaches(source) {
  return [...uncommented(source).matchAll(VIEW)].length;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname)) {
  const file = process.argv[2] || path.join(path.dirname(new URL(import.meta.url).pathname), '../shim/cdom.js');
  const source = fs.readFileSync(file, 'utf8');
  const found = literalOffsets(source);
  for (const f of found) {
    console.log(`FAIL ${path.basename(file)}:${f.line}: a literal offset into C memory: ${f.text}`);
  }
  console.log(`${memoryReaches(source)} reaches into C memory, ${found.length} with a literal offset`);
  process.exit(found.length ? 1 : 0);
}
