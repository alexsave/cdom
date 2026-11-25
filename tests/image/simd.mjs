// Runs tests/image/simd.c's module (tests/image/simd.sh): each loop's differential test, and prints one line each.
// Usage: node tests/image/simd.mjs simd.wasm      Exit 1 when any loop differed or trapped (a trap is a read or a write past a buffer).
import fs from 'node:fs';
const names = ['idct4 (jsimd_idct_4x4 against jpeg_idct_4x4)', 'idct2 (jsimd_idct_2x2 against jpeg_idct_2x2)',
  'ycc random widths (jsimd_color_deconvert against libjpeg ycc_extrgbx_convert)', 'ycc every (y, cb, cr)',
  'unfilter (every filter, 1 to 8 bytes a pixel)', 'rgb to rgba', 'scaler across', 'scaler down', 'scaler divide (in doubles)',
  'adler32 (SIMD blocks against a byte at a time)',
  'unread coefficients (every IDCT size, scalar and SIMD, against cd_image_idct_reads)',
  'paeth runs (the SIMD wavefronts over 2 to 4 rows of 3 and 4 bytes, 3 to 16 of 1 and 3 to 8 of 2, and the scalar run, against a row at a time)',
  'scaler across grey (the grey read against across on the pixels as RGBA)'];
const { instance } = await WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {});
const x = instance.exports;
let bad = 0;
for (let i = 0; i < names.length; i++) {
    let r, trap = '';
    try { r = x.run(i); } catch (e) { r = -1; trap = ` trapped: ${e.message}`; }
    const cases = x.last_case() + 1;
    if (r === 0) console.log(`ok   ${names[i]}: ${cases} cases equal`);
    else { bad++; console.log(`FAIL ${names[i]}: ${trap ? 'case ' + (x.last_case()) + trap : `case ${x.first_case()} differs (detail ${r})`}`); }
}
process.exit(bad ? 1 : 0);
