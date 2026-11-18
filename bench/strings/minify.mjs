// The same terser settings as tools/minshim.mjs, for a shim at any path: minify.mjs in.js out.js
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const top = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const { minify } = createRequire(path.join(top, 'package.json'))('terser');
const made = await minify(fs.readFileSync(process.argv[2], 'utf8'), { compress: { passes: 2 }, mangle: true });
fs.writeFileSync(process.argv[3], made.code + '\n');
